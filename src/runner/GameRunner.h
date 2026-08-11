#ifndef GAMERUNNER_H
#define GAMERUNNER_H

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QElapsedTimer>
#include <QStringList>
#include "core/Game.h"
#include "core/DLSSSettings.h"

class QTimer;

class GameRunner : public QObject {
    Q_OBJECT

public:
    explicit GameRunner(QObject* parent = nullptr);

    // Everything needed to spawn a game, with nothing spawned and nothing
    // written. resolveLaunch() fills it; launchWithProton()/launchNativeLinux()
    // then create the directories and start the process. Keeping the two apart
    // is what makes the launch chain — in particular the Steam Linux Runtime
    // wrapping, which is easy to get subtly wrong — inspectable without
    // actually running a game (see `protonforge --launch <appid> --dry-run`).
    struct LaunchPlan {
        bool valid = false;
        QString error;             // set when !valid
        QString warning;           // non-fatal; the launch still goes ahead

        bool nativeLinux = false;
        QString protonPath;        // empty on the native path
        QString runtimePath;       // Steam Linux Runtime dir, empty when unused
        bool runtimeRequired = false;
        QString gameExe;
        QString compatDataPath;    // empty on the native path
        QString shaderPath;        // only set when the container is used

        // Command prefix the game runs under — MangoHud, and any wrapper the
        // user put before %command% in their custom params. Already folded into
        // program/args below; kept separately so a dry run can show the nesting.
        QStringList wrapper;

        QString program;           // what gets exec'd
        QStringList args;
        QString workingDirectory;
        QProcessEnvironment env;
    };

    // Returns true when the launch was *accepted*. For Steam games that still
    // need the client to come up, the game starts later — watch gameStarted().
    bool launch(const Game& game, const DLSSSettings& settings);
    bool isGameRunning(const Game& game) const;
    bool isLaunchPending() const { return m_launchPending; }

    // Pure resolution: no process started, no directory created. Dispatches to
    // the native or Proton branch on game.isNativeLinux().
    LaunchPlan resolveLaunch(const Game& game, const DLSSSettings& settings);

    // Proton detection
    QString findProtonPath(const Game& game, const DLSSSettings& settings = DLSSSettings());
    QString findGameExecutable(const Game& game);
    // The prefix location now lives on the Game (Game::compatDataPath()); the
    // launcher that discovered it knows where it put it, and deriving it here
    // from the Steam library layout only ever worked for Steam.

signals:
    void gameStarted(const Game& game);
    void gameFinished(const Game& game, int exitCode);
    void launchError(const Game& game, const QString& error);
    // Non-fatal problem: the launch continues, but in a degraded mode.
    void launchWarning(const Game& game, const QString& message);
    // Launch accepted but deferred; waiting for the Steam client to become ready.
    void launchPending(const Game& game);

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

    LaunchPlan resolveProtonLaunch(const Game& game, const DLSSSettings& settings);
    LaunchPlan resolveNativeLaunch(const Game& game, const DLSSSettings& settings);

    bool launchNativeLinux(const Game& game, const DLSSSettings& settings);
    bool launchWithProton(const Game& game, const DLSSSettings& settings);

    // Dispatches to the native/Proton path. Called either directly from launch()
    // or from the Steam-readiness timer once waiting is over.
    bool continueLaunch(const Game& game, const DLSSSettings& settings);
    void onSteamWaitTick();

    QProcess* m_process = nullptr;
    Game m_runningGame;

    // Deferred launch while the Steam client comes up.
    QTimer* m_steamWaitTimer = nullptr;
    QElapsedTimer m_steamWaitElapsed;
    Game m_pendingGame;
    DLSSSettings m_pendingSettings;
    bool m_launchPending = false;
};

#endif // GAMERUNNER_H
