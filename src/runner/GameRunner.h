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
    bool isGameRunning(const Game& game) const;

    // Proton detection
    QString findProtonPath(const Game& game, const DLSSSettings& settings = DLSSSettings());
    QString findGameExecutable(const Game& game);
    QString getCompatDataPath(const Game& game);

signals:
    void gameStarted(const Game& game);
    void gameFinished(const Game& game, int exitCode);
    void launchError(const Game& game, const QString& error);
    // Non-fatal problem: the launch continues, but in a degraded mode.
    void launchWarning(const Game& game, const QString& message);

private:
    QString findDefaultProton() const;
    QString findLatestSteamProton() const;
    QString findProtonFromConfig(const QString& appId) const;

    // Steam Linux Runtime container. A Proton build declares the runtime it
    // wants via `require_tool_appid` in its toolmanifest.vdf; Steam honours
    // that by wrapping the Proton call in the runtime's _v2-entry-point.
    // Returns the runtime's install directory, or empty if none is needed
    // (*required == false) or none is installed (*required == true).
    QString findRequiredRuntimeTool(const QString& protonPath, bool* required) const;
    QString findToolByAppId(const QString& appId) const;
    QStringList findExecutables(const QString& installPath) const;
    QString findLinuxExecutable(const Game& game);

    bool launchNativeLinux(const Game& game, const DLSSSettings& settings);
    bool launchWithProton(const Game& game, const DLSSSettings& settings);

    void ensureSteamRunning();
    bool isSteamRunning() const;

    QProcess* m_process = nullptr;
    Game m_runningGame;
};

#endif // GAMERUNNER_H
