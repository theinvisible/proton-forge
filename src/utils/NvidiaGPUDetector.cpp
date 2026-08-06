#include "NvidiaGPUDetector.h"
#include "utils/NvmlSession.h"
#include <QRegularExpression>
#include <QFile>
#include <QDateTime>
#include <QLocale>
#include <QDebug>

namespace {
// Trimmed contents of a small sysfs/proc attribute, empty when unreadable.
QString readTrimmedFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll()).trimmed();
}
} // namespace

QList<GPUInfo> NvidiaGPUDetector::detect()
{
    // Straight from the driver via NVML. This used to run `nvidia-smi -q` and
    // parse its text — a subprocess costing 2.6 s on a cold start, whose parsed
    // values were then almost entirely overwritten by these same NVML calls.
    QList<GPUInfo> gpus = NvmlSession::instance().enumerate();

    if (!gpus.isEmpty()) {
        // Driver metadata is system-wide; detect once and share across all GPUs.
        const DriverInfo sharedDriverInfo = detectDriverInfo(gpus.first().driverVersion);
        for (GPUInfo& info : gpus) {
            info.driverInfo = sharedDriverInfo;
            if (info.architecture.isEmpty())
                info.architecture = inferArchitecture(info.name);
            if (info.cudaCores == 0)
                info.cudaCores = getCudaCoreCountFallback(info.name);
        }
        return gpus;
    }

    // Fallback: NVML enumerated nothing. If the nvidia driver is loaded and sysfs
    // shows an NVIDIA display device (e.g. an Optimus dGPU asleep in D3cold),
    // report it from static sources so the UI still shows the card instead of
    // claiming there's no compatible GPU.
    return detectFromPci();
}

QList<GPUInfo> NvidiaGPUDetector::detectFromPci(const QString& pciRoot, const QString& procRoot)
{
    QList<GPUInfo> gpus;

    // The nvidia kernel module must be loaded for DLSS/NVAPI to be usable at
    // all; /proc/driver/nvidia/version stays readable regardless of GPU power
    // state, so use its presence as the gate.
    if (!QFile::exists(procRoot + "/driver/nvidia/version"))
        return gpus;

    // Driver metadata is system-wide; read once.
    const DriverInfo driverInfo = detectDriverInfo();

    int index = 0;
    for (const PciDisplayDevice& device : GPUDetector::displayDevices(pciRoot)) {
        if (device.vendor != GPUInfo::NVIDIA)
            continue;
        // Skip an NVIDIA GPU that isn't actually bound to the nvidia driver
        // (e.g. nouveau) — DLSS/NVAPI wouldn't work there anyway.
        if (!device.boundDriver.isEmpty() && device.boundDriver != "nvidia")
            continue;

        GPUInfo info;
        info.vendor = GPUInfo::NVIDIA;
        info.index = index++;
        info.pciId = device.address;
        info.driverInfo = driverInfo;
        info.driverVersion = driverInfo.version;
        info.telemetryAvailable = false;  // GPU asleep / NVML couldn't enumerate it

        // The driver's own per-GPU record. Better than anything derivable from a
        // PCI id table: it carries NVIDIA's marketing name, the UUID and the
        // VBIOS version, none of which the old lspci parse could fill in.
        readProcGpuInformation(info, procRoot);

        if (info.name.isEmpty())
            info.name = QString("NVIDIA GPU %1").arg(device.address);
        if (info.architecture.isEmpty())
            info.architecture = inferArchitecture(info.name);

        gpus.append(info);
    }

    return gpus;
}

void NvidiaGPUDetector::readProcGpuInformation(GPUInfo& info, const QString& procRoot)
{
    // /proc/driver/nvidia/gpus/<domain:bus:slot.func>/information, e.g.
    //   Model:       NVIDIA GeForce RTX 5070 Laptop GPU
    //   GPU UUID:    GPU-0c31c1e7-f65e-f49b-a577-ec96b893c09d
    //   Video BIOS:  98.06.34.00.eb
    QFile file(QString("%1/driver/nvidia/gpus/%2/information").arg(procRoot, info.pciId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const QString content = QString::fromUtf8(file.readAll());
    for (const QString& line : content.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString val = line.mid(colon + 1).trimmed();
        if (val.isEmpty())
            continue;

        if      (key == "Model")      info.name = val;
        else if (key == "GPU UUID")   info.uuid = val;
        else if (key == "Video BIOS") info.vbiosVersion = val;
    }
}

QString NvidiaGPUDetector::inferArchitecture(const QString& name)
{
    if (name.contains("RTX 50")) return "Blackwell";
    if (name.contains("RTX 40")) return "Ada Lovelace";
    if (name.contains("RTX 30")) return "Ampere";
    if (name.contains("RTX 20")) return "Turing";
    if (name.contains("GTX 16")) return "Turing";
    if (name.contains("GTX 10")) return "Pascal";
    return QString();
}

DriverInfo NvidiaGPUDetector::detectDriverInfo(const QString& smiDriverVersion)
{
    DriverInfo info;
    info.moduleName = "nvidia";

    // Primary source: /proc/driver/nvidia/version
    // Typical line (proprietary):
    //   NVRM version: NVIDIA UNIX x86_64 Kernel Module  550.127.05  Tue Oct  8 16:30:26 UTC 2024
    // Typical line (open):
    //   NVRM version: NVIDIA UNIX Open Kernel Module for x86_64  550.127.05  Tue Oct  8 16:30:26 UTC 2024
    QFile versionFile("/proc/driver/nvidia/version");
    QString procContent;
    if (versionFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        procContent = QString::fromUtf8(versionFile.readAll());
        versionFile.close();
    }

    if (!procContent.isEmpty()) {
        // Extract the version from the NVRM line. Accept any text between
        // "NVRM version:" and the version number to stay robust across driver
        // variants (proprietary / open / arch suffixes).
        QRegularExpression nvrmRe(
            "NVRM version:.*?(\\d+(?:\\.\\d+)+)",
            QRegularExpression::MultilineOption);
        const QRegularExpressionMatch m = nvrmRe.match(procContent);
        if (m.hasMatch())
            info.version = m.captured(1);

        // The build date sits at the end of that same line, but not always
        // straight after the version — newer drivers insert build metadata, e.g.
        //   … 610.43.02  Release Build  (dvs-builder@…)  Tue May 19 11:24:27 UTC 2026
        // Anchoring on the timestamp instead of "everything after the version"
        // stops that metadata from being reported as the release date.
        static const QRegularExpression dateRe(
            "([A-Z][a-z]{2}\\s+[A-Z][a-z]{2}\\s+\\d{1,2}\\s+\\d{2}:\\d{2}:\\d{2}\\s+\\S+\\s+\\d{4})");
        const QRegularExpressionMatch dm = dateRe.match(procContent);
        if (dm.hasMatch()) {
            const QString rawDate = dm.captured(1).trimmed();

            // The double-space padding before single-digit days trips
            // QDateTime::fromString, so normalize whitespace first.
            QString normalizedDate = rawDate;
            normalizedDate.replace(QRegularExpression("\\s+"), " ");
            QLocale cLocale(QLocale::C);
            QDateTime dt = cLocale.toDateTime(normalizedDate, "ddd MMM d HH:mm:ss t yyyy");
            if (dt.isValid()) {
                info.releaseDate = dt.toString("yyyy-MM-dd");
            } else {
                info.releaseDate = rawDate; // fall back to raw string
            }
        }

        if (procContent.contains("Open Kernel Module", Qt::CaseInsensitive)) {
            info.moduleType = "Open Kernel Module";
        } else if (procContent.contains("Kernel Module", Qt::CaseInsensitive)) {
            info.moduleType = "Proprietary";
        }
    }

    // Fallbacks when /proc was unreadable (sandboxed environments, flatpak
    // without --filesystem=host): the module's own sysfs entry, then whatever
    // the caller already learned from the driver.
    if (info.version.isEmpty())
        info.version = readTrimmedFile("/sys/module/nvidia/version");
    if (info.version.isEmpty() && !smiDriverVersion.isEmpty()) {
        info.version = smiDriverVersion;
    }

    // Branch is the leading version component (driver "major") — useful to
    // distinguish production vs. new-feature branches at a glance.
    if (!info.version.isEmpty()) {
        info.branch = info.version.section('.', 0, 0);
    }

    // Cross-check the module type if /proc didn't tell us, via the kernel's own
    // taint record for the module rather than by shelling out to modinfo. The
    // open module is dual-licensed MIT/GPL and only taints 'O' (out-of-tree);
    // the proprietary one is not GPL-compatible and additionally taints 'P'.
    if (info.moduleType.isEmpty()) {
        const QString taint = readTrimmedFile("/sys/module/nvidia/taint");
        if (!taint.isEmpty())
            info.moduleType = taint.contains('P') ? "Proprietary" : "Open Kernel Module";
    }

    return info;
}

int NvidiaGPUDetector::getCudaCoreCountFallback(const QString& gpuName)
{
    const QString name = gpuName.toUpper();

    // --- Laptop / Mobile GPUs ---
    // Mobile parts use cut-down dies and have far fewer cores than their desktop
    // namesakes (e.g. RTX 5070 Laptop = 4608 vs. desktop 6144). NVIDIA marks them
    // with "Laptop GPU" in the product name, so they must be matched here BEFORE
    // the desktop tables below, whose contains() checks would otherwise catch them.
    // "TI" variants are checked before the plain model for the same reason.
    if (name.contains("LAPTOP")) {
        // Blackwell (RTX 50 Series Laptop)
        if (name.contains("RTX 5090"))    return 10496;
        if (name.contains("RTX 5080"))    return  7680;
        if (name.contains("RTX 5070 TI")) return  5888;
        if (name.contains("RTX 5070"))    return  4608;
        if (name.contains("RTX 5060"))    return  3328;
        if (name.contains("RTX 5050"))    return  2560;

        // Ada Lovelace (RTX 40 Series Laptop)
        if (name.contains("RTX 4090"))    return  9728;
        if (name.contains("RTX 4080"))    return  7424;
        if (name.contains("RTX 4070"))    return  4608;
        if (name.contains("RTX 4060"))    return  3072;
        if (name.contains("RTX 4050"))    return  2560;

        // Ampere (RTX 30 Series Laptop)
        if (name.contains("RTX 3080 TI")) return  7424;
        if (name.contains("RTX 3080"))    return  6144;
        if (name.contains("RTX 3070 TI")) return  5888;
        if (name.contains("RTX 3070"))    return  5120;
        if (name.contains("RTX 3060"))    return  3840;
        if (name.contains("RTX 3050 TI")) return  2560;
        if (name.contains("RTX 3050"))    return  2048;
        // Unknown laptop model: fall through to the desktop tables below.
    }

    // Blackwell (RTX 50 Series)  –  SM count × 128 cores/SM
    if (name.contains("RTX 5090"))    return 21760;  // GB202: 170 SMs
    if (name.contains("RTX 5080"))    return 10752;  // GB203: 84 SMs
    if (name.contains("RTX 5070 TI")) return  8960;  // GB203: 70 SMs
    if (name.contains("RTX 5070"))    return  6144;  // GB205: 48 SMs
    if (name.contains("RTX 5060 TI")) return  4608;  // GB206: 36 SMs
    if (name.contains("RTX 5060"))    return  3840;  // GB206: 30 SMs

    // Ada Lovelace (RTX 40 Series)
    if (name.contains("RTX 4090")) return 16384;
    if (name.contains("RTX 4080 SUPER")) return 10240;
    if (name.contains("RTX 4080")) return 9728;
    if (name.contains("RTX 4070 TI SUPER")) return 8448;
    if (name.contains("RTX 4070 TI")) return 7680;
    if (name.contains("RTX 4070 SUPER")) return 7168;
    if (name.contains("RTX 4070")) return 5888;
    if (name.contains("RTX 4060 TI")) return 4352;
    if (name.contains("RTX 4060")) return 3072;
    if (name.contains("RTX 4050")) return 2560;

    // Ampere (RTX 30 Series)
    if (name.contains("RTX 3090 TI")) return 10752;
    if (name.contains("RTX 3090")) return 10496;
    if (name.contains("RTX 3080 TI")) return 10240;
    if (name.contains("RTX 3080 12GB")) return 8960;
    if (name.contains("RTX 3080")) return 8704;
    if (name.contains("RTX 3070 TI")) return 6144;
    if (name.contains("RTX 3070")) return 5888;
    if (name.contains("RTX 3060 TI")) return 4864;
    if (name.contains("RTX 3060 12GB")) return 3584;
    if (name.contains("RTX 3060")) return 3584;
    if (name.contains("RTX 3050")) return 2560;

    // Turing (RTX 20 Series & GTX 16 Series)
    if (name.contains("RTX 2080 TI")) return 4352;
    if (name.contains("RTX 2080 SUPER")) return 3072;
    if (name.contains("RTX 2080")) return 2944;
    if (name.contains("RTX 2070 SUPER")) return 2560;
    if (name.contains("RTX 2070")) return 2304;
    if (name.contains("RTX 2060 SUPER")) return 2176;
    if (name.contains("RTX 2060")) return 1920;
    if (name.contains("GTX 1660 TI")) return 1536;
    if (name.contains("GTX 1660 SUPER")) return 1408;
    if (name.contains("GTX 1660")) return 1408;
    if (name.contains("GTX 1650 SUPER")) return 1280;
    if (name.contains("GTX 1650")) return 896;

    // Pascal (GTX 10 Series)
    if (name.contains("GTX 1080 TI")) return 3584;
    if (name.contains("GTX 1080")) return 2560;
    if (name.contains("GTX 1070 TI")) return 2432;
    if (name.contains("GTX 1070")) return 1920;
    if (name.contains("GTX 1060 6GB")) return 1280;
    if (name.contains("GTX 1060 3GB")) return 1152;
    if (name.contains("GTX 1060")) return 1280;
    if (name.contains("GTX 1050 TI")) return 768;
    if (name.contains("GTX 1050")) return 640;

    // Professional / Workstation
    if (name.contains("RTX 6000 ADA")) return 18176;
    if (name.contains("RTX 5880 ADA")) return 14080;
    if (name.contains("RTX 5000 ADA")) return 12800;
    if (name.contains("RTX 4500 ADA")) return 7680;
    if (name.contains("RTX 4000 ADA")) return 6144;
    if (name.contains("A100")) return 6912;
    if (name.contains("A40")) return 10752;
    if (name.contains("A30")) return 3584;
    if (name.contains("A10")) return 9216;
    if (name.contains("A6000")) return 10752;
    if (name.contains("A5500")) return 10240;
    if (name.contains("A5000")) return 8192;
    if (name.contains("A4500")) return 5888;
    if (name.contains("A4000")) return 6144;
    if (name.contains("A2000")) return 3328;

    // Titan Series
    if (name.contains("TITAN RTX")) return 4608;
    if (name.contains("TITAN V")) return 5120;
    if (name.contains("TITAN XP")) return 3840;
    if (name.contains("TITAN X")) return 3584;

    return 0;
}
