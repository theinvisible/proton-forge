#ifndef IDISPLAYPROBE_H
#define IDISPLAYPROBE_H

#include <QList>
#include "utils/DisplayInfo.h"

// Contract for a desktop-environment-specific display probe. QScreen gives a
// portable baseline (resolution, refresh, framebuffer depth, physical size); a
// probe enriches the DE-specific bits that have no portable API — Variable Refresh
// Rate (G-Sync/FreeSync) capability, the per-channel color bit depth, and the
// authoritative current mode.
//
// This mirrors the IGpuTelemetrySource + GPUDetector::enrichTelemetry pattern: add
// support for a new desktop (GNOME via Mutter DBus, wlroots via wlr-randr, …) by
// implementing this interface and adding one line to
// DisplayDetector::probeForCurrentDesktop(). Callers never hardcode a desktop.
class IDisplayProbe
{
public:
    virtual ~IDisplayProbe() = default;

    // True when this desktop's query tool is present and usable.
    virtual bool available() = 0;

    // Overlay DE-specific fields (vrr, bitsPerColor, native mode) onto the entries
    // in `displays`, matching by connector name. Fields it cannot determine are
    // left untouched so the QScreen baseline survives. Must tolerate being called
    // even when available() is false (should then be a no-op).
    //
    // `probeOutput` is the desktop tool's already-captured output — DisplayDetector
    // fetches it once and shares it with the other consumers instead of every
    // parser spawning its own copy. A null string means "could not ask": treat it
    // as no information rather than as a negative answer.
    virtual void enrich(QList<DisplayInfo>& displays, const QString& probeOutput) = 0;
};

#endif // IDISPLAYPROBE_H
