#include "LauncherManager.h"
#include "SteamLauncher.h"

LauncherManager& LauncherManager::instance()
{
    static LauncherManager instance;
    return instance;
}

LauncherManager::LauncherManager()
{
    // Register default launchers
    registerLauncher(std::make_shared<SteamLauncher>());
}

void LauncherManager::registerLauncher(std::shared_ptr<ILauncher> launcher)
{
    if (launcher && launcher->isAvailable()) {
        m_launchers.append(launcher);
    }
}

QList<std::shared_ptr<ILauncher>> LauncherManager::launchers() const
{
    return m_launchers;
}

std::shared_ptr<ILauncher> LauncherManager::launcher(const QString& name) const
{
    for (const auto& launcher : m_launchers) {
        if (launcher->name() == name) {
            return launcher;
        }
    }
    return nullptr;
}

QList<Game> LauncherManager::discoverAllGames()
{
    QList<Game> allGames;

    for (const auto& launcher : m_launchers) {
        QList<Game> games = launcher->discoverGames();
        allGames.append(games);
    }

    emit gamesDiscovered(allGames);
    return allGames;
}
