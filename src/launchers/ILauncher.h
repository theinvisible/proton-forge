#ifndef ILAUNCHER_H
#define ILAUNCHER_H

#include <QString>
#include <QList>
#include "core/Game.h"
#include "core/DLSSSettings.h"

class IStoreService;

class ILauncher {
public:
    virtual ~ILauncher() = default;

    // Launcher identification.
    //
    // name() is a STABLE IDENTIFIER, not a label. It is stored in
    // Game::launcher(), which is half of Game::settingsKey(), which is a key in
    // the user's settings.json — changing it orphans every per-game profile
    // they have. Anything shown to a user belongs in displayName().
    virtual QString name() const = 0;
    virtual QString displayName() const { return name(); }

    // What this launcher's games need from the rest of the app. Stamped onto
    // every discovered Game so nothing downstream compares name() to a literal.
    virtual LauncherTraits traits() const { return {}; }

    // Game discovery
    virtual QList<Game> discoverGames() = 0;

    // Apply settings to launcher configuration (e.g., write to localconfig.vdf)
    virtual bool applySettings(const Game& game, const DLSSSettings& settings) = 0;

    // The launch options this launcher already has stored for a game, if it
    // stores any at all. Empty when it does not.
    virtual QString readLaunchOptions(const Game& game) const
    {
        Q_UNUSED(game);
        return QString();
    }

    // Re-read whatever install state the launcher keeps on disk and update the
    // game in place; returns true if anything changed. Runs on a worker thread,
    // so implementations must not touch the network or any singleton.
    virtual bool refreshGameState(Game& game) const
    {
        Q_UNUSED(game);
        return false;
    }

    // Get launch command string for clipboard/manual use
    virtual QString getLaunchCommand(const Game& game, const DLSSSettings& settings) = 0;

    // Check if launcher is available on this system
    virtual bool isAvailable() const = 0;

    // The store account behind this launcher, when it has one: browsing an
    // owned library, signing in, installing. Null for a launcher that only ever
    // reads what is already on disk.
    virtual IStoreService* storeService() { return nullptr; }
};

#endif // ILAUNCHER_H
