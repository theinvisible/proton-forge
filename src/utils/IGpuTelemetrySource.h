#ifndef IGPUTELEMETRYSOURCE_H
#define IGPUTELEMETRYSOURCE_H

#include "utils/GPUDetector.h"

// Contract for a vendor-specific "telemetry enricher". A vendor detector first
// fills a GPUInfo with whatever static data it can (name, PCI id, …); the matching
// telemetry source then overlays exact and live values read straight from the
// driver (core count, clocks, power, temperatures, utilization, …).
//
// This mirrors the ILauncher/LauncherManager plugin pattern used elsewhere: to add
// a new vendor (AMD via amd-smi/sysfs, Intel via Level Zero/sysfs, …), implement
// this interface and register the singleton in GPUDetector::enrichTelemetry().
// Callers never hardcode a vendor — they route through that dispatcher.
class IGpuTelemetrySource
{
public:
    virtual ~IGpuTelemetrySource() = default;

    // True when the underlying driver library is present and initialized.
    virtual bool available() = 0;

    // Overlay driver-direct fields onto `info`. Fields the source cannot read must
    // be left untouched so the detector's fallback values survive. Must be safe to
    // call from background worker threads.
    virtual void enrich(GPUInfo& info) = 0;
};

#endif // IGPUTELEMETRYSOURCE_H
