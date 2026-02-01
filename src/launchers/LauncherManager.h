#ifndef LAUNCHERMANAGER_H
#define LAUNCHERMANAGER_H

#include <QObject>
#include <QList>
#include <memory>
#include "ILauncher.h"
#include "core/Game.h"

class LauncherManager : public QObject {
    Q_OBJECT

public:
    static LauncherManager& instance();

    void registerLauncher(std::shared_ptr<ILauncher> launcher);
    QList<std::shared_ptr<ILauncher>> launchers() const;
    std::shared_ptr<ILauncher> launcher(const QString& name) const;

    QList<Game> discoverAllGames();

signals:
    void gamesDiscovered(const QList<Game>& games);

private:
    LauncherManager();
    ~LauncherManager() = default;
    LauncherManager(const LauncherManager&) = delete;
    LauncherManager& operator=(const LauncherManager&) = delete;

    QList<std::shared_ptr<ILauncher>> m_launchers;
};

#endif // LAUNCHERMANAGER_H
