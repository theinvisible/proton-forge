#ifndef GPUINFOCACHE_H
#define GPUINFOCACHE_H

#include <QObject>
#include <QVersionNumber>
#include <QFutureWatcher>
#include <QList>
#include "core/FeatureGate.h"
#include "utils/GPUDetector.h"

// What feature gating needs to know about the machine's GPUs, in one value
// so the panel (from the cache below) and the CLI (from probeNow()) cannot
// disagree about how it is derived.
struct GpuFacts {
    QVersionNumber driver;      // NVIDIA driver; null = unknown or none
    // Newest NVIDIA generation present; Unknown for architectures FeatureGate
    // does not map.
    FeatureGate::GpuGeneration generation = FeatureGate::GpuGeneration::Unknown;
    QList<FeatureGate::GpuVendor> vendors;   // empty = unknown
    GPUDetector::HybridGpu hybrid = GPUDetector::HybridGpu::Unknown;
};

// App-wide, lazily-populated cache of the GPU facts used for feature gating.
// Detection goes through NVML (a one-time ~2.6 s init), so it is kicked off
// once asynchronously at startup; updated() fires when the facts become
// available so open widgets can refresh their warnings.
class GpuInfoCache : public QObject {
    Q_OBJECT

public:
    static GpuInfoCache& instance();

    // Start a one-shot background detection (no-op if already started).
    void refreshAsync();

    // Everything unknown until detection has run.
    const GpuFacts& facts() const { return m_facts; }

    // The facts without NVML: the driver from /proc, the GPUs from sysfs and
    // /proc/driver/nvidia/gpus, the architecture from the marketing name.
    // Milliseconds rather than seconds, which is what a CLI call can afford.
    static GpuFacts probeNow();

    // Pure: the facts from what the detectors found. NVIDIA counts as present
    // only when bound to the nvidia driver (nouveau or vfio cannot run DLSS);
    // devices from other vendors do not count at all, so a machine with none
    // of the three stays "unknown". `devices` empty = vendors unknown.
    static GpuFacts summarize(const QList<GPUInfo>& gpus,
                              const QList<PciDisplayDevice>& devices,
                              GPUDetector::HybridGpu hybrid);

signals:
    void updated();

private:
    struct Detection {
        QList<GPUInfo> gpus;
        QList<PciDisplayDevice> devices;
        GPUDetector::HybridGpu hybridGpu = GPUDetector::HybridGpu::Unknown;
    };

    explicit GpuInfoCache(QObject* parent = nullptr);
    ~GpuInfoCache() override = default;
    GpuInfoCache(const GpuInfoCache&) = delete;
    GpuInfoCache& operator=(const GpuInfoCache&) = delete;

    GpuFacts m_facts;
    bool m_started = false;
    QFutureWatcher<Detection>* m_watcher;
};

#endif // GPUINFOCACHE_H
