#include "utils/GpuInfoCache.h"

#include <QtConcurrent>

#include "utils/NvidiaGPUDetector.h"

GpuInfoCache& GpuInfoCache::instance()
{
    static GpuInfoCache cache;
    return cache;
}

GpuInfoCache::GpuInfoCache(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFutureWatcher<Detection>(this))
{
    connect(m_watcher, &QFutureWatcher<Detection>::finished, this, [this]() {
        const Detection detection = m_watcher->result();
        m_facts = summarize(detection.gpus, detection.devices, detection.hybridGpu);
        emit updated();
    });
}

void GpuInfoCache::refreshAsync()
{
    if (m_started)
        return;
    m_started = true;
    m_watcher->setFuture(QtConcurrent::run([]() {
        return Detection{GPUDetector::detectAllGPUs(), GPUDetector::displayDevices(),
                         GPUDetector::detectHybridGpu()};
    }));
}

GpuFacts GpuInfoCache::probeNow()
{
    return summarize(NvidiaGPUDetector::detectFromPci(), GPUDetector::displayDevices(),
                     GPUDetector::detectHybridGpu());
}

GpuFacts GpuInfoCache::summarize(const QList<GPUInfo>& gpus,
                                 const QList<PciDisplayDevice>& devices,
                                 GPUDetector::HybridGpu hybrid)
{
    using FeatureGate::GpuGeneration;
    using FeatureGate::GpuVendor;

    GpuFacts facts;
    facts.hybrid = hybrid;

    auto add = [&facts](GpuVendor vendor) {
        if (!facts.vendors.contains(vendor))
            facts.vendors << vendor;
    };
    for (const PciDisplayDevice& device : devices) {
        switch (device.vendor) {
            case GPUInfo::NVIDIA:
                if (device.boundDriver.isEmpty() || device.boundDriver == QLatin1String("nvidia"))
                    add(GpuVendor::NVIDIA);
                break;
            case GPUInfo::AMD:     add(GpuVendor::AMD); break;
            case GPUInfo::Intel:   add(GpuVendor::Intel); break;
            case GPUInfo::Unknown: break;
        }
    }

    for (const GPUInfo& gpu : gpus) {
        if (gpu.vendor != GPUInfo::NVIDIA)
            continue;
        // The driver enumerated it, so it is there whatever sysfs said.
        add(GpuVendor::NVIDIA);

        // The newest card decides what the user can run; on a multi-GPU
        // box the game is expected to land on it.
        GpuGeneration generation = FeatureGate::generationFromArchitecture(gpu.architecture);
        // GTX 16 is Turing without tensor or RT cores: for every RTX feature
        // it is as far away as Pascal.
        if (generation == GpuGeneration::Turing &&
            gpu.name.contains(QLatin1String("GTX"), Qt::CaseInsensitive)) {
            generation = GpuGeneration::PreTuring;
        }
        if (generation > facts.generation)
            facts.generation = generation;

        if (!facts.driver.isNull())
            continue;
        // Prefer the richer driverInfo.version, fall back to the plain
        // version the detector reported.
        const QString version = !gpu.driverInfo.version.isEmpty()
                                     ? gpu.driverInfo.version
                                     : gpu.driverVersion;
        if (!version.isEmpty())
            facts.driver = QVersionNumber::fromString(version);
    }
    return facts;
}
