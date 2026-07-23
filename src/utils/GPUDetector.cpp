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

GPUDetector::HybridGpu GPUDetector::detectHybridGpu()
{
    QProcess process;
    process.start("lspci", QStringList());
    if (!process.waitForFinished(2000)) {
        return HybridGpu::Unknown;
    }

    const QString output = process.readAllStandardOutput();
    bool hasNvidia = false;
    bool hasOtherVendor = false;
    bool anyDisplayDevice = false;

    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        // Display-class devices only; dGPUs on Optimus laptops typically show
        // up as "3D controller" rather than "VGA compatible controller".
        const bool isDisplay =
            line.contains("VGA compatible controller", Qt::CaseInsensitive) ||
            line.contains("3D controller", Qt::CaseInsensitive) ||
            line.contains("Display controller", Qt::CaseInsensitive);
        if (!isDisplay) {
            continue;
        }
        anyDisplayDevice = true;
        if (line.contains("NVIDIA", Qt::CaseInsensitive)) {
            hasNvidia = true;
        } else if (line.contains("Intel", Qt::CaseInsensitive) ||
                   line.contains("AMD", Qt::CaseInsensitive) ||
                   line.contains("ATI", Qt::CaseInsensitive) ||
                   line.contains("Advanced Micro Devices", Qt::CaseInsensitive)) {
            hasOtherVendor = true;
        }
    }

    if (!anyDisplayDevice) {
        return HybridGpu::Unknown;
    }
    return (hasNvidia && hasOtherVendor) ? HybridGpu::Yes : HybridGpu::No;
}
