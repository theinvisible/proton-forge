#ifndef NVMLSESSION_H
#define NVMLSESSION_H

#include <QMutex>
#include "utils/GPUDetector.h"
#include "utils/IGpuTelemetrySource.h"

// NVIDIA implementation of IGpuTelemetrySource.
//
// Thin, dependency-free wrapper around NVML (libnvidia-ml). The library is loaded
// lazily via dlopen — no NVML headers, no link-time coupling; every symbol is
// resolved by name. nvmlInit_v2 is called once and the handle is kept open for
// the process lifetime (mirrors how nvidia-smi keeps NVML initialized internally).
//
// This is the single place that talks to NVML. It replaces the earlier per-call
// dlopen used only for the CUDA core count and is meant as the clean base for all
// driver-direct GPU data (and future live monitoring).
//
// Thread-safe: enrich() is called from the GpuInfoCache worker thread, the
// SystemInfoDialog refresh worker, and synchronously from the GUI thread. A single
// QMutex guards the lazy init and every query (NVML calls are microsecond-cheap).
class NvmlSession : public IGpuTelemetrySource
{
public:
    static NvmlSession& instance();

    // Overlays every field NVML can supply onto `info`, addressing the device by
    // info.pciId (falling back to info.index). Fields NVML cannot provide are left
    // untouched, so text-parsed fallback values survive. No-op when NVML is
    // unavailable (missing library, old driver, init failure).
    void enrich(GPUInfo& info) override;

    // True once libnvidia-ml loaded and nvmlInit_v2 succeeded. Triggers lazy init.
    bool available() override;

private:
    NvmlSession() = default;
    ~NvmlSession();
    NvmlSession(const NvmlSession&) = delete;
    NvmlSession& operator=(const NvmlSession&) = delete;

    // One-time dlopen + symbol resolution + nvmlInit_v2. Caller must hold m_mutex.
    // Sets and returns m_available; safe to call repeatedly (guarded by m_initTried).
    bool ensureLoadedLocked();

    struct Fns;              // resolved NVML function pointers; defined in the .cpp

    QMutex m_mutex;
    bool   m_initTried = false;
    bool   m_available = false;
    void*  m_lib = nullptr;
    Fns*   m_fns = nullptr;
};

#endif // NVMLSESSION_H
