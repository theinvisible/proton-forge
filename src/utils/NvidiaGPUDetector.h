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
    static int getCudaCoreCount(const QString& gpuName, const QString& computeCapability,
                                const QString& smiOutput, const QString& pciBusId, int gpuIndex);
    // Preferred source: asks the driver directly via NVML (libnvidia-ml, loaded
    // with dlopen). Returns the exact CUDA core count, or 0 if NVML is missing,
    // too old to expose nvmlDeviceGetNumGpuCores, or fails for any reason — in
    // which case the caller falls back to compute-capability / name heuristics.
    static int getCudaCoreCountFromNvml(const QString& pciBusId, int gpuIndex);
    static int coresPerSMFromComputeCapability(const QString& computeCapability);
    static int getCudaCoreCountFallback(const QString& gpuName);
};

#endif // NVIDIAGPUDETECTOR_H
