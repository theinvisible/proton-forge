#include "GogLauncher.h"
#include "gog/GogAuth.h"
#include "gog/GogInstallRegistry.h"
#include "gog/GogStoreService.h"
#include "utils/EnvBuilder.h"

#include <QDir>
#include <QFileInfo>

GogLauncher::GogLauncher()
{
    GogInstallRegistry::instance().load();
}

GogLauncher::~GogLauncher() = default;

bool GogLauncher::isAvailable() const
{
    // GOG games are DRM-free, so once one is installed it must stay listed and
    // launchable whether or not the user is signed in — being offline, or having
    // signed out, is no reason for a game to vanish. Only browsing the store and
    // installing need an account.
    //
    // Cheap and non-blocking, as LauncherManager requires: both halves answer
    // from memory.
    return GogAuth::instance().isLoggedIn() || !GogInstallRegistry::instance().isEmpty();
}

QList<Game> GogLauncher::discoverGames()
{
    GogInstallRegistry& registry = GogInstallRegistry::instance();
    registry.load();

    QList<Game> games;
    for (const GogInstallRegistry::Entry& entry : registry.completeEntries()) {
        // An entry whose directory is gone is not a game we can offer. Left in
        // the registry rather than pruned here, because discovery runs on a
        // worker thread and a mislaid external drive should not silently erase
        // the record of what was on it.
        if (!QDir(entry.installPath).exists()) {
            continue;
        }

        Game game;
        game.setId(entry.productId);
        game.setName(entry.title.isEmpty() ? entry.productId : entry.title);
        game.setInstallPath(entry.installPath);

        // libraryPath is the install root rather than the game directory: it is
        // what STEAM_COMPAT_LIBRARY_PATHS and the prefix parent are derived from.
        game.setLibraryPath(GogInstallRegistry::installRoot());
        game.setCompatDataPath(GogInstallRegistry::prefixPathFor(entry.productId));
        // Deliberately *not* set, so Game derives it from libraryPath as
        // <installRoot>/shadercache/<productId>/fozpipelinesv6. The layout is
        // Steam's, but the mechanism is Proton's: without
        // STEAM_COMPAT_SHADER_PATH it skips fossilize caching altogether, and a
        // GOG game deserves the same shader pre-caching a Steam one gets. There
        // is no Steam cache to seed it from — it simply fills up as the game
        // runs.

        game.setExecutablePath(entry.executablePath);
        game.setWorkingDirectory(entry.workingDirectory);
        game.setLaunchArgs(entry.launchArgs);
        game.setIsNativeLinux(entry.nativeLinux);
        game.setVersion(entry.versionName.isEmpty() ? entry.buildId : entry.versionName);
        game.setNeedsUpdate(GogInstallRegistry::hasUpdate(entry));
        game.setSizeOnDisk(entry.size);
        game.setInstallWarnings(entry.warnings);
        // Whatever was recorded, and nothing derived: an empty one means the
        // list draws its placeholder, which is the honest answer until
        // GogStoreService has looked the banner up.
        game.setImageUrl(entry.imageUrl);

        games.append(game);
    }

    return games;
}

bool GogLauncher::refreshGameState(Game& game) const
{
    const GogInstallRegistry::Entry entry = GogInstallRegistry::instance().entry(game.id());
    if (!entry.valid) {
        return false;
    }
    // Only the cached answer. Whoever last opened the store dialog wrote it;
    // asking the content system from here would put a network call on the game
    // list's worker thread.
    game.setNeedsUpdate(GogInstallRegistry::hasUpdate(entry));
    return true;
}

bool GogLauncher::applySettings(const Game& game, const DLSSSettings& settings)
{
    Q_UNUSED(game);
    Q_UNUSED(settings);
    // GOG has no per-game launch-option store to write into; ProtonForge's own
    // settings are the only place these live. traits().supportsLaunchOptionsIO
    // is false, so the UI does not offer this in the first place.
    return false;
}

QString GogLauncher::getLaunchCommand(const Game& game, const DLSSSettings& settings)
{
    Q_UNUSED(game);
    // Launcher-agnostic: the same string the user could paste anywhere.
    return EnvBuilder::buildLaunchOptions(settings);
}

IStoreService* GogLauncher::storeService()
{
    if (!m_storeService) {
        m_storeService = std::make_unique<GogStoreService>();
    }
    return m_storeService.get();
}
