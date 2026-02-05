#include "GPUDetector.h"
#include "NvidiaGPUDetector.h"
#include <QProcess>

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

bool GPUDetector::hasNvidiaGPU()
{
    // Quick check using lspci
    QProcess process;
    process.start("lspci", QStringList());
    process.waitForFinished(1000);

    QString output = process.readAllStandardOutput();
    return output.contains("NVIDIA", Qt::CaseInsensitive) ||
           output.contains("VGA compatible controller", Qt::CaseInsensitive);
}
