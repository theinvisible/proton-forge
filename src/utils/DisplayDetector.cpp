#include "utils/DisplayDetector.h"
#include "utils/IDisplayProbe.h"
#include "utils/KdeDisplayProbe.h"
#include "utils/HDRChecker.h"
#include "utils/KScreenDoctor.h"

#include <QGuiApplication>
#include <QScreen>
#include <cmath>

QList<DisplayInfo> DisplayDetector::detect()
{
    QList<DisplayInfo> displays;

    // ── Portable baseline from QScreen (works on X11 and Wayland) ──
    const QScreen* primary = QGuiApplication::primaryScreen();
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* s : screens) {
        DisplayInfo d;
        d.name         = s->name();
        d.manufacturer = s->manufacturer();
        d.model        = s->model();

        const double dpr = s->devicePixelRatio();
        d.scaleFactor = dpr;
        // Under fractional scaling QScreen::size() is in logical pixels; multiply
        // by the device pixel ratio to recover the native resolution. On KDE this
        // is overwritten below with the authoritative kscreen mode anyway.
        const QSize logical = s->size();
        d.width  = qRound(logical.width()  * dpr);
        d.height = qRound(logical.height() * dpr);

        d.refreshRate = s->refreshRate();
        d.depthBpp    = s->depth();

        const QSizeF phys = s->physicalSize();  // millimetres
        d.physWidthMM  = phys.width();
        d.physHeightMM = phys.height();
        if (phys.width() > 0 && phys.height() > 0) {
            const double diagMM = std::sqrt(phys.width() * phys.width() +
                                            phys.height() * phys.height());
            d.diagonalInch = diagMM / 25.4;
        }

        d.primary = (s == primary);
        displays << d;
    }

    // ── Desktop-specific enrichment: VRR, per-channel bit depth, native mode ──
    //
    // The probe and HDRChecker both read the same `kscreen-doctor -o` dump on
    // KDE, so it is fetched once here and handed to both. Null means the tool
    // could not be asked; each consumer then falls back on its own terms.
    QString kscreenOutput;
    if (std::unique_ptr<IDisplayProbe> probe = probeForCurrentDesktop()) {
        if (probe->available()) {
            kscreenOutput = KScreenDoctor::run();
            probe->enrich(displays, kscreenOutput);
        }
    }

    // ── HDR is session-wide; reuse HDRChecker for every display ──
    const HDRChecker::HDRStatus hdr = HDRChecker::checkHDRStatus(kscreenOutput);
    for (DisplayInfo& d : displays) {
        d.hdrSupported = hdr.isSupported;
        d.hdrEnabled   = hdr.isEnabled;
    }

    return displays;
}

std::unique_ptr<IDisplayProbe> DisplayDetector::probeForCurrentDesktop()
{
    switch (HDRChecker::detectDesktopEnvironment()) {
        case HDRChecker::KDE:
            return std::make_unique<KdeDisplayProbe>();
        // Future: GNOME (Mutter DBus / gnome-monitor-config), wlroots (wlr-randr).
        // case HDRChecker::Gnome: return std::make_unique<GnomeDisplayProbe>();
        default:
            return nullptr;  // no probe → VRR/bit-depth stay from the QScreen baseline
    }
}
