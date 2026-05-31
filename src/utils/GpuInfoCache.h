#ifndef GPUINFOCACHE_H
#define GPUINFOCACHE_H

#include <QObject>
#include <QVersionNumber>
#include <QFutureWatcher>
#include <QList>
#include "utils/GPUDetector.h"

// App-wide, lazily-populated cache of the NVIDIA driver version, used for
// feature gating. GPU detection runs nvidia-smi (blocking), so detection is
// kicked off once asynchronously at startup; updated() fires when the cached
// value becomes available so open widgets can refresh their warnings.
class GpuInfoCache : public QObject {
    Q_OBJECT

public:
    static GpuInfoCache& instance();

    // Start a one-shot background detection (no-op if already started).
    void refreshAsync();

    // Parsed NVIDIA driver version; null QVersionNumber until detected.
    QVersionNumber nvidiaDriverVersion() const { return m_driverVersion; }
    bool driverKnown() const { return !m_driverVersion.isNull(); }

signals:
    void updated();

private:
    explicit GpuInfoCache(QObject* parent = nullptr);
    ~GpuInfoCache() override = default;
    GpuInfoCache(const GpuInfoCache&) = delete;
    GpuInfoCache& operator=(const GpuInfoCache&) = delete;

    QVersionNumber m_driverVersion;
    bool m_started = false;
    QFutureWatcher<QList<GPUInfo>>* m_watcher;
};

#endif // GPUINFOCACHE_H
