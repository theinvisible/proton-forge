#include "GameRunner.h"
#include "utils/EnvBuilder.h"
#include "utils/ProtonManager.h"
#include "utils/SteamPaths.h"
#include "utils/SteamClient.h"
#include "parsers/VDFParser.h"
#include "launchers/SteamLauncher.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr int kSteamPollIntervalMs  = 500;
// Older clients never register the launcher service; once the process has been
// alive this long, stop waiting for a signal that may never come.
constexpr int kSteamLivenessGraceMs = 10000;
constexpr int kSteamReadyTimeoutMs  = 60000;

// The command prefix the game is launched under, outermost first.
//
// MangoHud has to be a wrapper, not just MANGOHUD=1: that variable only enables
// the Vulkan implicit layer, so an OpenGL game (Stellaris, and every other
// Clausewitz title) shows nothing. The wrapper script preloads libMangoHud,
// which is what covers OpenGL.
//
// A user wrapper from customLaunchParams ("gamemoderun %command%") goes inside
// it — Steam honours those and a direct launch used to drop them silently.
// Resolves the prefix, records it on the plan and folds it into what actually
// gets exec'd:  wrapper=[mangohud], program=game, args=[-x] -> mangohud game -x
// Call once, at the end of a resolver, after program and args are final.
void applyWrapper(GameRunner::LaunchPlan& plan, const DLSSSettings& settings)
{
    if (settings.enableMangoHud) {
        const QString mangohud = QStandardPaths::findExecutable("mangohud");
        if (mangohud.isEmpty()) {
            // Not fatal: MANGOHUD=1 is still in the environment, so a Vulkan
            // game keeps its overlay. Only OpenGL loses out. Append rather than
            // assign — the Proton path may already have a warning of its own.
            const QString note =
                "MangoHud is enabled but the 'mangohud' command was not found in "
                "PATH — the overlay will only appear in Vulkan games. Install the "
                "mangohud package for OpenGL titles.";
            plan.warning = plan.warning.isEmpty() ? note : plan.warning + "\n\n" + note;
        } else {
            plan.wrapper << mangohud;
        }
    }

    plan.wrapper << EnvBuilder::customWrapper(settings);

    if (plan.wrapper.isEmpty()) {
        return;
    }

    QStringList args = plan.wrapper.mid(1);
    args << plan.program;
    args << plan.args;

    plan.program = plan.wrapper.first();
    plan.args    = args;
}

} // namespace

GameRunner::GameRunner(QObject* parent)
    : QObject(parent)
{
}

QString GameRunner::findDefaultProton() const
{
    // Get all Steam library paths
    QStringList libraryPaths = SteamLauncher::libraryPaths();

    // Build list of directories to check for Proton
    QStringList protonDirs;

    // Where our own Proton-Manager installs to. With Steam present this is the
    // same compatibilitytools.d as below and dedups away; without Steam it is
    // the only one of these that resolves at all, and leaving it out is what
    // made the default ("auto") find nothing on a machine with no Steam but a
    // Proton installed through ProtonForge itself.
    const QString managedTools = ProtonManager::protonCachyOSPath();
    if (!managedTools.isEmpty()) {
        protonDirs << managedTools;
    }

    // Then compatibilitytools.d in the detected Steam directory
    const QString compatTools = SteamPaths::compatibilityToolsPath();
    if (!compatTools.isEmpty() && !protonDirs.contains(compatTools)) {
        protonDirs << compatTools;
    }

    // Add common folders from all libraries
    for (const QString& libPath : libraryPaths) {
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

QString GameRunner::findLatestSteamProton() const
{
    // Get all Steam library paths
    QStringList libraryPaths = SteamLauncher::libraryPaths();

    // Build list of directories to check for Proton
    QStringList protonDirs;

    // Add common folders from all libraries (official Steam Proton is in common/)
    for (const QString& libPath : libraryPaths) {
        protonDirs << libPath + "/common";
    }

    // Preferred Steam Proton versions (newest first)
    QStringList steamProtonVersions = {
        "Proton - Experimental",
        "Proton 10",
        "Proton 9",
        "Proton 8"
    };

    for (const QString& dir : protonDirs) {
        QDir d(dir);
        if (!d.exists()) continue;

        QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        // Check for Steam Proton versions (excluding CachyOS and GE)
        for (const QString& preferred : steamProtonVersions) {
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
    const QString configPath = SteamPaths::configVdfPath();
    if (configPath.isEmpty()) {
        return QString();
    }

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
    const QString steamApps = SteamPaths::steamAppsPath() + "/common";
    const QString compatTools = SteamPaths::compatibilityToolsPath();

    // Check steamapps/common first
    QString toolPath = steamApps + "/" + toolName;
    if (QFile::exists(toolPath + "/proton")) {
        return toolPath;
    }

    // Check compatibilitytools.d
    if (!compatTools.isEmpty()) {
        toolPath = compatTools + "/" + toolName;
        if (QFile::exists(toolPath + "/proton")) {
            return toolPath;
        }
    }

    return QString();
}

QString GameRunner::findRequiredRuntimeTool(const QString& protonPath, bool* required) const
{
    *required = false;

    const QString manifestPath = protonPath + "/toolmanifest.vdf";
    if (!QFile::exists(manifestPath)) {
        return QString();
    }

    VDFParser parser;
    if (!parser.parseFile(manifestPath)) {
        return QString();
    }

    VDFNode root = parser.root();
    if (!root.hasChild("manifest")) {
        return QString();
    }

    const QString toolAppId = root.child("manifest").getString("require_tool_appid");
    if (toolAppId.isEmpty()) {
        // This Proton runs directly on the host; nothing to wrap it in.
        return QString();
    }

    *required = true;
    return findToolByAppId(toolAppId);
}

QString GameRunner::findToolByAppId(const QString& appId) const
{
    const QStringList libraryPaths = SteamLauncher::libraryPaths();

    for (const QString& libPath : libraryPaths) {
        const QString manifestPath = libPath + "/appmanifest_" + appId + ".acf";
        if (!QFile::exists(manifestPath)) continue;

        VDFParser parser;
        if (!parser.parseFile(manifestPath)) continue;

        VDFNode root = parser.root();
        if (!root.hasChild("AppState")) continue;

        const QString installDir = root.child("AppState").getString("installdir");
        if (installDir.isEmpty()) continue;

        const QString toolPath = libPath + "/common/" + installDir;
        if (QFileInfo(toolPath + "/_v2-entry-point").isExecutable()) {
            return toolPath;
        }
    }

    return QString();
}

QString GameRunner::findProtonPath(const Game& game, const DLSSSettings& settings)
{
    // First check if user selected a specific Proton version
    if (!settings.protonVersion.isEmpty()) {
        // Handle special values
        if (settings.protonVersion == "auto") {
            // Use latest CachyOS - fall through to default logic
        } else if (settings.protonVersion == "latest-ge") {
            // Find latest GE-Proton version
            QDir dir(ProtonManager::protonCachyOSPath());
            if (dir.exists()) {
                QStringList geVersions = dir.entryList(QStringList() << "GE-Proton*", QDir::Dirs, QDir::Name | QDir::Reversed);
                if (!geVersions.isEmpty()) {
                    QString protonPath = ProtonManager::protonCachyOSPath() + "/" + geVersions.first();
                    if (QFile::exists(protonPath + "/proton")) {
                        return protonPath;
                    }
                }
            }
        } else if (settings.protonVersion == "steam-proton") {
            // Find latest Steam Proton version
            QString steamProton = findLatestSteamProton();
            if (!steamProton.isEmpty()) {
                return steamProton;
            }
        } else {
            // Check if it's an absolute path (for Steam Proton versions)
            QString protonPath = settings.protonVersion;
            if (!protonPath.startsWith("/")) {
                // Relative path, prepend compatibilitytools.d directory
                protonPath = ProtonManager::protonCachyOSPath() + "/" + settings.protonVersion;
            }

            // Use specific version selected by user
            if (QFile::exists(protonPath + "/proton")) {
                return protonPath;
            }
        }
    }

    // Then Steam's own per-game choice. Only for games whose id really is a
    // Steam appid — another store's product id that happens to be the same
    // number would otherwise silently pick up a stranger's CompatToolMapping.
    if (game.traits().idIsSteamAppId) {
        QString protonPath = findProtonFromConfig(game.id());
        if (!protonPath.isEmpty()) {
            return protonPath;
        }
    }

    // Fall back to default Proton (latest CachyOS)
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

bool GameRunner::launch(const Game& game, const DLSSSettings& settings)
{
    // Check if this game is already running
    if (isGameRunning(game)) {
        emit launchError(game, "Game is already running");
        return false;
    }

    if (m_launchPending) {
        emit launchError(game, "A launch is already in progress");
        return false;
    }

    // Steam games need the client up — Steamworks, overlay, cloud saves and
    // playtime all go through it. The wait runs on a timer instead of spinning
    // the event loop inside this call.
    if (game.traits().requiresClientRunning && !SteamClient::isReady()) {
        if (!SteamClient::isRunning()) {
            QString error;
            if (!SteamClient::start(&error)) {
                emit launchError(game, error);
                return false;
            }
        }

        m_pendingGame = game;
        m_pendingSettings = settings;
        m_launchPending = true;
        m_steamWaitElapsed.start();

        if (!m_steamWaitTimer) {
            m_steamWaitTimer = new QTimer(this);
            m_steamWaitTimer->setInterval(kSteamPollIntervalMs);
            connect(m_steamWaitTimer, &QTimer::timeout, this, &GameRunner::onSteamWaitTick);
        }
        m_steamWaitTimer->start();

        emit launchPending(game);
        return true;   // accepted; the game starts once Steam is ready
    }

    return continueLaunch(game, settings);
}

void GameRunner::onSteamWaitTick()
{
    const qint64 waited = m_steamWaitElapsed.elapsed();

    // One state query per tick. isReady() and isRunning() each re-run the full
    // probe, which on a Flatpak Steam install means a blocking `flatpak ps` — at
    // a 500 ms interval, asking twice doubled that for no new information.
    const SteamClient::State state = SteamClient::state();
    const bool ready = state == SteamClient::State::Ready
        || (waited >= kSteamLivenessGraceMs && state != SteamClient::State::NotRunning);

    if (!ready && waited < kSteamReadyTimeoutMs) {
        return;
    }

    m_steamWaitTimer->stop();
    m_launchPending = false;

    const Game game = m_pendingGame;
    const DLSSSettings settings = m_pendingSettings;
    m_pendingGame = Game();

    if (!ready) {
        emit launchWarning(game, QString(
            "Steam did not become ready within %1 s — launching %2 anyway. "
            "Steamworks features may be unavailable.")
            .arg(kSteamReadyTimeoutMs / 1000).arg(game.name()));
    }

    continueLaunch(game, settings);
}

bool GameRunner::continueLaunch(const Game& game, const DLSSSettings& settings)
{
    // Native Linux games don't need Proton
    if (game.isNativeLinux()) {
        return launchNativeLinux(game, settings);
    }

    // Windows games need Proton
    return launchWithProton(game, settings);
}

bool GameRunner::isGameRunning(const Game& game) const
{
    // Check if we have a running process and it's for this game
    return m_process && m_process->state() == QProcess::Running &&
           m_runningGame == game;
}

GameRunner::LaunchPlan GameRunner::resolveLaunch(const Game& game, const DLSSSettings& settings)
{
    return game.isNativeLinux() ? resolveNativeLaunch(game, settings)
                                : resolveProtonLaunch(game, settings);
}

GameRunner::LaunchPlan GameRunner::resolveProtonLaunch(const Game& game, const DLSSSettings& settings)
{
    LaunchPlan plan;
    const LauncherTraits traits = game.traits();

    QString protonPath = findProtonPath(game, settings);
    if (protonPath.isEmpty()) {
        plan.error = "Could not find Proton installation";
        return plan;
    }

    QString gameExe = findGameExecutable(game);
    if (gameExe.isEmpty()) {
        plan.error = "Could not find game executable";
        return plan;
    }

    // Empty means no launcher ever said where this game's prefix belongs.
    // Refuse rather than invent one: an unwritable STEAM_COMPAT_DATA_PATH fails
    // deep inside Proton, a long way from the actual cause.
    const QString compatDataPath = game.compatDataPath();
    if (compatDataPath.isEmpty()) {
        plan.error = "No Proton prefix location is configured for this game";
        return plan;
    }

    // Resolve the Steam Linux Runtime this Proton asks for. Running inside it
    // is what Steam does, and it keeps helper processes (MangoHud's popen()
    // calls, protonfixes, ...) on the runtime's own userland instead of the
    // host's, which is where host/Proton mismatches otherwise surface.
    bool runtimeRequired = false;
    const QString runtimePath = findRequiredRuntimeTool(protonPath, &runtimeRequired);
    const bool useContainer = !runtimePath.isEmpty();

    if (runtimeRequired && !useContainer) {
        plan.warning = QString(
            "Steam Linux Runtime not installed — launching %1 without the container. "
            "ProtonForge cannot install it; Steam ships it as a compatibility tool.")
            .arg(game.name());
    }

    // Build environment
    QProcessEnvironment env = EnvBuilder::buildEnvironment(settings);

    // Proton needs to know where its prefix goes whoever owns the game.
    env.insert("STEAM_COMPAT_DATA_PATH", compatDataPath);

    // Only when there is a Steam install to point at. Proton reads this to link
    // steamclient.so into the prefix and copes with it being absent; handing it
    // an empty string instead is strictly worse than saying nothing.
    const QString steamRoot = SteamPaths::steamRoot();
    if (!steamRoot.isEmpty()) {
        env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamRoot);
    }

    // Steamworks identity. A DRM-free game from another store has no appid and
    // must not claim one.
    if (traits.usesSteamEnv) {
        env.insert("SteamAppId", game.id());
        env.insert("SteamGameId", game.id());
    }

    QString shaderPath;
    if (useContainer) {
        // pressure-vessel only sees what it is told to mount, so anything the
        // game touches has to be named here. The install path matters as much
        // as the executable's own directory: a game whose exe lives in bin/
        // keeps its data a level up, and mounting only bin/ starts a game that
        // cannot find its own assets.
        QStringList mounts = SteamLauncher::libraryPaths();
        mounts << game.installPath()
               << QFileInfo(gameExe).absolutePath()
               << QFileInfo(compatDataPath).absolutePath();
        mounts.removeAll(QString());
        mounts.removeDuplicates();

        shaderPath = game.shaderCachePath();

        env.insert("STEAM_COMPAT_APP_ID",
                   traits.usesSteamEnv ? game.id() : QStringLiteral("0"));
        env.insert("STEAM_COMPAT_INSTALL_PATH", game.installPath());
        env.insert("STEAM_COMPAT_TOOL_PATHS", protonPath + ":" + runtimePath);
        if (!game.libraryPath().isEmpty()) {
            env.insert("STEAM_COMPAT_LIBRARY_PATHS", game.libraryPath());
        }
        if (!mounts.isEmpty()) {
            env.insert("STEAM_COMPAT_MOUNTS", mounts.join(":"));
        }
        if (!shaderPath.isEmpty()) {
            env.insert("STEAM_COMPAT_SHADER_PATH", shaderPath);
        }
    }

    // The legacy Steam runtime. Nothing here executes it, but Proton looks for
    // it — and there is nothing to look for without a Steam install.
    if (traits.usesSteamEnv) {
        const QString steamRuntime = SteamPaths::steamRuntimePath();
        if (!steamRuntime.isEmpty()) {
            env.insert("STEAM_RUNTIME", steamRuntime);
        }
    }

    // Inherit current user's DISPLAY and other X11 variables
    if (!env.contains("DISPLAY")) {
        env.insert("DISPLAY", ":0");
    }

    // The overlay is Steam's, and enableSteamOverlay defaults to true. Gating on
    // the trait rather than flipping that default is what keeps every existing
    // user's stored value working while a GOG game never sees the preload.
    if (traits.usesSteamEnv && settings.enableSteamOverlay) {
        const QString overlay64 = SteamPaths::overlayLibPath(true);
        const QString overlay32 = SteamPaths::overlayLibPath(false);

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
    }

    // Launch, mirroring how Steam composes the compat tool chain:
    //   <runtime>/_v2-entry-point --verb=waitforexitandrun -- <proton> waitforexitandrun <exe>
    // Without a runtime this stays the plain `proton run <exe>`. The verb
    // matters: --verb=run puts the entry point in batch mode, which is meant
    // for setup commands rather than the main process.
    const QString protonExe = protonPath + "/proton";

    if (useContainer) {
        plan.program = runtimePath + "/_v2-entry-point";
        plan.args << "--verb=waitforexitandrun" << "--" << protonExe << "waitforexitandrun" << gameExe;
    } else {
        plan.program = protonExe;
        plan.args << "run" << gameExe;
    }
    // What the launcher says the game needs, then what the user added — so a
    // user's own arguments still come last and win.
    plan.args << game.launchArgs() << EnvBuilder::customGameArgs(settings);

    plan.valid            = true;
    plan.nativeLinux      = false;
    plan.protonPath       = protonPath;
    plan.runtimePath      = runtimePath;
    plan.runtimeRequired  = runtimeRequired;
    plan.gameExe          = gameExe;
    plan.compatDataPath   = compatDataPath;
    plan.shaderPath       = shaderPath;
    plan.workingDirectory = game.workingDirectory().isEmpty()
                                ? QFileInfo(gameExe).absolutePath()
                                : game.workingDirectory();
    plan.env              = env;

    // Outermost, in front of the whole compat-tool chain — the same nesting
    // Steam produces from "mangohud %command%".
    applyWrapper(plan, settings);
    return plan;
}

bool GameRunner::launchWithProton(const Game& game, const DLSSSettings& settings)
{
    const LaunchPlan plan = resolveLaunch(game, settings);
    if (!plan.valid) {
        emit launchError(game, plan.error);
        return false;
    }
    if (!plan.warning.isEmpty()) {
        emit launchWarning(game, plan.warning);
    }

    // Create compat data directory if needed. Failing this is fatal: Proton
    // would be handed a STEAM_COMPAT_DATA_PATH it cannot write to and fail far
    // from the actual cause. The shader cache below is only an optimisation,
    // hence the deliberate asymmetry.
    if (!QDir().mkpath(plan.compatDataPath)) {
        emit launchError(game, "Could not create the Proton prefix directory: " + plan.compatDataPath);
        return false;
    }
    if (!plan.shaderPath.isEmpty()) {
        QDir().mkpath(plan.shaderPath);
    }

    // Clean up previous process
    if (m_process) {
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProcessEnvironment(plan.env);
    m_process->setWorkingDirectory(plan.workingDirectory);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, game](int exitCode, QProcess::ExitStatus) {
        m_runningGame = Game();  // Clear running game
        emit gameFinished(game, exitCode);
    });

    connect(m_process, &QProcess::errorOccurred, this, [this, game](QProcess::ProcessError error) {
        m_runningGame = Game();  // Clear running game on error
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

    m_process->start(plan.program, plan.args);

    if (m_process->waitForStarted(5000)) {
        m_runningGame = game;  // Track running game
        emit gameStarted(game);
        return true;
    }

    emit launchError(game, "Proton failed to start within timeout");
    return false;
}

QString GameRunner::findLinuxExecutable(const Game& game)
{
    // If already set, use it
    if (!game.executablePath().isEmpty() && QFile::exists(game.executablePath())) {
        return game.executablePath();
    }

    QString installPath = game.installPath();
    QStringList candidates;

    // Common Linux executable patterns
    QStringList nameVariants;
    QString gameName = game.name().toLower();
    QString gameNameNoSpaces = gameName;
    gameNameNoSpaces.remove(' ');
    QString installDirName = QFileInfo(installPath).fileName().toLower();

    nameVariants << gameNameNoSpaces << gameName.replace(' ', '_')
                 << gameName.replace(' ', '-') << installDirName;

    // Search for executables (files with execute permission, no extension)
    QDirIterator it(installPath, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QString filename = QFileInfo(path).fileName().toLower();

        // Skip common non-game files
        if (filename.contains("uninstall") || filename.contains("setup") ||
            filename.endsWith(".sh") || filename.endsWith(".py") ||
            filename.endsWith(".so") || filename.contains("crash")) {
            continue;
        }

        candidates << path;
    }

    // Sort candidates by path depth (shallower first)
    std::sort(candidates.begin(), candidates.end(), [](const QString& a, const QString& b) {
        return a.count('/') < b.count('/');
    });

    // Prefer executables matching game name
    for (const QString& exe : candidates) {
        QString exeName = QFileInfo(exe).fileName().toLower();
        for (const QString& variant : nameVariants) {
            if (exeName == variant || exeName.contains(variant)) {
                return exe;
            }
        }
    }

    // Look for x86_64 executables (more likely to be the main game)
    for (const QString& exe : candidates) {
        if (exe.contains("x86_64") || exe.contains("x64")) {
            return exe;
        }
    }

    // Return first candidate if we found any
    if (!candidates.isEmpty()) {
        return candidates.first();
    }

    return QString();
}

GameRunner::LaunchPlan GameRunner::resolveNativeLaunch(const Game& game, const DLSSSettings& settings)
{
    LaunchPlan plan;

    QString gameExe = findLinuxExecutable(game);
    if (gameExe.isEmpty()) {
        plan.error = "Could not find game executable";
        return plan;
    }

    // Build environment with DLSS settings
    QProcessEnvironment env = EnvBuilder::buildEnvironment(settings);

    // Add Steam environment variables for Steam games
    if (game.traits().usesSteamEnv) {
        env.insert("SteamAppId", game.id());
        env.insert("SteamGameId", game.id());

        // Setup Steam Overlay
        if (settings.enableSteamOverlay) {
            const QString overlay64 = SteamPaths::overlayLibPath(true);
            const QString overlay32 = SteamPaths::overlayLibPath(false);

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
        }
    }

    // Launch the game directly, with any custom game args from launch params
    plan.valid            = true;
    plan.nativeLinux      = true;
    plan.gameExe          = gameExe;
    plan.program          = gameExe;
    plan.args             = game.launchArgs() + EnvBuilder::customGameArgs(settings);
    plan.workingDirectory = game.workingDirectory().isEmpty()
                                ? QFileInfo(gameExe).absolutePath()
                                : game.workingDirectory();
    plan.env              = env;

    applyWrapper(plan, settings);
    return plan;
}

bool GameRunner::launchNativeLinux(const Game& game, const DLSSSettings& settings)
{
    const LaunchPlan plan = resolveLaunch(game, settings);
    if (!plan.valid) {
        emit launchError(game, plan.error);
        return false;
    }
    if (!plan.warning.isEmpty()) {
        emit launchWarning(game, plan.warning);
    }

    // Clean up previous process
    if (m_process) {
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProcessEnvironment(plan.env);
    m_process->setWorkingDirectory(plan.workingDirectory);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, game](int exitCode, QProcess::ExitStatus) {
        m_runningGame = Game();  // Clear running game
        emit gameFinished(game, exitCode);
    });

    connect(m_process, &QProcess::errorOccurred, this, [this, game](QProcess::ProcessError error) {
        QString errorMsg;
        switch (error) {
            case QProcess::FailedToStart:
                errorMsg = "Failed to start game";
                break;
            case QProcess::Crashed:
                errorMsg = "Game crashed";
                break;
            default:
                errorMsg = "Unknown error";
        }
        emit launchError(game, errorMsg);
    });

    m_process->start(plan.program, plan.args);

    if (m_process->waitForStarted(5000)) {
        m_runningGame = game;  // Track running game
        emit gameStarted(game);
        return true;
    }

    emit launchError(game, "Game failed to start within timeout");
    return false;
}
