#ifndef GPUDETECTOR_H
#define GPUDETECTOR_H

#include <QString>
#include <QList>
#include <QMap>

// Vendor-agnostic driver details. Populated per-GPU by the vendor-specific
// detector (currently only NVIDIA). AMD/Intel detectors should map their own
// data onto the same fields so the UI can treat them uniformly.
struct DriverInfo {
    QString version;          // e.g. "550.127.05"
    QString branch;           // e.g. "550" (major branch)
    QString releaseDate;      // build/release date as reported by the vendor
    QString moduleName;       // kernel module name, e.g. "nvidia", "amdgpu", "i915"
    QString moduleType;       // e.g. "Proprietary", "Open Kernel Module", "Mesa"
};

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
    DriverInfo driverInfo;

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

    // Utilization
    int gpuUtilization = 0;
    int memoryUtilization = 0;
    int encoderUtilization = 0;
    int decoderUtilization = 0;
    int jpegUtilization = 0;
    int ofaUtilization = 0;

    bool displayConnected = false;
    QString uuid;

    // False when the GPU was discovered but live telemetry could not be read —
    // e.g. an Optimus dGPU in runtime D3cold suspend, where nvidia-smi reports
    // "No devices were found". Static fields (name, architecture, driver) are
    // still valid; clocks, temperatures, and utilization are not.
    bool telemetryAvailable = true;

    QMap<QString, QString> extraData;
};

class GPUDetector
{
public:
    // Tri-state result of the hybrid-graphics probe. Unknown when lspci is
    // unavailable or reports no display devices — callers must stay lenient.
    enum class HybridGpu { Unknown, No, Yes };

    static QList<GPUInfo> detectAllGPUs();
    static bool hasNvidiaGPU();

    // True (Yes) when lspci shows an NVIDIA display device alongside an
    // Intel/AMD one — the setups where PRIME render offload applies.
    static HybridGpu detectHybridGpu();
};

#endif // GPUDETECTOR_H
