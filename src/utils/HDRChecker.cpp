#include "HDRChecker.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

HDRChecker::HDRStatus HDRChecker::checkHDRStatus()
{
    HDRStatus status;
    status.de = detectDesktopEnvironment();

    // First check if we're on Wayland
    if (!isWaylandSession()) {
        status.isSupported = false;
        status.isEnabled = false;
        status.message = "HDR requires Wayland session";
        return status;
    }

    // Check based on desktop environment
    switch (status.de) {
        case KDE:
            return checkKDEHDR();
        case Gnome:
            return checkGnomeHDR();
        default:
            status.isSupported = true; // Assume supported on other DEs
            status.isEnabled = false; // But we can't check
            status.message = "Unable to detect HDR status on this desktop environment";
            return status;
    }
}

HDRChecker::DesktopEnvironment HDRChecker::detectDesktopEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    QString desktopSession = env.value("DESKTOP_SESSION").toLower();
    QString xdgCurrentDesktop = env.value("XDG_CURRENT_DESKTOP").toLower();
    QString xdgSessionDesktop = env.value("XDG_SESSION_DESKTOP").toLower();

    // Check for KDE/Plasma
    if (desktopSession.contains("plasma") ||
        xdgCurrentDesktop.contains("kde") ||
        xdgSessionDesktop.contains("plasma")) {
        return KDE;
    }

    // Check for Gnome
    if (desktopSession.contains("gnome") ||
        xdgCurrentDesktop.contains("gnome") ||
        xdgSessionDesktop.contains("gnome")) {
        return Gnome;
    }

    return Other;
}

bool HDRChecker::isWaylandSession()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString sessionType = env.value("XDG_SESSION_TYPE").toLower();
    QString waylandDisplay = env.value("WAYLAND_DISPLAY");

    return sessionType == "wayland" || !waylandDisplay.isEmpty();
}

HDRChecker::HDRStatus HDRChecker::checkKDEHDR()
{
    HDRStatus status;
    status.de = KDE;
    status.isSupported = true;

    // Method 1: Try kscreen-doctor to check display configuration
    QProcess process;
    process.start("kscreen-doctor", QStringList() << "-o");
    process.waitForFinished(3000);

    if (process.error() == QProcess::UnknownError) {
        QString output = process.readAllStandardOutput();

        // Remove ANSI color codes for easier parsing
        // ANSI codes: \033[XXm or \x1b[XXm
        output.remove(QRegularExpression("\x1b\\[[0-9;]*m"));
        output.remove(QRegularExpression("\\033\\[[0-9;]*m"));

        // Look for HDR status in output
        // Format: "HDR: enabled" or "HDR: disabled"
        QRegularExpression hdrRegex("HDR:\\s*(enabled|disabled|on|off)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = hdrRegex.match(output);

        if (match.hasMatch()) {
            QString hdrStatus = match.captured(1).toLower();
            if (hdrStatus == "enabled" || hdrStatus == "on") {
                status.isEnabled = true;
                status.message = "HDR is enabled in KDE Plasma";
                return status;
            } else {
                status.isEnabled = false;
                status.message = "HDR is not enabled in KDE Plasma system settings";
                return status;
            }
        }
    }

    // Method 2: Check KWin configuration file
    QString kwinrcPath = QProcessEnvironment::systemEnvironment().value("HOME") + "/.config/kwinrc";
    QFile kwinrc(kwinrcPath);

    if (kwinrc.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&kwinrc);
        QString content = in.readAll();
        kwinrc.close();

        // Look for HDR settings in kwinrc
        if (content.contains("HDREnabled=true", Qt::CaseInsensitive)) {
            status.isEnabled = true;
            status.message = "HDR is enabled in KDE Plasma";
            return status;
        }
    }

    // HDR not detected as enabled (default to disabled if we can't determine)
    status.isEnabled = false;
    status.message = "HDR is not enabled in KDE Plasma system settings";
    return status;
}

HDRChecker::HDRStatus HDRChecker::checkGnomeHDR()
{
    HDRStatus status;
    status.de = Gnome;
    status.isSupported = true;

    // Check if HDR is enabled in Mutter experimental features
    QProcess process;
    process.start("gsettings", QStringList() << "get" << "org.gnome.mutter" << "experimental-features");
    process.waitForFinished(3000);

    if (process.error() == QProcess::UnknownError) {
        QString output = process.readAllStandardOutput().trimmed();

        // Output format: ['feature1', 'feature2', 'hdr', ...]
        // or: @as []
        if (output.contains("'hdr'") || output.contains("\"hdr\"")) {
            status.isEnabled = true;
            status.message = "HDR is enabled in Gnome settings";
            return status;
        }
    }

    // HDR not detected as enabled
    status.isEnabled = false;
    status.message = "HDR is not enabled in Gnome settings";
    return status;
}

QString HDRChecker::getWarningMessage(const HDRStatus& status)
{
    if (!isWaylandSession()) {
        return "⚠️ HDR Warning: Wayland Required\n\n"
               "You are currently using an X11 session. HDR only works on Wayland.\n\n"
               "Please log out and select a Wayland session at the login screen.";
    }

    if (!status.isEnabled) {
        QString message = "⚠️ HDR Warning: System HDR Not Enabled\n\n";

        switch (status.de) {
            case KDE:
                message += "HDR is not enabled in your KDE Plasma system settings.\n\n"
                          "To enable HDR:\n"
                          "1. Open System Settings\n"
                          "2. Go to Display and Monitor → Display Configuration\n"
                          "3. Select your HDR-capable monitor\n"
                          "4. Enable 'Allow HDR' or 'HDR Mode'\n"
                          "5. Click Apply\n\n"
                          "Note: Your display must support HDR.";
                break;

            case Gnome:
                message += "HDR is not enabled in your Gnome settings.\n\n"
                          "To enable HDR:\n"
                          "1. Open Terminal\n"
                          "2. Run: gsettings set org.gnome.mutter experimental-features \"['hdr']\"\n"
                          "3. Log out and log back in\n\n"
                          "Note: Your display must support HDR and you need Gnome 46+.";
                break;

            default:
                message += "Unable to detect HDR configuration on your desktop environment.\n\n"
                          "Please ensure HDR is enabled in your system settings.\n"
                          "Your display must support HDR for this to work.";
                break;
        }

        message += "\n\nDo you want to enable the HDR options anyway?";
        return message;
    }

    return QString(); // No warning needed
}
