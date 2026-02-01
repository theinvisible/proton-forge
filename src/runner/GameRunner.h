#ifndef GAMERUNNER_H
#define GAMERUNNER_H

#include <QObject>
#include <QProcess>
#include "core/Game.h"
#include "core/DLSSSettings.h"

class GameRunner : public QObject {
    Q_OBJECT

public:
    explicit GameRunner(QObject* parent = nullptr);

    bool launch(const Game& game, const DLSSSettings& settings);

    // Proton detection
    QString findProtonPath(const Game& game);
    QString findGameExecutable(const Game& game);
    QString getCompatDataPath(const Game& game);

signals:
    void gameStarted(const Game& game);
    void gameFinished(const Game& game, int exitCode);
    void launchError(const Game& game, const QString& error);

private:
    QString steamPath() const;
    QString findDefaultProton() const;
    QString findProtonFromConfig(const QString& appId) const;
    QStringList findExecutables(const QString& installPath) const;

    QProcess* m_process = nullptr;
};

#endif // GAMERUNNER_H
