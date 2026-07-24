#ifndef KDEDISPLAYPROBE_H
#define KDEDISPLAYPROBE_H

#include "utils/IDisplayProbe.h"

// KDE Plasma display probe. Enriches DisplayInfo via `kscreen-doctor -o`, which is
// the only source on KDE that exposes VRR policy and the resolved per-channel bit
// depth alongside the current mode. Same QProcess + ANSI-strip approach as
// HDRChecker's KDE HDR probe.
class KdeDisplayProbe : public IDisplayProbe
{
public:
    bool available() override;
    void enrich(QList<DisplayInfo>& displays) override;
};

#endif // KDEDISPLAYPROBE_H
