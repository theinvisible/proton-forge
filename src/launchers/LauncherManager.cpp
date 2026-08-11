#include "LauncherManager.h"
#include "SteamLauncher.h"
#include "GogLauncher.h"

LauncherManager& LauncherManager::instance()
{
    static LauncherManager instance;
    return instance;
}

LauncherManager::LauncherManager()
{
    registerBuiltinLaunchers();
    // Seed the change-detection baseline without emitting: nobody is connected
    // yet, and the first refresh must not report a change that never happened.
    m_available = currentlyAvailable();
}

void LauncherManager::registerBuiltinLaunchers()
{
    registerLauncher(std::make_shared<SteamLauncher>());
    registerLauncher(std::make_shared<GogLauncher>());
}

void LauncherManager::registerLauncher(std::shared_ptr<ILauncher> launcher)
{
    // Deliberately unconditional. Filtering on isAvailable() here evaluated it
    // exactly once per process, so a launcher that became usable later — a
    // store the user signs into, a Steam install that appears — could never be
    // picked up, not even by an explicit refresh.
    if (launcher) {
        m_launchers.append(launcher);
    }
}

QList<std::shared_ptr<ILauncher>> LauncherManager::launchers() const
{
    return m_launchers;
}

QList<std::shared_ptr<ILauncher>> LauncherManager::availableLaunchers() const
{
    QList<std::shared_ptr<ILauncher>> available;
    for (const auto& launcher : m_launchers) {
        if (launcher->isAvailable()) {
            available.append(launcher);
        }
    }
    return available;
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

QSet<QString> LauncherManager::currentlyAvailable() const
{
    QSet<QString> names;
    for (const auto& launcher : m_launchers) {
        if (launcher->isAvailable()) {
            names.insert(launcher->name());
        }
    }
    return names;
}

void LauncherManager::refreshAvailability()
{
    const QSet<QString> now = currentlyAvailable();
    if (now == m_available) {
        return;
    }
    m_available = now;
    emit availabilityChanged();
}

QList<Game> LauncherManager::discoverAllGames()
{
    QList<Game> allGames;

    for (const auto& launcher : availableLaunchers()) {
        QList<Game> games = launcher->discoverGames();

        // Identity and traits are stamped here, centrally, rather than by each
        // discoverGames(). One place decides what a game's launcher is called
        // and what it needs from the app, so no two implementations can
        // disagree and none has to remember.
        const QString name = launcher->name();
        const LauncherTraits traits = launcher->traits();
        for (Game& game : games) {
            game.setLauncher(name);
            game.setTraits(traits);
        }

        allGames.append(games);
    }

    emit gamesDiscovered(allGames);
    return allGames;
}

void LauncherManager::resetForTesting()
{
    m_launchers.clear();
    m_available.clear();
}
