#ifndef STEAMLAUNCHER_H
#define STEAMLAUNCHER_H

#include "ILauncher.h"
#include <QStringList>

class SteamLauncher : public ILauncher {
public:
    SteamLauncher();

    QString name() const override { return "Steam"; }
    QList<Game> discoverGames() override;
    bool applySettings(const Game& game, const DLSSSettings& settings) override;
    QString getLaunchCommand(const Game& game, const DLSSSettings& settings) override;
    bool isAvailable() const override;

    // Steam-specific paths
    static QString steamPath();
    static QString steamAppsPath();
    static QStringList libraryPaths();

    // Re-reads ACF file for a game and updates stateFlags/buildId.
    // Returns true if the update status changed.
    static bool checkUpdateStatus(Game& game);

    // Reads the existing Steam launch options (the "%command%" string) for a
    // game from localconfig.vdf. Returns an empty string if none is set or the
    // config can't be read. Iterates all Steam users; returns the first match.
    static QString readLaunchOptions(const QString& appId);

private:
    Game parseAppManifest(const QString& manifestPath, const QString& libraryPath);
    QString localConfigPath() const;
    bool writeToLocalConfig(const QString& appId, const QString& launchOptions);
};

#endif // STEAMLAUNCHER_H
