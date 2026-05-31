#include "utils/GpuInfoCache.h"

#include <QtConcurrent>

GpuInfoCache& GpuInfoCache::instance()
{
    static GpuInfoCache cache;
    return cache;
}

GpuInfoCache::GpuInfoCache(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFutureWatcher<QList<GPUInfo>>(this))
{
    connect(m_watcher, &QFutureWatcher<QList<GPUInfo>>::finished, this, [this]() {
        const QList<GPUInfo> gpus = m_watcher->result();
        for (const GPUInfo& gpu : gpus) {
            if (gpu.vendor != GPUInfo::NVIDIA)
                continue;
            // Prefer the richer driverInfo.version, fall back to the plain
            // version reported by nvidia-smi.
            const QString version = !gpu.driverInfo.version.isEmpty()
                                         ? gpu.driverInfo.version
                                         : gpu.driverVersion;
            if (!version.isEmpty()) {
                m_driverVersion = QVersionNumber::fromString(version);
                break;
            }
        }
        emit updated();
    });
}

void GpuInfoCache::refreshAsync()
{
    if (m_started)
        return;
    m_started = true;
    m_watcher->setFuture(QtConcurrent::run([]() {
        return GPUDetector::detectAllGPUs();
    }));
}
