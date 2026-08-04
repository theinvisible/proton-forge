#ifndef STEAMCLIENT_H
#define STEAMCLIENT_H

#include <QString>

// Runtime state of the Steam client, and starting it. Path resolution lives in
// SteamPaths; this is about the process. The native/Flatpak distinction is
// dispatched here centrally, so callers never have to know which variant is
// installed.
//
// Why callers care: the Steam Linux Runtime container does not need a running
// client, but Steamworks (SteamAPI_Init via Proton's lsteamclient), the overlay,
// cloud saves, achievements and playtime tracking all do.
namespace SteamClient {

enum class State {
    NotRunning,   // no live client process
    Starting,     // process is alive but its runtime services are not up yet
    Ready         // process is alive and the launcher service is registered
};

State state();
bool isRunning();   // state() != NotRunning
bool isReady();     // state() == Ready

// Everything state() looked at on its way to a verdict. Only the verdict
// matters to the app, but "Steam is not detected as running" is a common and
// opaque complaint, and without this the reason lives nowhere a user or a test
// can reach. Exposed through `protonforge --steam-client`.
struct Diagnostics {
    State state = State::NotRunning;
    QString variant;               // "native" | "flatpak" | "none"
    QString detail;                // which check decided, in words
    bool dbusConnected = false;
    bool dbusNameRegistered = false;
    QString pidFilePath;           // native only
    bool pidFileExists = false;
    qint64 pid = -1;               // as read from the pid file, -1 if unusable
    QString comm;                  // /proc/<pid>/comm, empty when unreadable
    bool flatpakProbeRan = false;  // flatpak only: `flatpak ps` completed
    bool flatpakAppListed = false;
};

Diagnostics diagnose();

// Starts the client detached and silently (tray only). Returns false and fills
// `error` when no launch method could be determined or the spawn failed.
bool start(QString* error = nullptr);

} // namespace SteamClient

#endif // STEAMCLIENT_H
