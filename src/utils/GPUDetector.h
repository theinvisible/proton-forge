#ifndef GPUDETECTOR_H
#define GPUDETECTOR_H

#include <QString>
#include <QList>
#include <QMap>

// Vendor-agnostic driver details. Populated per-GPU by the vendor-specific
// detector (currently only NVIDIA). AMD/Intel detectors should map their own
// data onto the same fields so the UI can treat them uniformly.
struct DriverInfo {
    QString version;          // e.g. "550.127.05"
    QString branch;           // e.g. "550" (major branch)
    QString releaseDate;      // build/release date as reported by the vendor
    QString moduleName;       // kernel module name, e.g. "nvidia", "amdgpu", "i915"
    QString moduleType;       // e.g. "Proprietary", "Open Kernel Module", "Mesa"
};

struct GPUInfo {
    enum Vendor { Unknown, NVIDIA, AMD, Intel };

    Vendor vendor = Unknown;
    QString name;
    QString architecture;
    QString pciId;
    int index = 0;

    QString driverVersion;
    QString vbiosVersion;
    QString cudaVersion;
    DriverInfo driverInfo;

    QString gpuPartNumber;
    QString computeCapability;
    int memoryTotalMB = 0;
    int memoryUsedMB = 0;      // live VRAM in use (NVML)
    int memoryFreeMB = 0;      // live VRAM free (NVML)
    int memoryBusWidth = 0;    // memory bus width in bits (NVML)
    int cudaCores = 0;

    QString pcieCurrentGen;
    QString pcieMaxGen;
    QString pcieLinkWidth;
    QString pcieLinkSpeed;  // Current link speed (e.g., "8.0 GT/s PCIe x16")
    int bar1TotalMB = 0;    // BAR1 size for Resizeable BAR detection
    bool resizeableBarEnabled = false;

    int currentGraphicsClock = 0;
    int currentMemoryClock = 0;
    int maxGraphicsClock = 0;
    int maxMemoryClock = 0;

    int powerLimit = 0;           // enforced power limit (W)
    int powerDefaultLimit = 0;    // default power limit (W, NVML)
    int powerLimitMin = 0;        // min settable power limit (W, NVML)
    int powerLimitMax = 0;        // max settable power limit (W, NVML)
    QString powerSource;          // "AC" / "Battery" / "Other" (NVML, laptop-relevant)
    int currentPowerDraw = 0;
    int temperature = 0;
    int tempSlowdown = 0;         // slowdown temperature threshold (°C, NVML)
    int tempShutdown = 0;         // shutdown temperature threshold (°C, NVML)
    int tempGpuMax = 0;           // GPU max operating temperature (°C, NVML)
    int fanSpeed = 0;
    QString performanceState;

    // Current clock-throttle reasons as an NVML bitmask. -1 = unknown/unavailable;
    // 0 = not throttled; >0 = OR of nvmlClocksThrottleReason* bits.
    qint64 throttleReasons = -1;

    // Utilization
    int gpuUtilization = 0;
    int memoryUtilization = 0;
    int encoderUtilization = 0;
    int decoderUtilization = 0;
    int jpegUtilization = 0;
    int ofaUtilization = 0;

    bool displayConnected = false;
    QString uuid;

    // False when the GPU was discovered but live telemetry could not be read —
    // e.g. an Optimus dGPU in runtime D3cold suspend, where nvidia-smi reports
    // "No devices were found". Static fields (name, architecture, driver) are
    // still valid; clocks, temperatures, and utilization are not.
    bool telemetryAvailable = true;

    QMap<QString, QString> extraData;
};

class GPUDetector
{
public:
    // Tri-state result of the hybrid-graphics probe. Unknown when lspci is
    // unavailable or reports no display devices — callers must stay lenient.
    enum class HybridGpu { Unknown, No, Yes };

    static QList<GPUInfo> detectAllGPUs();

    // Vendors of every PCI display-class device the kernel reports, read straight
    // out of sysfs (`class` + `vendor` per device). Pure file reads: no subprocess,
    // so unlike the lspci call this replaced it cannot time out and silently report
    // an empty machine. An empty list means sysfs was unreadable or holds no
    // display device — callers must treat that as "unknown", not "none".
    // `pciRoot` exists so tests can point at a fixture tree.
    static QList<GPUInfo::Vendor> displayDeviceVendors(
        const QString& pciRoot = QStringLiteral("/sys/bus/pci/devices"));

    // Overlays driver-direct telemetry onto `info` by routing to the vendor's
    // IGpuTelemetrySource (NVIDIA → NvmlSession today). Central vendor dispatch:
    // adding AMD/Intel means implementing the interface and adding one case here.
    // No-op for vendors without a source yet. Safe to call from worker threads.
    static void enrichTelemetry(GPUInfo& info);

    // True (Yes) when sysfs shows an NVIDIA display device alongside an
    // Intel/AMD one — the setups where PRIME render offload applies.
    // `pciRoot` is forwarded to displayDeviceVendors() for tests.
    static HybridGpu detectHybridGpu(
        const QString& pciRoot = QStringLiteral("/sys/bus/pci/devices"));
};

#endif // GPUDETECTOR_H
