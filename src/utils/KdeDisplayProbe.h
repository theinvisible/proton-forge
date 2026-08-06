#ifndef KDEDISPLAYPROBE_H
#define KDEDISPLAYPROBE_H

#include "utils/IDisplayProbe.h"

// KDE Plasma display probe. Enriches DisplayInfo from `kscreen-doctor -o`, which
// is the only source on KDE that exposes VRR policy and the resolved per-channel
// bit depth alongside the current mode. The text comes in via KScreenDoctor::run(),
// shared with HDRChecker, which reads the HDR line out of the same dump.
class KdeDisplayProbe : public IDisplayProbe
{
public:
    bool available() override;
    void enrich(QList<DisplayInfo>& displays, const QString& probeOutput) override;
};

#endif // KDEDISPLAYPROBE_H
