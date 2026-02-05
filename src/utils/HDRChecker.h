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

    // Check if HDR is enabled system-wide
    static HDRStatus checkHDRStatus();

    // Get current desktop environment
    static DesktopEnvironment detectDesktopEnvironment();

    // Get user-friendly warning message
    static QString getWarningMessage(const HDRStatus& status);

private:
    static HDRStatus checkKDEHDR();
    static HDRStatus checkGnomeHDR();
    static bool isWaylandSession();
};

#endif // HDRCHECKER_H
