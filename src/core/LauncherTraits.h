#ifndef LAUNCHERTRAITS_H
#define LAUNCHERTRAITS_H

// What a launcher's games need from the rest of the app, expressed as data
// instead of as a string comparison against the launcher's name.
//
// Every field here replaced a `game.launcher() == "Steam"` somewhere in the
// runner, the UI or the CLI. Carrying them on the Game — stamped once, centrally,
// by LauncherManager at discovery — is what lets GameRunner::resolveLaunch
// depend on nothing but its two arguments, and therefore be testable without a
// launcher, a singleton, or a Steam installation.
//
// Lives in core rather than next to ILauncher so the dependency runs one way:
// launchers already include core/Game.h, and Game carries this.
struct LauncherTraits {
    // SteamAppId/SteamGameId, STEAM_RUNTIME, and the gameoverlayrenderer.so
    // LD_PRELOAD pair. A DRM-free game from another store wants none of it —
    // the overlay in particular must not be injected just because the user's
    // global default has it switched on.
    bool usesSteamEnv            = false;

    // The launch waits for the store's client to be up before starting.
    bool requiresClientRunning   = false;

    // readLaunchOptions() and applySettings() actually do something. When false
    // the import/write buttons are hidden rather than shown and then refused.
    bool supportsLaunchOptionsIO = false;

    // The launcher keeps install state on disk that is worth re-reading
    // periodically to notice a pending update.
    bool providesUpdateState     = false;

    // Game::id() is a Steam appid, so it may be used to key ProtonDB lookups
    // and Steam's own CompatToolMapping. A product id from another store that
    // happens to be numeric must never be sent to either.
    bool idIsSteamAppId          = false;
};

#endif // LAUNCHERTRAITS_H
