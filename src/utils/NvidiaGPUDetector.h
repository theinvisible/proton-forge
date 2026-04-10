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

private:
    static GPUInfo parseNvidiaSmiOutput(const QString& output, int index);
    static QString extractValue(const QString& output, const QString& key);
    static int extractIntValue(const QString& output, const QString& key);
    static int getCudaCoreCount(const QString& gpuName, const QString& computeCapability, const QString& smiOutput);
    static int coresPerSMFromComputeCapability(const QString& computeCapability);
    static int getCudaCoreCountFallback(const QString& gpuName);
};

#endif // NVIDIAGPUDETECTOR_H
