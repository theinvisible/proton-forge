#ifndef HDRCHECKER_H
#define HDRCHECKER_H

#include <QString>

class HDRChecker {
public:
    enum DesktopEnvironment {
        Unknown,
        KDE,
        Gnome,
        Other
    };

    struct HDRStatus {
        bool isSupported = false;
        bool isEnabled = false;
        QString message;
        DesktopEnvironment de = Unknown;
    };

    // Check if HDR is enabled system-wide.
    //
    // `kscreenOutput` lets a caller that already ran `kscreen-doctor -o` hand the
    // text in rather than have a second copy spawned — DisplayDetector does that,
    // since KdeDisplayProbe needs the same dump. Omit it and the KDE branch
    // fetches its own via KScreenDoctor::run().
    static HDRStatus checkHDRStatus(const QString& kscreenOutput = QString());

    // Get current desktop environment
    static DesktopEnvironment detectDesktopEnvironment();

    // Get user-friendly warning message
    static QString getWarningMessage(const HDRStatus& status);

private:
    static HDRStatus checkKDEHDR(const QString& kscreenOutput);
    static HDRStatus checkGnomeHDR();
    static bool isWaylandSession();
};

#endif // HDRCHECKER_H
