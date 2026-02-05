#ifndef GPUDETECTOR_H
#define GPUDETECTOR_H

#include <QString>
#include <QList>
#include <QMap>

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

    QString gpuPartNumber;
    QString computeCapability;
    int memoryTotalMB = 0;
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

    int powerLimit = 0;
    int currentPowerDraw = 0;
    int temperature = 0;
    int fanSpeed = 0;
    QString performanceState;

    bool displayConnected = false;
    QString uuid;

    QMap<QString, QString> extraData;
};

class GPUDetector
{
public:
    static QList<GPUInfo> detectAllGPUs();
    static bool hasNvidiaGPU();
};

#endif // GPUDETECTOR_H
