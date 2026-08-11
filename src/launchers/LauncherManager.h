#ifndef LAUNCHERMANAGER_H
#define LAUNCHERMANAGER_H

#include <QObject>
#include <QList>
#include <QSet>
#include <QString>
#include <memory>
#include "ILauncher.h"
#include "core/Game.h"

// The registry of game sources.
//
// Registration happens once, at construction, and the registry is immutable
// afterwards — which is what makes it safe to snapshot into a worker thread.
// Availability is the part that moves: a store launcher becomes usable when the
// user signs in and stops being usable when they sign out, long after the
// singleton was built. So availability is asked for at call time rather than
// baked in at registration, and refreshAvailability() tells the UI when it moved.
class LauncherManager : public QObject {
    Q_OBJECT

public:
    static LauncherManager& instance();

    // Construction-time only. Kept public for resetForTesting().
    void registerLauncher(std::shared_ptr<ILauncher> launcher);

    // Every registered launcher, available or not. launcher(name) resolves from
    // here on purpose: a game already on screen must keep resolving to the
    // launcher that produced it even if that launcher just went away, or the
    // UI reports "launcher not found" instead of the real problem.
    QList<std::shared_ptr<ILauncher>> launchers() const;
    std::shared_ptr<ILauncher> launcher(const QString& name) const;

    // Only those that can answer right now. Discovery uses this.
    QList<std::shared_ptr<ILauncher>> availableLaunchers() const;

    QList<Game> discoverAllGames();

    // Re-evaluate ILauncher::isAvailable() across the registry and emit
    // availabilityChanged() only if the set actually moved. Called on refresh
    // and whenever a store's sign-in state changes.
    //
    // Note for implementors: isAvailable() runs on every refresh, so it must be
    // cheap and must not block — a cached bool or a stat, never a network call.
    void refreshAvailability();

    // Test seam. The singleton hardcodes its own contents, so there is no other
    // way to drive it with a launcher of the test's choosing.
    void resetForTesting();

signals:
    void gamesDiscovered(const QList<Game>& games);
    void availabilityChanged();

private:
    LauncherManager();
    ~LauncherManager() = default;
    LauncherManager(const LauncherManager&) = delete;
    LauncherManager& operator=(const LauncherManager&) = delete;

    void registerBuiltinLaunchers();
    QSet<QString> currentlyAvailable() const;

    QList<std::shared_ptr<ILauncher>> m_launchers;
    QSet<QString> m_available;   // names, for change detection only
};

#endif // LAUNCHERMANAGER_H
