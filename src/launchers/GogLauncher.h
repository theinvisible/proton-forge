#ifndef GOGLAUNCHER_H
#define GOGLAUNCHER_H

#include <memory>

#include "ILauncher.h"

class GogStoreService;

// GOG as a game source.
//
// Lists exactly what ProtonForge installed itself, from GogInstallRegistry — no
// filesystem scan, so a Heroic or Lutris library sitting in the same directory
// is not adopted and no install ever has two tools competing to update it.
//
// Traits are all false, and that is the whole point: a GOG game gets no
// SteamAppId, no Steam overlay, no launch-option writeback and no ProtonDB
// lookup keyed by its product id.
class GogLauncher : public ILauncher {
public:
    GogLauncher();
    ~GogLauncher() override;

    QString name() const override { return QStringLiteral("GOG"); }
    LauncherTraits traits() const override { return {}; }

    QList<Game> discoverGames() override;
    bool applySettings(const Game& game, const DLSSSettings& settings) override;
    QString getLaunchCommand(const Game& game, const DLSSSettings& settings) override;
    bool isAvailable() const override;

    // Reads the update flag the store dialog cached into the registry. Runs on
    // GameListWidget's worker thread, so it never touches the network.
    bool refreshGameState(Game& game) const override;

    IStoreService* storeService() override;

private:
    std::unique_ptr<GogStoreService> m_storeService;
};

#endif // GOGLAUNCHER_H
