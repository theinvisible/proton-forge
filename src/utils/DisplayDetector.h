#ifndef DISPLAYDETECTOR_H
#define DISPLAYDETECTOR_H

#include <QList>
#include <memory>
#include "utils/DisplayInfo.h"

class IDisplayProbe;

// Detects all active monitors. Builds a portable baseline from QScreen and then
// applies the desktop-environment-specific IDisplayProbe (VRR, per-channel bit
// depth, authoritative mode) plus session-wide HDR status from HDRChecker.
class DisplayDetector
{
public:
    static QList<DisplayInfo> detect();

private:
    // Central desktop dispatch (mirrors GPUDetector::enrichTelemetry): returns the
    // probe for the running desktop, or nullptr when none is implemented yet.
    static std::unique_ptr<IDisplayProbe> probeForCurrentDesktop();
};

#endif // DISPLAYDETECTOR_H
