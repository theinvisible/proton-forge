#ifndef DISPLAYINFO_H
#define DISPLAYINFO_H

#include <QString>

// One active monitor/output. The portable fields (name, resolution, refresh,
// framebuffer depth, physical size, scale, manufacturer/model) come from QScreen;
// the desktop-environment-specific fields (per-channel bit depth, VRR) are filled
// by an IDisplayProbe when one is available for the running desktop. HDR is
// session-wide (from HDRChecker). Convention mirrors CPUInfo/GPUInfo: plain struct,
// all fields default-initialized, 0/empty means "unknown".
struct DisplayInfo {
    QString name;             // connector name, e.g. "eDP-1"
    QString manufacturer;     // from QScreen (EDID), may be empty
    QString model;

    int    width  = 0;        // native resolution in pixels
    int    height = 0;
    double refreshRate = 0.0; // Hz

    int    depthBpp     = 0;  // framebuffer bits per pixel (QScreen::depth())
    int    bitsPerColor = 0;  // bits per color channel (e.g. 8/10); 0 = unknown

    double physWidthMM  = 0.0;
    double physHeightMM = 0.0;
    double diagonalInch = 0.0; // derived from physical size
    double scaleFactor  = 0.0; // devicePixelRatio (e.g. 1.25)
    bool   primary      = false;

    // Variable Refresh Rate (G-Sync / FreeSync). Unknown when no probe could
    // determine it (e.g. non-KDE desktop without an implemented probe).
    enum class Vrr { Unknown, Unsupported, Supported };
    Vrr     vrr = Vrr::Unknown;
    QString vrrRaw;           // raw policy string, e.g. "incapable"/"automatic"/"always"

    bool hdrSupported = false; // session-wide (HDRChecker)
    bool hdrEnabled   = false;
};

#endif // DISPLAYINFO_H
