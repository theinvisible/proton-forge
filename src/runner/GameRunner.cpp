#include "GameRunner.h"
#include "utils/EnvBuilder.h"
#include "parsers/VDFParser.h"
#include "launchers/SteamLauncher.h"
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QThread>
#include <QCoreApplication>

GameRunner::GameRunner(QObject* parent)
    : QObject(parent)
{
}

QString GameRunner::steamPath() const
{
    QStringList possiblePaths = {
        QDir::homePath() + "/.steam/steam",
        QDir::homePath() + "/.local/share/Steam",
        QDir::homePath() + "/.steam/debian-installation"
    };

    for (const QString& path : possiblePaths) {
        if (QDir(path).exists()) {
            return path;
        }
    }

    return QDir::homePath() + "/.steam/steam";
}

QString GameRunner::getCompatDataPath(const Game& game)
{
    // Compat data is stored in the same library as the game
    return game.libraryPath() + "/compatdata/" + game.id();
}

QString GameRunner::findDefaultProton() const
{
    QString steam = steamPath();

    // Get all Steam library paths
    QStringList libraryPaths = SteamLauncher::libraryPaths();

    // Build list of directories to check for Proton
    QStringList protonDirs;
    // First check compatibilitytools.d in main Steam directory
    protonDirs << steam + "/compatibilitytools.d";

    // Add common folders from all libraries
    for (const QString& libPath : libraryPaths) {
        // Extract base path (remove /steamapps)
        QString basePath = libPath;
        if (basePath.endsWith("/steamapps")) {
            basePath.chop(10); // Remove "/steamapps"
        }
        protonDirs << libPath + "/common";
    }

    // Preferred Proton versions (newest first)
    QStringList preferredVersions = {
        "proton-cachyos",
        "GE-Proton",
        "Proton - Experimental",
        "Proton 9",
        "Proton 8",
        "Proton 7",
        "Proton Hotfix",
        "Proton 6",
        "Proton 5"
    };

    for (const QString& dir : protonDirs) {
        QDir d(dir);
        if (!d.exists()) continue;

        QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        // First check for preferred versions
        for (const QString& preferred : preferredVersions) {
            for (const QString& entry : entries) {
                if (entry.contains(preferred, Qt::CaseInsensitive)) {
                    QString protonExe = dir + "/" + entry + "/proton";
                    if (QFile::exists(protonExe)) {
                        return dir + "/" + entry;
                    }
                }
            }
        }
    }

    return QString();
}

QString GameRunner::findProtonFromConfig(const QString& appId) const
{
    // Check Steam config for per-game Proton setting
    QString configPath = steamPath() + "/config/config.vdf";

    VDFParser parser;
    if (!parser.parseFile(configPath)) {
        return QString();
    }

    VDFNode root = parser.root();

    // Navigate to: InstallConfigStore/Software/Valve/Steam/CompatToolMapping/<appId>
    if (!root.hasChild("InstallConfigStore")) return QString();

    VDFNode installConfig = root.child("InstallConfigStore");
    if (!installConfig.hasChild("Software")) return QString();

    VDFNode software = installConfig.child("Software");
    if (!software.hasChild("Valve")) return QString();

    VDFNode valve = software.child("Valve");
    if (!valve.hasChild("Steam")) return QString();

    VDFNode steam = valve.child("Steam");
    if (!steam.hasChild("CompatToolMapping")) return QString();

    VDFNode mapping = steam.child("CompatToolMapping");
    if (!mapping.hasChild(appId)) return QString();

    VDFNode appMapping = mapping.child(appId);
    QString toolName = appMapping.getString("name");

    if (toolName.isEmpty()) {
        return QString();
    }

    // Find the tool path
    QString steamApps = steamPath() + "/steamapps/common";
    QString compatTools = steamPath() + "/compatibilitytools.d";

    // Check steamapps/common first
    QString toolPath = steamApps + "/" + toolName;
    if (QFile::exists(toolPath + "/proton")) {
        return toolPath;
    }

    // Check compatibilitytools.d
    toolPath = compatTools + "/" + toolName;
    if (QFile::exists(toolPath + "/proton")) {
        return toolPath;
    }

    return QString();
}

QString GameRunner::findProtonPath(const Game& game)
{
    // First try to find per-game configured Proton
    QString protonPath = findProtonFromConfig(game.id());
    if (!protonPath.isEmpty()) {
        return protonPath;
    }

    // Fall back to default Proton
    return findDefaultProton();
}

QStringList GameRunner::findExecutables(const QString& installPath) const
{
    QStringList executables;

    QDirIterator it(installPath, {"*.exe"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();

        // Skip common non-game executables
        QString filename = QFileInfo(path).fileName().toLower();
        if (filename.contains("unins") ||
            filename.contains("setup") ||
            filename.contains("install") ||
            filename.contains("crash") ||
            filename.contains("report") ||
            filename.contains("launcher") ||  // Often separate launcher exes
            filename.contains("redist") ||
            filename.contains("vcredist") ||
            filename.contains("directx") ||
            filename.contains("dotnet") ||
            filename.contains("easyanticheat") ||
            filename.contains("battleye") ||
            filename.contains("regroup") ||  // Log utilities
            filename.contains("tts") ||  // Text-to-speech utilities
            filename.contains("voice")) {
            continue;
        }

        executables << path;
    }

    // Sort by path depth (shallower = more likely main exe)
    std::sort(executables.begin(), executables.end(), [](const QString& a, const QString& b) {
        return a.count('/') < b.count('/');
    });

    return executables;
}

QString GameRunner::findGameExecutable(const Game& game)
{
    // If already set, use it
    if (!game.executablePath().isEmpty() && QFile::exists(game.executablePath())) {
        return game.executablePath();
    }

    QStringList executables = findExecutables(game.installPath());
    if (executables.isEmpty()) {
        return QString();
    }

    // Prefer executables that match the game name
    QString gameName = game.name().toLower();
    QString installDirName = QFileInfo(game.installPath()).fileName().toLower();

    for (const QString& exe : executables) {
        QString exeName = QFileInfo(exe).baseName().toLower();

        // Check if executable name matches game name or install directory name
        if (exeName == gameName || exeName == installDirName) {
            return exe;
        }
    }

    // Check for partial matches (e.g., "GameName.exe" vs "Game Name")
    for (const QString& exe : executables) {
        QString exeName = QFileInfo(exe).baseName().toLower();
        QString gameNameNoSpaces = gameName;
        gameNameNoSpaces.remove(' ');

        if (exeName == gameNameNoSpaces || gameNameNoSpaces.contains(exeName)) {
            return exe;
        }
    }

    // Fall back to first executable (already sorted by path depth)
    return executables.first();
}

bool GameRunner::isSteamRunning() const
{
    QProcess process;
    process.start("pgrep", {"-x", "steam"});
    process.waitForFinished();
    return process.exitCode() == 0;
}

void GameRunner::ensureSteamRunning()
{
    if (!isSteamRunning()) {
        QProcess::startDetached("steam", {"-silent"});

        // Wait for Steam to appear, up to 15 seconds
        for (int i = 0; i < 30; ++i) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
            if (isSteamRunning()) {
                // Give it a bit more time to initialize
                for (int j = 0; j < 6; ++j) {
                    QThread::msleep(500);
                    QCoreApplication::processEvents();
                }
                return;
            }
        }
    }
}

bool GameRunner::launch(const Game& game, const DLSSSettings& settings)
{
    if (game.launcher() == "Steam") {
        ensureSteamRunning();
    }

    QString protonPath = findProtonPath(game);
    if (protonPath.isEmpty()) {
        emit launchError(game, "Could not find Proton installation");
        return false;
    }

    QString gameExe = findGameExecutable(game);
    if (gameExe.isEmpty()) {
        emit launchError(game, "Could not find game executable");
        return false;
    }

    QString compatDataPath = getCompatDataPath(game);

    // Build environment
    QProcessEnvironment env = EnvBuilder::buildEnvironment(settings);

    // Required Proton environment variables
    env.insert("STEAM_COMPAT_DATA_PATH", compatDataPath);
    env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamPath());
    env.insert("SteamAppId", game.id());
    env.insert("SteamGameId", game.id());

    // Setup Steam Overlay
    QString steamRoot = steamPath();
    QString overlay64 = steamRoot + "/ubuntu12_64/gameoverlayrenderer.so";
    QString overlay32 = steamRoot + "/ubuntu12_32/gameoverlayrenderer.so";

    QStringList preloads;
    if (env.contains("LD_PRELOAD")) {
        preloads << env.value("LD_PRELOAD");
    }

    if (QFile::exists(overlay64)) {
        preloads << overlay64;
    }
    if (QFile::exists(overlay32)) {
        preloads << overlay32;
    }

    if (!preloads.isEmpty()) {
        env.insert("LD_PRELOAD", preloads.join(":"));
    }

    // Create compat data directory if needed
    QDir().mkpath(compatDataPath);

    // Clean up previous process
    if (m_process) {
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(QFileInfo(gameExe).absolutePath());

    QString protonExe = protonPath + "/proton";

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, game](int exitCode, QProcess::ExitStatus) {
        emit gameFinished(game, exitCode);
    });

    connect(m_process, &QProcess::errorOccurred, this, [this, game](QProcess::ProcessError error) {
        QString errorMsg;
        switch (error) {
            case QProcess::FailedToStart:
                errorMsg = "Failed to start Proton";
                break;
            case QProcess::Crashed:
                errorMsg = "Proton crashed";
                break;
            default:
                errorMsg = "Unknown error";
        }
        emit launchError(game, errorMsg);
    });

    // Launch: proton run /path/to/game.exe
    m_process->start(protonExe, {"run", gameExe});

    if (m_process->waitForStarted(5000)) {
        emit gameStarted(game);
        return true;
    }

    emit launchError(game, "Proton failed to start within timeout");
    return false;
}
