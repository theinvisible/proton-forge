#ifndef NVIDIAGPUDETECTOR_H
#define NVIDIAGPUDETECTOR_H

#include "GPUDetector.h"
#include <QList>

class NvidiaGPUDetector
{
public:
    static QList<GPUInfo> detect();

    // Detects NVIDIA driver metadata (version, build date, module type) by
    // reading /proc/driver/nvidia/version and querying modinfo. The result is
    // the same for every NVIDIA GPU in the system, so it's cached per call.
    static DriverInfo detectDriverInfo(const QString& smiDriverVersion = QString());

    // Fallback discovery from sysfs + /proc/driver/nvidia, used when the driver
    // can't enumerate a GPU — typically an Optimus dGPU asleep in runtime D3cold
    // suspend. Returns static-only GPUInfo entries with telemetryAvailable=false,
    // or an empty list if the nvidia driver isn't loaded or no NVIDIA display
    // device is present. The roots are parameters so tests can use a fixture tree.
    static QList<GPUInfo> detectFromPci(
        const QString& pciRoot  = QStringLiteral("/sys/bus/pci/devices"),
        const QString& procRoot = QStringLiteral("/proc"));

    // Map a marketing GPU name to its architecture ("Ada Lovelace", "Blackwell", …).
    static QString inferArchitecture(const QString& name);

private:
    // Overlay name/UUID/VBIOS from the driver's own per-GPU record at
    // /proc/driver/nvidia/gpus/<pciId>/information.
    static void readProcGpuInformation(GPUInfo& info, const QString& procRoot);

    // Name-table core count, used only when NVML reported none — which happens
    // on the D3cold fallback path and on drivers without nvmlDeviceGetNumGpuCores.
    static int getCudaCoreCountFallback(const QString& gpuName);
};

#endif // NVIDIAGPUDETECTOR_H
