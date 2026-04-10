#ifndef STEAMPATHS_H
#define STEAMPATHS_H

#include <QString>

// Centralized Steam path detection. Handles both native Steam installs and
// the Flatpak `com.valvesoftware.Steam` install. Detection is cached; call
// invalidateCache() (e.g. from a Refresh action) to force a re-probe.
namespace SteamPaths {

enum class Variant {
    None,
    Native,
    Flatpak
};

// Detected Steam root, e.g. "~/.steam/steam" or "~/.var/app/com.valvesoftware.Steam/.local/share/Steam".
// Empty if no valid Steam install was found.
QString steamRoot();

Variant detectedVariant();

// Force re-detection on the next call.
void invalidateCache();

QString steamAppsPath();           // <root>/steamapps
QString compatibilityToolsPath();  // <root>/compatibilitytools.d
QString configVdfPath();           // <root>/config/config.vdf
QString steamRuntimePath();        // <root>/ubuntu12_32/steam-runtime
QString overlayLibPath(bool is64); // <root>/ubuntu12_{32,64}/gameoverlayrenderer.so
QString userDataPath();            // <root>/userdata

// Where new Proton installs should be written. Returns compatibilityToolsPath()
// when a Steam install is detected; otherwise falls back based on FLATPAK_ID
// so the "Install Proton" action still works on a clean system.
QString defaultInstallCompatPath();

} // namespace SteamPaths

#endif // STEAMPATHS_H
