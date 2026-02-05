#ifndef NVIDIAGPUDETECTOR_H
#define NVIDIAGPUDETECTOR_H

#include "GPUDetector.h"
#include <QList>

class NvidiaGPUDetector
{
public:
    static QList<GPUInfo> detect();

private:
    static GPUInfo parseNvidiaSmiOutput(const QString& output, int index);
    static QString extractValue(const QString& output, const QString& key);
    static int extractIntValue(const QString& output, const QString& key);
    static int getCudaCoreCount(const QString& gpuName, const QString& architecture);
};

#endif // NVIDIAGPUDETECTOR_H
