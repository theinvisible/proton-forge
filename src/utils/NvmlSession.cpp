#include "utils/NvmlSession.h"

#include <QByteArray>
#include <QMutexLocker>
#include <QDebug>

#include <dlfcn.h>

// ── NVML ABI (subset) ────────────────────────────────────────────────────────
// We deliberately avoid including <nvml.h>: the app must build and run without
// the CUDA toolkit. All types/enums below mirror the stable NVML ABI. nvmlReturn_t
// is an int enum with NVML_SUCCESS == 0; nvmlDevice_t is an opaque handle.
namespace {

using nvmlDevice_t = void*;

constexpr int NVML_SUCCESS = 0;

// nvmlClockType_t
constexpr unsigned NVML_CLOCK_GRAPHICS = 0;
constexpr unsigned NVML_CLOCK_MEM      = 2;

// nvmlTemperatureSensors_t
constexpr unsigned NVML_TEMPERATURE_GPU = 0;

// nvmlTemperatureThresholds_t
constexpr unsigned NVML_TEMPERATURE_THRESHOLD_SHUTDOWN = 0;
constexpr unsigned NVML_TEMPERATURE_THRESHOLD_SLOWDOWN = 1;
constexpr unsigned NVML_TEMPERATURE_THRESHOLD_GPU_MAX  = 3;

// nvmlPowerSource_t
constexpr unsigned NVML_POWER_SOURCE_AC      = 0;
constexpr unsigned NVML_POWER_SOURCE_BATTERY = 1;

// nvmlDeviceArchitecture_t
constexpr unsigned NVML_DEVICE_ARCH_KEPLER    = 2;
constexpr unsigned NVML_DEVICE_ARCH_MAXWELL   = 3;
constexpr unsigned NVML_DEVICE_ARCH_PASCAL    = 4;
constexpr unsigned NVML_DEVICE_ARCH_VOLTA     = 5;
constexpr unsigned NVML_DEVICE_ARCH_TURING    = 6;
constexpr unsigned NVML_DEVICE_ARCH_AMPERE    = 7;
constexpr unsigned NVML_DEVICE_ARCH_ADA       = 8;
constexpr unsigned NVML_DEVICE_ARCH_HOPPER    = 9;
constexpr unsigned NVML_DEVICE_ARCH_BLACKWELL = 10;

// nvmlMemory_t (v1): field order is total, free, used.
struct nvmlMemory_t { unsigned long long total, free, used; };
// nvmlUtilization_t
struct nvmlUtilization_t { unsigned int gpu, memory; };

QString archToString(unsigned arch)
{
    switch (arch) {
        case NVML_DEVICE_ARCH_KEPLER:    return "Kepler";
        case NVML_DEVICE_ARCH_MAXWELL:   return "Maxwell";
        case NVML_DEVICE_ARCH_PASCAL:    return "Pascal";
        case NVML_DEVICE_ARCH_VOLTA:     return "Volta";
        case NVML_DEVICE_ARCH_TURING:    return "Turing";
        case NVML_DEVICE_ARCH_AMPERE:    return "Ampere";
        case NVML_DEVICE_ARCH_ADA:       return "Ada Lovelace";
        case NVML_DEVICE_ARCH_HOPPER:    return "Hopper";
        case NVML_DEVICE_ARCH_BLACKWELL: return "Blackwell";
        default:                         return QString();
    }
}

// PCIe transfer rate per lane (GT/s) for a given link generation.
double gtPerSecondForGen(int gen)
{
    switch (gen) {
        case 1: return 2.5;  case 2: return 5.0;  case 3: return 8.0;
        case 4: return 16.0; case 5: return 32.0; case 6: return 64.0;
        default: return 0.0;
    }
}

// mW → W, rounded to the nearest watt.
int mWToW(unsigned mw) { return static_cast<int>((mw + 500) / 1000); }

} // namespace

// ── Resolved NVML entry points ───────────────────────────────────────────────
struct NvmlSession::Fns {
    int (*init)();
    int (*shutdown)();
    int (*handleByPci)(const char*, nvmlDevice_t*);
    int (*handleByIndex)(unsigned, nvmlDevice_t*);

    int (*numGpuCores)(nvmlDevice_t, unsigned*);
    int (*cudaCC)(nvmlDevice_t, int*, int*);
    int (*architecture)(nvmlDevice_t, unsigned*);
    int (*memBusWidth)(nvmlDevice_t, unsigned*);
    int (*memInfo)(nvmlDevice_t, nvmlMemory_t*);
    int (*clockInfo)(nvmlDevice_t, unsigned, unsigned*);
    int (*maxClockInfo)(nvmlDevice_t, unsigned, unsigned*);
    int (*currPcieGen)(nvmlDevice_t, unsigned*);
    int (*maxPcieGen)(nvmlDevice_t, unsigned*);
    int (*currPcieWidth)(nvmlDevice_t, unsigned*);
    int (*maxPcieWidth)(nvmlDevice_t, unsigned*);
    int (*temperature)(nvmlDevice_t, unsigned, unsigned*);
    int (*tempThreshold)(nvmlDevice_t, unsigned, unsigned*);
    int (*powerUsage)(nvmlDevice_t, unsigned*);
    int (*enforcedPowerLimit)(nvmlDevice_t, unsigned*);
    int (*defaultPowerLimit)(nvmlDevice_t, unsigned*);
    int (*powerConstraints)(nvmlDevice_t, unsigned*, unsigned*);
    int (*powerSource)(nvmlDevice_t, unsigned*);
    int (*perfState)(nvmlDevice_t, unsigned*);
    int (*utilization)(nvmlDevice_t, nvmlUtilization_t*);
    int (*encoderUtil)(nvmlDevice_t, unsigned*, unsigned*);
    int (*decoderUtil)(nvmlDevice_t, unsigned*, unsigned*);
    int (*jpgUtil)(nvmlDevice_t, unsigned*, unsigned*);
    int (*ofaUtil)(nvmlDevice_t, unsigned*, unsigned*);
    int (*throttleReasons)(nvmlDevice_t, unsigned long long*);
    int (*fanSpeed)(nvmlDevice_t, unsigned*);
};

NvmlSession& NvmlSession::instance()
{
    static NvmlSession session;
    return session;
}

NvmlSession::~NvmlSession()
{
    if (m_fns) {
        if (m_available && m_fns->shutdown)
            m_fns->shutdown();
        delete m_fns;
        m_fns = nullptr;
    }
    if (m_lib) {
        dlclose(m_lib);
        m_lib = nullptr;
    }
}

bool NvmlSession::available()
{
    QMutexLocker lock(&m_mutex);
    return ensureLoadedLocked();
}

bool NvmlSession::ensureLoadedLocked()
{
    if (m_initTried)
        return m_available;
    m_initTried = true;

    // libnvidia-ml.so.1 is the versioned SONAME present on end-user systems;
    // libnvidia-ml.so only exists with the -dev package.
    m_lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!m_lib)
        m_lib = dlopen("libnvidia-ml.so", RTLD_LAZY | RTLD_LOCAL);
    if (!m_lib) {
        qDebug() << "NvmlSession: libnvidia-ml not loadable –" << dlerror();
        return false;
    }

    auto* f = new Fns{};
    auto sym = [this](const char* name) { return dlsym(m_lib, name); };

    f->init               = reinterpret_cast<int(*)()>(sym("nvmlInit_v2"));
    f->shutdown           = reinterpret_cast<int(*)()>(sym("nvmlShutdown"));
    f->handleByPci        = reinterpret_cast<int(*)(const char*, nvmlDevice_t*)>(sym("nvmlDeviceGetHandleByPciBusId_v2"));
    f->handleByIndex      = reinterpret_cast<int(*)(unsigned, nvmlDevice_t*)>(sym("nvmlDeviceGetHandleByIndex_v2"));

    f->numGpuCores        = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetNumGpuCores"));
    f->cudaCC             = reinterpret_cast<int(*)(nvmlDevice_t, int*, int*)>(sym("nvmlDeviceGetCudaComputeCapability"));
    f->architecture       = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetArchitecture"));
    f->memBusWidth        = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetMemoryBusWidth"));
    f->memInfo            = reinterpret_cast<int(*)(nvmlDevice_t, nvmlMemory_t*)>(sym("nvmlDeviceGetMemoryInfo"));
    f->clockInfo          = reinterpret_cast<int(*)(nvmlDevice_t, unsigned, unsigned*)>(sym("nvmlDeviceGetClockInfo"));
    f->maxClockInfo       = reinterpret_cast<int(*)(nvmlDevice_t, unsigned, unsigned*)>(sym("nvmlDeviceGetMaxClockInfo"));
    f->currPcieGen        = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetCurrPcieLinkGeneration"));
    f->maxPcieGen         = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetMaxPcieLinkGeneration"));
    f->currPcieWidth      = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetCurrPcieLinkWidth"));
    f->maxPcieWidth       = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetMaxPcieLinkWidth"));
    f->temperature        = reinterpret_cast<int(*)(nvmlDevice_t, unsigned, unsigned*)>(sym("nvmlDeviceGetTemperature"));
    f->tempThreshold      = reinterpret_cast<int(*)(nvmlDevice_t, unsigned, unsigned*)>(sym("nvmlDeviceGetTemperatureThreshold"));
    f->powerUsage         = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetPowerUsage"));
    f->enforcedPowerLimit = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetEnforcedPowerLimit"));
    f->defaultPowerLimit  = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetPowerManagementDefaultLimit"));
    f->powerConstraints   = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*, unsigned*)>(sym("nvmlDeviceGetPowerManagementLimitConstraints"));
    f->powerSource        = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetPowerSource"));
    f->perfState          = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetPerformanceState"));
    f->utilization        = reinterpret_cast<int(*)(nvmlDevice_t, nvmlUtilization_t*)>(sym("nvmlDeviceGetUtilizationRates"));
    f->encoderUtil        = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*, unsigned*)>(sym("nvmlDeviceGetEncoderUtilization"));
    f->decoderUtil        = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*, unsigned*)>(sym("nvmlDeviceGetDecoderUtilization"));
    f->jpgUtil            = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*, unsigned*)>(sym("nvmlDeviceGetJpgUtilization"));
    f->ofaUtil            = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*, unsigned*)>(sym("nvmlDeviceGetOfaUtilization"));
    f->throttleReasons    = reinterpret_cast<int(*)(nvmlDevice_t, unsigned long long*)>(sym("nvmlDeviceGetCurrentClocksThrottleReasons"));
    f->fanSpeed           = reinterpret_cast<int(*)(nvmlDevice_t, unsigned*)>(sym("nvmlDeviceGetFanSpeed"));

    // Only the bootstrap trio is mandatory; every data getter is probed per call
    // and simply skipped when null, so an older driver still yields what it can.
    if (!f->init || !f->shutdown || (!f->handleByPci && !f->handleByIndex)) {
        qDebug() << "NvmlSession: essential NVML symbols missing (driver too old?)";
        delete f;
        dlclose(m_lib);
        m_lib = nullptr;
        return false;
    }

    if (f->init() != NVML_SUCCESS) {
        qDebug() << "NvmlSession: nvmlInit_v2 failed";
        delete f;
        dlclose(m_lib);
        m_lib = nullptr;
        return false;
    }

    m_fns = f;
    m_available = true;
    return true;
}

void NvmlSession::enrich(GPUInfo& info)
{
    QMutexLocker lock(&m_mutex);
    if (!ensureLoadedLocked())
        return;

    const Fns* f = m_fns;

    // Resolve the device handle: prefer the PCI bus id so we address the exact
    // card in multi-GPU systems; fall back to the enumeration index.
    nvmlDevice_t dev = nullptr;
    int rc = -1;
    const QByteArray bus = info.pciId.trimmed().toLatin1();
    if (f->handleByPci && !bus.isEmpty())
        rc = f->handleByPci(bus.constData(), &dev);
    if (rc != NVML_SUCCESS && f->handleByIndex && info.index >= 0)
        rc = f->handleByIndex(static_cast<unsigned>(info.index), &dev);
    if (rc != NVML_SUCCESS || !dev)
        return;

    unsigned u = 0, u2 = 0, sampling = 0;
    int i = 0, i2 = 0;

    // ── Identity / static ──
    if (f->numGpuCores && f->numGpuCores(dev, &u) == NVML_SUCCESS && u > 0)
        info.cudaCores = static_cast<int>(u);
    if (f->cudaCC && f->cudaCC(dev, &i, &i2) == NVML_SUCCESS)
        info.computeCapability = QString("%1.%2").arg(i).arg(i2);
    if (info.architecture.isEmpty() && f->architecture && f->architecture(dev, &u) == NVML_SUCCESS) {
        const QString a = archToString(u);
        if (!a.isEmpty())
            info.architecture = a;
    }
    if (f->memBusWidth && f->memBusWidth(dev, &u) == NVML_SUCCESS && u > 0)
        info.memoryBusWidth = static_cast<int>(u);

    // ── Memory (live) ──
    if (f->memInfo) {
        nvmlMemory_t mem{};
        if (f->memInfo(dev, &mem) == NVML_SUCCESS && mem.total > 0) {
            info.memoryTotalMB = static_cast<int>(mem.total >> 20);
            info.memoryUsedMB  = static_cast<int>(mem.used  >> 20);
            info.memoryFreeMB  = static_cast<int>(mem.free  >> 20);
        }
    }

    // ── Clocks (live), MHz ──
    if (f->clockInfo && f->clockInfo(dev, NVML_CLOCK_GRAPHICS, &u) == NVML_SUCCESS && u > 0)
        info.currentGraphicsClock = static_cast<int>(u);
    if (f->clockInfo && f->clockInfo(dev, NVML_CLOCK_MEM, &u) == NVML_SUCCESS && u > 0)
        info.currentMemoryClock = static_cast<int>(u);
    if (f->maxClockInfo && f->maxClockInfo(dev, NVML_CLOCK_GRAPHICS, &u) == NVML_SUCCESS && u > 0)
        info.maxGraphicsClock = static_cast<int>(u);
    if (f->maxClockInfo && f->maxClockInfo(dev, NVML_CLOCK_MEM, &u) == NVML_SUCCESS && u > 0)
        info.maxMemoryClock = static_cast<int>(u);

    // ── PCIe (live) ──
    {
        unsigned curGen = 0, maxGen = 0, curW = 0;
        const bool haveCurGen = f->currPcieGen && f->currPcieGen(dev, &curGen) == NVML_SUCCESS && curGen > 0;
        const bool haveMaxGen = f->maxPcieGen && f->maxPcieGen(dev, &maxGen) == NVML_SUCCESS && maxGen > 0;
        const bool haveCurW   = f->currPcieWidth && f->currPcieWidth(dev, &curW) == NVML_SUCCESS && curW > 0;
        if (haveCurGen)
            info.pcieCurrentGen = QString("Gen %1").arg(curGen);
        if (haveMaxGen)
            info.pcieMaxGen = QString("Gen %1").arg(maxGen);
        if (haveCurW)
            info.pcieLinkWidth = QString("%1x").arg(curW);
        if (haveCurGen && haveCurW) {
            const double gt = gtPerSecondForGen(static_cast<int>(curGen));
            if (gt > 0)
                info.pcieLinkSpeed = QString("%1 GT/s PCIe %2x").arg(gt).arg(curW);
        }
    }

    // ── Thermal ──
    if (f->temperature && f->temperature(dev, NVML_TEMPERATURE_GPU, &u) == NVML_SUCCESS)
        info.temperature = static_cast<int>(u);
    if (f->tempThreshold) {
        if (f->tempThreshold(dev, NVML_TEMPERATURE_THRESHOLD_SLOWDOWN, &u) == NVML_SUCCESS && u > 0)
            info.tempSlowdown = static_cast<int>(u);
        if (f->tempThreshold(dev, NVML_TEMPERATURE_THRESHOLD_SHUTDOWN, &u) == NVML_SUCCESS && u > 0)
            info.tempShutdown = static_cast<int>(u);
        if (f->tempThreshold(dev, NVML_TEMPERATURE_THRESHOLD_GPU_MAX, &u) == NVML_SUCCESS && u > 0)
            info.tempGpuMax = static_cast<int>(u);
    }

    // ── Power (mW → W) ──
    if (f->powerUsage && f->powerUsage(dev, &u) == NVML_SUCCESS && u > 0)
        info.currentPowerDraw = mWToW(u);
    if (f->enforcedPowerLimit && f->enforcedPowerLimit(dev, &u) == NVML_SUCCESS && u > 0)
        info.powerLimit = mWToW(u);
    if (f->defaultPowerLimit && f->defaultPowerLimit(dev, &u) == NVML_SUCCESS && u > 0)
        info.powerDefaultLimit = mWToW(u);
    if (f->powerConstraints && f->powerConstraints(dev, &u, &u2) == NVML_SUCCESS && u2 > 0) {
        info.powerLimitMin = mWToW(u);
        info.powerLimitMax = mWToW(u2);
    }
    if (f->powerSource && f->powerSource(dev, &u) == NVML_SUCCESS) {
        info.powerSource = (u == NVML_POWER_SOURCE_AC)      ? QStringLiteral("AC")
                         : (u == NVML_POWER_SOURCE_BATTERY) ? QStringLiteral("Battery")
                                                            : QStringLiteral("Other");
    }

    // ── State / utilization (live) ──
    if (f->perfState && f->perfState(dev, &u) == NVML_SUCCESS && u <= 15)
        info.performanceState = QString("P%1").arg(u);
    if (f->utilization) {
        nvmlUtilization_t util{};
        if (f->utilization(dev, &util) == NVML_SUCCESS) {
            info.gpuUtilization = static_cast<int>(util.gpu);
            info.memoryUtilization = static_cast<int>(util.memory);
        }
    }
    if (f->encoderUtil && f->encoderUtil(dev, &u, &sampling) == NVML_SUCCESS)
        info.encoderUtilization = static_cast<int>(u);
    if (f->decoderUtil && f->decoderUtil(dev, &u, &sampling) == NVML_SUCCESS)
        info.decoderUtilization = static_cast<int>(u);
    if (f->jpgUtil && f->jpgUtil(dev, &u, &sampling) == NVML_SUCCESS)
        info.jpegUtilization = static_cast<int>(u);
    if (f->ofaUtil && f->ofaUtil(dev, &u, &sampling) == NVML_SUCCESS)
        info.ofaUtilization = static_cast<int>(u);

    // ── Throttle reasons (0 = not throttled is meaningful, so overwrite the -1) ──
    if (f->throttleReasons) {
        unsigned long long mask = 0;
        if (f->throttleReasons(dev, &mask) == NVML_SUCCESS)
            info.throttleReasons = static_cast<qint64>(mask);
    }

    // ── Fan (often NVML_ERROR_NOT_SUPPORTED on laptops → left untouched) ──
    if (f->fanSpeed && f->fanSpeed(dev, &u) == NVML_SUCCESS)
        info.fanSpeed = static_cast<int>(u);
}
