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

    // Fallback discovery from lspci + /proc/driver/nvidia/version, used when
    // nvidia-smi can't enumerate a GPU — typically an Optimus dGPU asleep in
    // runtime D3cold suspend. Returns static-only GPUInfo entries with
    // telemetryAvailable=false, or an empty list if the nvidia driver isn't
    // loaded or no NVIDIA display device is present.
    static QList<GPUInfo> detectFromPci();

    // Map a marketing GPU name to its architecture ("Ada Lovelace", "Blackwell", …).
    static QString inferArchitecture(const QString& name);

private:
    static GPUInfo parseNvidiaSmiOutput(const QString& output, int index);
    // Extract a human-readable GPU name from an lspci device description line.
    static QString marketingNameFromLspci(const QString& desc);
    static QString extractValue(const QString& output, const QString& key);
    static int extractIntValue(const QString& output, const QString& key);
    // Heuristic core count (SM×cores/SM, then name table). Used only as a fallback
    // when the driver reported none via GPUDetector::enrichTelemetry (NVML).
    static int getCudaCoreCount(const QString& gpuName, const QString& computeCapability,
                                const QString& smiOutput);
    static int coresPerSMFromComputeCapability(const QString& computeCapability);
    static int getCudaCoreCountFallback(const QString& gpuName);
};

#endif // NVIDIAGPUDETECTOR_H
