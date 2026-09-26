#ifndef STEAMLAUNCHER_H
#define STEAMLAUNCHER_H

#include "ILauncher.h"
#include <QStringList>
#include <memory>

class SteamStoreService;

class SteamLauncher : public ILauncher {
public:
    SteamLauncher();
    ~SteamLauncher() override;

    QString name() const override { return "Steam"; }
    LauncherTraits traits() const override;
    QList<Game> discoverGames() override;
    bool applySettings(const Game& game, const DLSSSettings& settings) override;
    QString getLaunchCommand(const Game& game, const DLSSSettings& settings) override;
    bool isAvailable() const override;

    // ILauncher's per-game entry points. Both forward to the static overloads
    // below, which stay public because Cli and the test lab call them directly.
    QString readLaunchOptions(const Game& game) const override;
    bool refreshGameState(Game& game) const override;

    // Libraries the last discoverGames() could not read, as warnings.
    QStringList discoveryWarnings() const override;

    // Owned but not installed Steam games, via the Web API. Created lazily
    // because most sessions never open the library dialog.
    IStoreService* storeService() override;

    // Steam-specific paths
    static QString steamPath();
    static QString steamAppsPath();
    static QStringList libraryPaths();

    // What one pass over libraryfolders.vdf found.
    //
    // `unreadable` is the half that used to be thrown away: a library folder
    // Steam's own config names and whose steamapps directory this process
    // cannot see. That is not an odd corner — it is an unmounted drive, a
    // deleted folder, or (the common one, and issue #1) a Flatpak that was
    // never granted the mount the games live on. Dropping it silently makes an
    // unreachable library indistinguishable from one that was never configured,
    // so the user is told "no games" and has nothing to go on.
    struct LibraryScan {
        QStringList paths;        // steamapps directories that can be read
        QStringList unreadable;   // library roots named by the vdf that cannot
    };
    static LibraryScan scanLibraries();

    // The app id when running inside a Flatpak, empty otherwise.
    static QString flatpakAppId();

    // One human-readable warning per unreadable library, naming the path and —
    // inside a Flatpak, where the cause is almost always a missing grant — the
    // exact `flatpak override` that fixes it. flatpakId is a parameter rather
    // than an environment read so the same input can be asked both questions.
    static QStringList libraryWarnings(const QStringList& unreadable,
                                       const QString& flatpakId = flatpakAppId());

    // Re-reads ACF file for a game and updates stateFlags/buildId.
    // Returns true if the update status changed.
    static bool checkUpdateStatus(Game& game);

    // Reads the existing Steam launch options (the "%command%" string) for a
    // game from localconfig.vdf. Returns an empty string if none is set or the
    // config can't be read. Iterates all Steam users; returns the first match.
    static QString readLaunchOptions(const QString& appId);

private:
    std::unique_ptr<SteamStoreService> m_storeService;

    // Set by discoverGames(), read by discoveryWarnings(). Discovery is
    // synchronous, so both run on the thread that asked.
    QStringList m_unreadableLibraries;

    Game parseAppManifest(const QString& manifestPath, const QString& libraryPath);
    QString localConfigPath() const;
    bool writeToLocalConfig(const QString& appId, const QString& launchOptions);
};

#endif // STEAMLAUNCHER_H
