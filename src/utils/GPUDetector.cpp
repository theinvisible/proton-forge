#include "GPUDetector.h"
#include "NvidiaGPUDetector.h"
#include "utils/NvmlSession.h"
#include "utils/IGpuTelemetrySource.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
// Vendor → telemetry source. Add AMD/Intel by implementing IGpuTelemetrySource
// and returning its singleton here; every caller routes through enrichTelemetry().
IGpuTelemetrySource* telemetrySourceFor(GPUInfo::Vendor vendor)
{
    switch (vendor) {
        case GPUInfo::NVIDIA: return &NvmlSession::instance();
        // case GPUInfo::AMD:   return &AmdSmiSession::instance();
        // case GPUInfo::Intel: return &IntelGpuTelemetry::instance();
        default: return nullptr;
    }
}

// PCI vendor ID → GPUInfo::Vendor. The other half of the vendor dispatch above:
// a new vendor gets its ID here and its telemetry source there.
GPUInfo::Vendor vendorFromPciId(quint16 id)
{
    switch (id) {
        case 0x10de: return GPUInfo::NVIDIA;
        case 0x1002: return GPUInfo::AMD;    // ATI / AMD graphics
        case 0x8086: return GPUInfo::Intel;
        default:     return GPUInfo::Unknown;
    }
}

// Reads a sysfs attribute holding a single "0x…" hex word. -1 when the file is
// missing or does not parse.
qint64 readSysfsHex(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;

    bool ok = false;
    const qint64 value = file.readLine().trimmed().toLongLong(&ok, 0);  // base 0 → honours 0x
    return ok ? value : -1;
}
} // namespace

void GPUDetector::enrichTelemetry(GPUInfo& info)
{
    if (IGpuTelemetrySource* source = telemetrySourceFor(info.vendor))
        source->enrich(info);
}

QList<GPUInfo> GPUDetector::detectAllGPUs()
{
    QList<GPUInfo> allGPUs;

    // Detect NVIDIA GPUs
    QList<GPUInfo> nvidiaGPUs = NvidiaGPUDetector::detect();
    allGPUs.append(nvidiaGPUs);

    // Future: Add AMD and Intel detection here
    // allGPUs.append(AmdGPUDetector::detect());
    // allGPUs.append(IntelGPUDetector::detect());

    return allGPUs;
}

QList<PciDisplayDevice> GPUDetector::displayDevices(const QString& pciRoot)
{
    QList<PciDisplayDevice> devices;

    const QDir root(pciRoot);
    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        // PCI class register. Base class 0x03 is "display controller" and covers
        // VGA compatible (0x0300), 3D controller (0x0302 — how Optimus dGPUs
        // announce themselves) and other display controllers (0x0380).
        const qint64 classCode = readSysfsHex(root.filePath(entry + "/class"));
        if (classCode < 0 || (classCode >> 16) != 0x03)
            continue;

        const qint64 vendorId = readSysfsHex(root.filePath(entry + "/vendor"));
        if (vendorId < 0)
            continue;

        PciDisplayDevice device;
        device.address = entry;
        device.vendor  = vendorFromPciId(static_cast<quint16>(vendorId));

        const qint64 deviceId = readSysfsHex(root.filePath(entry + "/device"));
        if (deviceId >= 0)
            device.deviceId = static_cast<quint16>(deviceId);

        // The `driver` symlink points at the bound driver's directory; its base
        // name is the driver. Absent when nothing has claimed the device.
        const QFileInfo driverLink(root.filePath(entry + "/driver"));
        if (driverLink.exists())
            device.boundDriver = driverLink.symLinkTarget().isEmpty()
                                     ? driverLink.fileName()
                                     : QFileInfo(driverLink.symLinkTarget()).fileName();

        devices.append(device);
    }

    return devices;
}

GPUDetector::HybridGpu GPUDetector::detectHybridGpu(const QString& pciRoot)
{
    const QList<PciDisplayDevice> devices = displayDevices(pciRoot);
    if (devices.isEmpty()) {
        // Nothing readable — say so rather than claiming a non-hybrid system.
        return HybridGpu::Unknown;
    }

    bool hasNvidia = false;
    bool hasOtherVendor = false;
    for (const PciDisplayDevice& device : devices) {
        if (device.vendor == GPUInfo::NVIDIA) {
            hasNvidia = true;
        } else if (device.vendor != GPUInfo::Unknown) {
            hasOtherVendor = true;
        }
    }

    return (hasNvidia && hasOtherVendor) ? HybridGpu::Yes : HybridGpu::No;
}
