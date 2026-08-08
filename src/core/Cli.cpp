#include "Cli.h"

#include "core/DLSSSettings.h"
#include "core/Game.h"
#include "core/SettingsManager.h"
#include "launchers/LauncherManager.h"
#include "gog/GogAuth.h"
#include "launchers/IStoreService.h"
#include "gog/GogContentClient.h"
#include "gog/GogInstallPlan.h"
#include "gog/GogDownloader.h"
#include "gog/GogInstallRegistry.h"
#include "core/SecretStore.h"
#include "launchers/SteamLauncher.h"
#include "runner/GameRunner.h"
#include "utils/EnvBuilder.h"
#include "utils/ProtonManager.h"
#include "utils/SteamClient.h"
#include "utils/SteamPaths.h"
#include "Version.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

namespace {

// Every long option the CLI owns. isCliInvocation() matches against this list
// and nothing else, so any argument we do not know about — including all of
// Qt's own — still reaches QApplication and opens the GUI.
const char* const kOptions[] = {
    "--help", "-h", "--version", "-v",
    "--steam-info", "--list-games", "--steam-client",
    "--print-launch-options", "--parse-launch-options",
    "--apply", "--launch", "--dry-run", "--set", "--timeout",
    "--gog-login-url", "--gog-status", "--store-list", "--gog-plan",
    "--gog-install", "--gog-uninstall",
};

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream& errs()
{
    static QTextStream s(stderr);
    return s;
}

void printLine(const QString& text)
{
    out() << text << Qt::endl;
}

void printJson(const QJsonObject& obj)
{
    printLine(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void printJson(const QJsonArray& arr)
{
    printLine(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

int fail(const QString& message, int code)
{
    errs() << "protonforge: " << message << Qt::endl;
    return code;
}

QString variantName(SteamPaths::Variant v)
{
    switch (v) {
    case SteamPaths::Variant::Native:  return "native";
    case SteamPaths::Variant::Flatpak: return "flatpak";
    case SteamPaths::Variant::None:    break;
    }
    return "none";
}

// Directories under compatibilitytools.d that hold an executable `proton` —
// the same test ProtonManager and GameRunner apply when they go looking.
QJsonArray compatToolList()
{
    QJsonArray tools;
    const QString dir = SteamPaths::compatibilityToolsPath();
    if (dir.isEmpty()) {
        return tools;
    }
    const QStringList entries = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& entry : entries) {
        if (QFileInfo::exists(dir + "/" + entry + "/proton")) {
            tools.append(entry);
        }
    }
    return tools;
}

// Is this the name of a real setting?
//
// Asked of the value object rather than answered from a list here: several
// string fields (executablePath, protonVersion, customLaunchParams,
// vkd3dConfigExtra) are omitted from toJson() when they are empty, so a default
// object's key set is not the whole story. Putting a probe value under the key
// and seeing whether fromJson gives it back covers those without naming them,
// and keeps working when a field is added.
bool isKnownSetting(const QString& key, bool* isString)
{
    const QJsonObject reference = DLSSSettings().toJson();
    if (reference.contains(key)) {
        *isString = reference.value(key).isString();
        return true;
    }

    static const QString probeValue = QStringLiteral("__protonforge_probe__");
    QJsonObject probe = reference;
    probe[key] = probeValue;
    if (DLSSSettings::fromJson(probe).toJson().value(key).toString() == probeValue) {
        *isString = true;
        return true;
    }
    return false;
}

// --set key=value, applied through the JSON representation rather than a
// hand-maintained field table: DLSSSettings::toJson() already names every
// field, so a new setting becomes settable here for free. The existing value's
// JSON type decides how the string is interpreted.
bool applyOverrides(DLSSSettings& settings, const QStringList& assignments, QString* error)
{
    if (assignments.isEmpty()) {
        return true;
    }

    QJsonObject json = settings.toJson();

    for (const QString& assignment : assignments) {
        const int eq = assignment.indexOf('=');
        if (eq <= 0) {
            *error = QString("--set expects key=value, got '%1'").arg(assignment);
            return false;
        }
        const QString key = assignment.left(eq);
        const QString value = assignment.mid(eq + 1);

        bool isString = false;
        if (!isKnownSetting(key, &isString)) {
            *error = QString("unknown setting '%1'").arg(key);
            return false;
        }

        if (isString) {
            json[key] = value;
            continue;
        }

        const QJsonObject reference = DLSSSettings().toJson();
        const QJsonValue existing = json.contains(key) ? json.value(key) : reference.value(key);

        if (existing.isBool()) {
            if (value == "true" || value == "1" || value == "yes" || value == "on") {
                json[key] = true;
            } else if (value == "false" || value == "0" || value == "no" || value == "off") {
                json[key] = false;
            } else {
                *error = QString("'%1' expects a boolean, got '%2'").arg(key, value);
                return false;
            }
        } else if (existing.isDouble()) {
            bool ok = false;
            const int number = value.toInt(&ok);
            if (!ok) {
                *error = QString("'%1' expects an integer, got '%2'").arg(key, value);
                return false;
            }
            json[key] = number;
        } else {
            json[key] = value;
        }
    }

    settings = DLSSSettings::fromJson(json);
    return true;
}

QJsonObject gameToJson(const Game& game)
{
    QJsonObject o;
    o["appId"]          = game.id();
    o["name"]           = game.name();
    o["launcher"]       = game.launcher();
    o["installPath"]    = game.installPath();
    o["executablePath"] = game.executablePath();
    o["libraryPath"]    = game.libraryPath();
    o["sizeOnDisk"]     = game.sizeOnDisk();
    o["imageUrl"]       = game.imageUrl();
    o["nativeLinux"]    = game.isNativeLinux();
    o["stateFlags"]     = game.stateFlags();
    o["needsUpdate"]    = game.needsUpdate();
    o["buildId"]        = game.buildId();
    o["version"]        = game.version();
    o["settingsKey"]    = game.settingsKey();
    o["compatDataPath"]   = game.compatDataPath();
    o["shaderCachePath"]  = game.shaderCachePath();
    o["workingDirectory"] = game.workingDirectory();
    o["launchArgs"]       = QJsonArray::fromStringList(game.launchArgs());
    o["installWarnings"]  = QJsonArray::fromStringList(game.installWarnings());

    // The traits decide most of what the app does with a game, so the lab needs
    // to be able to see them rather than infer them from behaviour.
    const LauncherTraits traits = game.traits();
    QJsonObject t;
    t["usesSteamEnv"]            = traits.usesSteamEnv;
    t["requiresClientRunning"]   = traits.requiresClientRunning;
    t["supportsLaunchOptionsIO"] = traits.supportsLaunchOptionsIO;
    t["providesUpdateState"]     = traits.providesUpdateState;
    t["idIsSteamAppId"]          = traits.idIsSteamAppId;
    o["traits"] = t;

    return o;
}

QList<Game> discoverGames()
{
    return LauncherManager::instance().discoverAllGames();
}

// Finds a game by app id, discovering as a side effect. `games` gets the full
// list so callers can report how many there were.
bool findGame(const QString& appId, Game* game, QList<Game>* games = nullptr)
{
    const QList<Game> all = discoverGames();
    if (games) {
        *games = all;
    }
    for (const Game& candidate : all) {
        if (candidate.id() == appId) {
            *game = candidate;
            return true;
        }
    }
    return false;
}

DLSSSettings settingsFor(const Game& game)
{
    SettingsManager& sm = SettingsManager::instance();
    return sm.hasSettings(game.settingsKey()) ? sm.getSettings(game.settingsKey())
                                              : sm.defaultSettings();
}

QJsonObject envToJson(const QProcessEnvironment& env, const QStringList& keys)
{
    QJsonObject o;
    for (const QString& key : keys) {
        if (env.contains(key)) {
            o[key] = env.value(key);
        }
    }
    return o;
}

// ---------------------------------------------------------------- commands

int cmdSteamInfo()
{
    const SteamPaths::Variant variant = SteamPaths::detectedVariant();
    const QString root = SteamPaths::steamRoot();

    QJsonObject o;
    o["variant"]                  = variantName(variant);
    o["root"]                     = root;
    o["steamApps"]                = SteamPaths::steamAppsPath();
    o["compatibilityTools"]       = SteamPaths::compatibilityToolsPath();
    o["configVdf"]                = SteamPaths::configVdfPath();
    o["steamRuntime"]             = SteamPaths::steamRuntimePath();
    o["overlay64"]                = SteamPaths::overlayLibPath(true);
    o["overlay32"]                = SteamPaths::overlayLibPath(false);
    o["userData"]                 = SteamPaths::userDataPath();
    o["pidFile"]                  = SteamPaths::steamPidFilePath();
    o["defaultInstallCompatPath"] = SteamPaths::defaultInstallCompatPath();

    QJsonArray libraries;
    for (const QString& path : SteamLauncher::libraryPaths()) {
        libraries.append(path);
    }
    o["libraries"]   = libraries;
    o["compatTools"] = compatToolList();

    ProtonManager& pm = ProtonManager::instance();
    o["protonCachyOSInstalled"] = pm.isProtonCachyOSInstalled();
    o["protonGEInstalled"]      = pm.isProtonGEInstalled();
    o["installedVersion"]       = pm.getInstalledVersion();
    o["installedGEVersion"]     = pm.getInstalledGEVersion();

    printJson(o);

    // The JSON is printed either way — a "no Steam here" answer is a useful
    // answer — but the exit code says so too, so callers can branch on it.
    return root.isEmpty() ? Cli::NoSteam : Cli::Ok;
}

int cmdListGames()
{
    const QList<Game> games = discoverGames();
    SettingsManager& sm = SettingsManager::instance();

    LauncherManager& lm = LauncherManager::instance();

    QJsonArray arr;
    for (const Game& game : games) {
        QJsonObject o = gameToJson(game);
        o["hasSettings"] = sm.hasSettings(game.settingsKey());
        // Whatever this game's launcher already has stored, if it stores
        // anything. Launchers that do not simply return nothing.
        const auto launcher = lm.launcher(game.launcher());
        o["launchOptions"] = launcher ? launcher->readLaunchOptions(game) : QString();
        arr.append(o);
    }
    printJson(arr);

    // NoSteam only when there is genuinely nothing to report. Once a second
    // source exists, "Steam is absent" and "this command failed" stop being the
    // same statement: a GOG-only machine lists its games perfectly well, and
    // returning 3 there would have every script treat a good answer as an
    // error. --steam-info still reports on Steam itself and keeps its exit 3.
    if (!games.isEmpty()) {
        return Cli::Ok;
    }
    return SteamPaths::steamRoot().isEmpty() ? Cli::NoSteam : Cli::Ok;
}

int cmdSteamClient()
{
    const SteamClient::Diagnostics d = SteamClient::diagnose();

    QString state;
    switch (d.state) {
    case SteamClient::State::Ready:      state = "ready";       break;
    case SteamClient::State::Starting:   state = "starting";    break;
    case SteamClient::State::NotRunning: state = "not-running"; break;
    }

    QJsonObject o;
    o["state"]              = state;
    o["variant"]            = d.variant;
    o["detail"]             = d.detail;
    o["dbusConnected"]      = d.dbusConnected;
    o["dbusNameRegistered"] = d.dbusNameRegistered;
    o["pidFile"]            = d.pidFilePath;
    o["pidFileExists"]      = d.pidFileExists;
    o["pid"]                = d.pid;
    o["comm"]               = d.comm;
    o["flatpakProbeRan"]    = d.flatpakProbeRan;
    o["flatpakAppListed"]   = d.flatpakAppListed;
    printJson(o);

    return Cli::Ok;
}

int cmdPrintLaunchOptions(const QString& appId, const QStringList& overrides)
{
    Game game;
    if (!findGame(appId, &game)) {
        return fail(QString("no game with app id %1").arg(appId), Cli::UnknownGame);
    }

    DLSSSettings settings = settingsFor(game);
    QString error;
    if (!applyOverrides(settings, overrides, &error)) {
        return fail(error, Cli::UsageError);
    }

    printLine(EnvBuilder::buildLaunchOptions(settings));
    return Cli::Ok;
}

int cmdParseLaunchOptions(const QString& raw)
{
    const DLSSSettings base = SettingsManager::instance().defaultSettings();
    const EnvBuilder::ParsedLaunchOptions parsed = EnvBuilder::parseLaunchOptions(raw, base);

    QJsonObject o;
    o["input"]        = raw;
    o["settings"]     = parsed.settings.toJson();
    o["customParams"] = parsed.customParams;
    // Feeding the parsed settings straight back through the builder makes the
    // round-trip contract (EnvBuilder.cpp: parseLaunchOptions is the documented
    // inverse of buildLaunchOptions) checkable in a single call.
    o["roundTrip"]    = EnvBuilder::buildLaunchOptions(parsed.settings);
    printJson(o);
    return Cli::Ok;
}

int cmdApply(const QString& appId, const QStringList& overrides)
{
    Game game;
    if (!findGame(appId, &game)) {
        return fail(QString("no game with app id %1").arg(appId), Cli::UnknownGame);
    }

    DLSSSettings settings = settingsFor(game);
    QString error;
    if (!applyOverrides(settings, overrides, &error)) {
        return fail(error, Cli::UsageError);
    }

    auto launcher = LauncherManager::instance().launcher(game.launcher());
    if (!launcher) {
        return fail(QString("no launcher registered for '%1'").arg(game.launcher()), Cli::Error);
    }

    const QString wanted = EnvBuilder::buildLaunchOptions(settings);
    const bool applied = launcher->applySettings(game, settings);
    // Read it back rather than trusting the return value: applySettings reports
    // success on paths where it did not actually change anything.
    const QString readBack = launcher->readLaunchOptions(game);

    QJsonObject o;
    o["appId"]         = appId;
    o["applied"]       = applied;
    o["launchOptions"] = wanted;
    o["readBack"]      = readBack;
    o["matches"]       = readBack == wanted;
    printJson(o);

    return applied ? Cli::Ok : Cli::Error;
}

QJsonObject planToJson(const GameRunner::LaunchPlan& plan)
{
    QJsonObject o;
    o["valid"]            = plan.valid;
    o["error"]            = plan.error;
    o["warning"]          = plan.warning;
    o["nativeLinux"]      = plan.nativeLinux;
    o["protonPath"]       = plan.protonPath;
    o["runtimePath"]      = plan.runtimePath;
    o["runtimeRequired"]  = plan.runtimeRequired;
    o["usesContainer"]    = !plan.runtimePath.isEmpty();
    o["gameExe"]          = plan.gameExe;
    o["compatDataPath"]   = plan.compatDataPath;
    o["shaderPath"]       = plan.shaderPath;
    o["program"]          = plan.program;
    o["workingDirectory"] = plan.workingDirectory;

    // Already folded into program/args; reported on its own because otherwise
    // the nesting is only visible as "program is suddenly mangohud".
    QJsonArray wrapper;
    for (const QString& part : plan.wrapper) {
        wrapper.append(part);
    }
    o["wrapper"] = wrapper;

    QJsonArray args;
    for (const QString& arg : plan.args) {
        args.append(arg);
    }
    o["args"] = args;

    // The full environment would bury the interesting part; these are the
    // variables GameRunner itself injects.
    o["env"] = envToJson(plan.env, {
        "STEAM_COMPAT_DATA_PATH", "STEAM_COMPAT_CLIENT_INSTALL_PATH",
        "STEAM_COMPAT_APP_ID", "STEAM_COMPAT_INSTALL_PATH",
        "STEAM_COMPAT_LIBRARY_PATHS", "STEAM_COMPAT_TOOL_PATHS",
        "STEAM_COMPAT_MOUNTS", "STEAM_COMPAT_SHADER_PATH",
        "STEAM_RUNTIME", "SteamAppId", "SteamGameId",
        "LD_PRELOAD", "DISPLAY",
        // Not injected by GameRunner, but it is the other half of the overlay
        // story: with the wrapper it is redundant, without it it is all that is
        // left, and only Vulkan games benefit then.
        "MANGOHUD",
    });
    return o;
}

int cmdLaunch(const QString& appId, const QStringList& overrides, bool dryRun, int timeoutSec)
{
    Game game;
    if (!findGame(appId, &game)) {
        return fail(QString("no game with app id %1").arg(appId), Cli::UnknownGame);
    }

    DLSSSettings settings = settingsFor(game);
    QString error;
    if (!applyOverrides(settings, overrides, &error)) {
        return fail(error, Cli::UsageError);
    }

    GameRunner runner;

    if (dryRun) {
        const GameRunner::LaunchPlan plan = runner.resolveLaunch(game, settings);
        QJsonObject o = planToJson(plan);
        o["dryRun"] = true;
        printJson(o);
        return plan.valid ? Cli::Ok : Cli::Error;
    }

    QJsonObject result;
    result["appId"]   = appId;
    result["dryRun"]  = false;
    result["pending"] = false;
    result["started"] = false;

    QJsonArray warnings;
    QEventLoop loop;
    bool sawTerminal = false;
    int code = Cli::Ok;

    QObject::connect(&runner, &GameRunner::launchPending, &runner, [&](const Game&) {
        result["pending"] = true;
    });
    QObject::connect(&runner, &GameRunner::launchWarning, &runner, [&](const Game&, const QString& message) {
        warnings.append(message);
    });
    QObject::connect(&runner, &GameRunner::gameStarted, &runner, [&](const Game&) {
        result["started"] = true;
    });
    QObject::connect(&runner, &GameRunner::gameFinished, &runner, [&](const Game&, int exitCode) {
        result["finished"] = true;
        result["exitCode"] = exitCode;
        sawTerminal = true;
        loop.quit();
    });
    QObject::connect(&runner, &GameRunner::launchError, &runner, [&](const Game&, const QString& message) {
        result["error"] = message;
        code = Cli::Error;
        sawTerminal = true;
        loop.quit();
    });

    const bool accepted = runner.launch(game, settings);
    result["accepted"] = accepted;

    if (accepted && !sawTerminal) {
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, [&]() {
            result["timedOut"] = true;
            loop.quit();
        });
        guard.start(timeoutSec * 1000);
        loop.exec();
    }

    if (!accepted && code == Cli::Ok) {
        code = Cli::Error;
    }
    result["warnings"] = warnings;
    printJson(result);
    return code;
}

} // namespace

namespace Cli {

bool isCliInvocation(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        // Only the option name is compared; --set=x=y and --set x=y both start
        // with a known token.
        const char* arg = argv[i];
        for (const char* known : kOptions) {
            const size_t len = strlen(known);
            if (strncmp(arg, known, len) == 0 && (arg[len] == '\0' || arg[len] == '=')) {
                return true;
            }
        }
    }
    return false;
}

void configureMetadata(QCoreApplication& app)
{
    app.setApplicationName("ProtonForge");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("ProtonForge");
    app.setOrganizationDomain("protonforge");
}

// The credential store loads asynchronously. A CLI command runs and exits, so
// there is no event loop to wait in unless we make one — short-lived, and with
// a ceiling so a wedged keyring cannot hang a terminal.
bool waitForSecrets()
{
    SecretStore& store = SecretStore::instance();
    store.load();
    if (store.isReady()) {
        return true;
    }

    QEventLoop loop;
    QObject::connect(&store, &SecretStore::ready, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    return store.isReady();
}

int cmdGogLoginUrl()
{
    printLine(GogAuth::authorizationUrl());
    return Ok;
}

int cmdGogStatus()
{
    if (!waitForSecrets()) {
        return fail("the credential store did not come up", Error);
    }

    GogAuth& auth = GogAuth::instance();
    auth.restoreSession();

    QJsonObject o;
    o["loggedIn"] = auth.isLoggedIn();
    o["userId"]   = auth.userId();
    // Which backend actually holds the credential, because "it forgot my login"
    // is almost always this. Asked of the store rather than inferred from the
    // file existing: with nothing stored yet, both backends look the same.
    o["credentialStore"] = SecretStore::instance().backendName();
    printJson(o);
    return auth.isLoggedIn() ? Ok : Error;
}

// Drive one store's fetchLibrary() to a conclusion. The services are async by
// design (a dialog must not freeze while ten pages come in), so the CLI makes
// its own event loop rather than the services growing a blocking mode.
int cmdStoreList(const QString& launcherName)
{
    if (!waitForSecrets()) {
        return fail("the credential store did not come up", Error);
    }
    GogAuth::instance().restoreSession();

    const auto launcher = LauncherManager::instance().launcher(launcherName);
    if (!launcher) {
        return fail(QString("no launcher named '%1'").arg(launcherName), UsageError);
    }
    IStoreService* store = launcher->storeService();
    if (!store) {
        return fail(QString("'%1' has no store account").arg(launcherName), UsageError);
    }
    if (!store->isAuthenticated()) {
        // Non-zero rather than an empty array: "not configured" and "you own
        // nothing" are different answers and a script has to tell them apart.
        return fail(store->canSignIn()
                        ? QString("not signed in to %1").arg(store->displayName())
                        : store->authenticationHint(),
                    Error);
    }

    QEventLoop loop;
    int code = Error;
    QJsonArray arr;

    QObject::connect(store, &IStoreService::libraryReady, &loop,
                     [&](const QList<StoreEntry>& entries) {
        for (const StoreEntry& entry : entries) {
            QJsonObject o;
            o["id"]              = entry.id;
            o["title"]           = entry.title;
            o["storeUrl"]        = entry.storeUrl;
            o["installUrl"]      = entry.installUrl;
            o["supportsWindows"] = entry.supportsWindows;
            o["supportsLinux"]   = entry.supportsLinux;
            o["installable"]     = entry.installable;
            arr.append(o);
        }
        code = Ok;
        loop.quit();
    });
    QObject::connect(store, &IStoreService::libraryFailed, &loop, [&](const QString& reason) {
        errs() << "protonforge: " << reason << Qt::endl;
        loop.quit();
    });

    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    store->fetchLibrary();
    loop.exec();

    if (code == Ok) {
        printJson(arr);
    }
    return code;
}

// Download and install a GOG game, reporting progress on stderr so stdout stays
// a single JSON object a script can parse.
int cmdGogInstall(const QString& productId)
{
    if (productId.isEmpty()) {
        return fail("--gog-install needs a GOG product id", UsageError);
    }
    if (!GogAuth::instance().isLoggedIn()) {
        // Unlike --gog-plan, this one genuinely needs an account: the chunk URLs
        // are signed per user.
        return fail("not signed in to GOG (try --gog-login-url)", Error);
    }

    GogDownloader& downloader = GogDownloader::instance();
    GogInstallRegistry::instance().load();

    QEventLoop loop;
    int code = Error;
    QString installPath;
    int lastPercent = -1;

    QObject::connect(&downloader, &GogDownloader::installProgress, &loop,
                     [&](const QString& id, const GogDownloader::Progress& progress) {
        if (id != productId) {
            return;
        }
        if (progress.bytesTotal <= 0) {
            return;
        }
        // One line per whole percent: enough to see it moving, few enough to
        // survive being redirected to a file.
        const int percent = static_cast<int>(progress.bytesDone * 100 / progress.bytesTotal);
        if (percent != lastPercent) {
            lastPercent = percent;
            errs() << "protonforge: " << percent << "% (" << progress.filesDone << "/"
                   << progress.filesTotal << " files)" << Qt::endl;
        }
    });
    QObject::connect(&downloader, &GogDownloader::installFinished, &loop,
                     [&](const QString& id, const QString& path) {
        if (id != productId) {
            return;
        }
        installPath = path;
        code = Ok;
        loop.quit();
    });
    QObject::connect(&downloader, &GogDownloader::installFailed, &loop,
                     [&](const QString& id, const QString& reason) {
        if (id != productId) {
            return;
        }
        errs() << "protonforge: " << reason << Qt::endl;
        loop.quit();
    });

    GogDownloader::Request request;
    request.productId = productId;
    downloader.enqueue(request);
    loop.exec();

    if (code == Ok) {
        const GogInstallRegistry::Entry entry = GogInstallRegistry::instance().entry(productId);
        QJsonObject object;
        object["productId"]      = productId;
        object["title"]          = entry.title;
        object["installPath"]    = installPath;
        object["buildId"]        = entry.buildId;
        object["platform"]       = entry.platform;
        object["executablePath"] = entry.executablePath;
        object["nativeLinux"]    = entry.nativeLinux;
        object["size"]           = entry.size;
        printJson(object);
    }
    return code;
}

int cmdGogUninstall(const QString& productId)
{
    if (productId.isEmpty()) {
        return fail("--gog-uninstall needs a GOG product id", UsageError);
    }

    GogInstallRegistry::instance().load();

    QString error;
    if (!GogDownloader::instance().uninstall(productId, &error)) {
        return fail(error, Error);
    }

    QJsonObject object;
    object["productId"] = productId;
    object["removed"]   = true;
    printJson(object);
    return Ok;
}

// Resolve what installing a game would fetch, and write nothing.
//
// The analogue of --launch --dry-run, and the same reason for existing: the
// whole depot pipeline — build selection, the zlib bodies, depot filtering,
// path sanitising — can be exercised end to end against the real API without
// downloading gigabytes or touching the disk.
//
// Notably this needs no sign-in. The content system is public; only the signed
// chunk URLs require a token. So it also answers "can ProtonForge install this
// at all", which is a question worth being able to ask before signing in.
int cmdGogPlan(const QString& productId)
{
    if (productId.isEmpty()) {
        return fail("--gog-plan needs a GOG product id", UsageError);
    }

    GogContentClient& content = GogContentClient::instance();

    QEventLoop loop;
    int code = Error;
    QString chosenOs = QStringLiteral("linux");
    GogContentClient::BuildMeta meta;
    QList<GogContentClient::DepotRef> depots;
    QMap<QString, GogContentClient::DepotManifest> manifestsByHash;
    int pending = 0;

    // The same language --gog-install would use, falling back to English. A
    // picker belongs in the GUI; a CLI that has to be told a language before it
    // can answer "is this installable" would be the wrong shape. But answering
    // for a *different* language than the install would fetch makes the dry run
    // worth less than nothing — the depots differ, and so does the layout.
    const QString configured = QSettings().value("gog/language").toString();
    const QStringList languages = configured.isEmpty()
        ? QStringList{QStringLiteral("en-US"), QStringLiteral("en")}
        : QStringList{configured};

    const auto finishWithPlan = [&]() {
        QList<GogContentClient::DepotManifest> ordered;
        for (const GogContentClient::DepotRef& depot : std::as_const(depots)) {
            if (manifestsByHash.contains(depot.manifestHash)) {
                ordered.append(manifestsByHash.value(depot.manifestHash));
            }
        }

        const GogInstallPlan::Plan plan = GogInstallPlan::build(meta, ordered);

        QJsonArray files;
        for (const GogInstallPlan::FileTask& task : plan.files) {
            QJsonObject f;
            f["path"]   = task.relPath;
            f["size"]   = task.size;
            f["chunks"] = task.chunks.size();
            if (!task.linkTarget.isEmpty()) {
                f["linkTarget"] = task.linkTarget;
            }
            if (task.executable) {
                f["executable"] = true;
            }
            files.append(f);
        }

        QString collision;
        QJsonObject o;
        o["productId"]        = productId;
        o["os"]               = chosenOs;
        o["buildId"]          = meta.buildId;
        o["installDirectory"] = plan.installDirectory;
        o["depots"]           = static_cast<int>(depots.size());
        o["fileCount"]        = static_cast<int>(plan.files.size());
        o["totalSize"]        = plan.totalSize;
        o["totalDownload"]    = plan.totalCompressedSize;
        o["languages"]        = QJsonArray::fromStringList(GogInstallPlan::availableLanguages(meta));
        o["warnings"]         = QJsonArray::fromStringList(plan.warnings);
        o["caseCollision"]    = GogInstallPlan::wouldCollideCaseInsensitively(plan, &collision)
                                    ? collision : QString();
        o["files"]            = files;
        printJson(o);

        code = plan.valid ? Ok : Error;
        loop.quit();
    };

    QObject::connect(&content, &GogContentClient::depotManifestReady, &loop,
                     [&](const QString&, const QString& hash,
                         const GogContentClient::DepotManifest& manifest) {
        manifestsByHash.insert(hash, manifest);
        if (--pending == 0) {
            finishWithPlan();
        }
    });
    QObject::connect(&content, &GogContentClient::depotManifestFailed, &loop,
                     [&](const QString&, const QString& hash, const QString& reason) {
        errs() << "protonforge: depot " << hash << ": " << reason << Qt::endl;
        if (--pending == 0) {
            finishWithPlan();
        }
    });

    QObject::connect(&content, &GogContentClient::buildMetaReady, &loop,
                     [&](const QString&, const GogContentClient::BuildMeta& buildMeta) {
        meta = buildMeta;
        depots = GogInstallPlan::selectDepots(meta, languages, {}, 64);
        if (depots.isEmpty()) {
            errs() << "protonforge: this build has no depots for the chosen language" << Qt::endl;
            loop.quit();
            return;
        }
        pending = static_cast<int>(depots.size());
        for (const GogContentClient::DepotRef& depot : std::as_const(depots)) {
            content.fetchDepotManifest(productId, depot.manifestHash);
        }
    });
    QObject::connect(&content, &GogContentClient::buildMetaFailed, &loop,
                     [&](const QString&, const QString& reason) {
        errs() << "protonforge: " << reason << Qt::endl;
        loop.quit();
    });

    QObject::connect(&content, &GogContentClient::buildsReady, &loop,
                     [&](const QString&, const QList<GogContentClient::Build>& builds) {
        const GogContentClient::Build build = GogContentClient::newestPublicBuild(builds);
        if (!build.link.isEmpty()) {
            content.fetchBuildMeta(productId, build.link);
            return;
        }

        // GOG serves Linux through the content system for only a minority of
        // products; the rest ship it as a .sh installer. Falling back to the
        // Windows build is the normal outcome, not an error.
        if (chosenOs == QLatin1String("linux")) {
            chosenOs = QStringLiteral("windows");
            content.fetchBuilds(productId, chosenOs);
            return;
        }
        errs() << "protonforge: no generation-2 build for this product — "
                  "ProtonForge cannot install it" << Qt::endl;
        loop.quit();
    });
    QObject::connect(&content, &GogContentClient::buildsFailed, &loop,
                     [&](const QString&, const QString& reason) {
        errs() << "protonforge: " << reason << Qt::endl;
        loop.quit();
    });

    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    content.fetchBuilds(productId, chosenOs);
    loop.exec();

    return code;
}

int run(QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "ProtonForge — DLSS & Proton manager for Steam games.\n"
        "\n"
        "Run without arguments to open the GUI. The options below inspect and\n"
        "change the same configuration from a terminal.");

    QCommandLineOption help    = parser.addHelpOption();
    QCommandLineOption version = parser.addVersionOption();

    const QCommandLineOption steamInfo("steam-info",
        "Print the detected Steam installation as JSON.");
    const QCommandLineOption listGames("list-games",
        "Print all discovered games as a JSON array.");
    const QCommandLineOption steamClient("steam-client",
        "Print the Steam client's runtime state as JSON.");
    const QCommandLineOption printLaunchOptions("print-launch-options",
        "Print the Steam launch-options string for <appid>.", "appid");
    const QCommandLineOption parseLaunchOptions("parse-launch-options",
        "Parse a launch-options <string> back into settings and print it as JSON.", "string");
    const QCommandLineOption apply("apply",
        "Write the launch options for <appid> into Steam's localconfig.vdf.", "appid");
    const QCommandLineOption launch("launch",
        "Launch <appid>.", "appid");
    const QCommandLineOption dryRun("dry-run",
        "With --launch: resolve the launch and print the plan without starting anything.");
    const QCommandLineOption set("set",
        "Override one setting before printing, applying or launching. Repeatable. "
        "Keys are the field names from the settings file.", "key=value");
    const QCommandLineOption timeout("timeout",
        "With --launch: seconds to wait for the game (default 60).", "seconds", "60");
    const QCommandLineOption gogLoginUrl("gog-login-url",
        "Print the GOG sign-in URL to open in a browser.");
    const QCommandLineOption gogStatus("gog-status",
        "Print the GOG sign-in state as JSON.");
    const QCommandLineOption storeList("store-list",
        "Print the games owned on <launcher> as a JSON array.", "launcher");
    const QCommandLineOption gogPlan("gog-plan",
        "Resolve what installing GOG <productid> would fetch, and write nothing.", "productid");
    const QCommandLineOption gogInstall("gog-install",
        "Download and install GOG <productid>.", "productid");
    const QCommandLineOption gogUninstall("gog-uninstall",
        "Delete GOG <productid> and its Proton prefix.", "productid");

    parser.addOptions({steamInfo, listGames, steamClient, printLaunchOptions,
                       parseLaunchOptions, apply, launch, dryRun, set, timeout,
                       gogLoginUrl, gogStatus, storeList, gogPlan,
                       gogInstall, gogUninstall});

    if (!parser.parse(app.arguments())) {
        errs() << "protonforge: " << parser.errorText() << Qt::endl;
        errs() << Qt::endl << parser.helpText() << Qt::endl;
        return UsageError;
    }

    if (parser.isSet(help)) {
        printLine(parser.helpText());
        return Ok;
    }
    if (parser.isSet(version)) {
        printLine(app.applicationName() + " " + app.applicationVersion());
        return Ok;
    }

    // Exactly one command, so a typo can never silently do something else.
    const QList<QCommandLineOption> commands = {
        steamInfo, listGames, steamClient, printLaunchOptions,
        parseLaunchOptions, apply, launch, gogLoginUrl, gogStatus, storeList, gogPlan,
        gogInstall, gogUninstall,
    };
    int given = 0;
    for (const QCommandLineOption& option : commands) {
        if (parser.isSet(option)) {
            ++given;
        }
    }
    if (given == 0) {
        return fail("no command given (try --help)", UsageError);
    }
    if (given > 1) {
        return fail("only one command at a time", UsageError);
    }

    const QStringList overrides = parser.values(set);

    // --set only means something for the commands that read settings. Silently
    // ignoring it elsewhere would let a typo look like it had been applied.
    if (!overrides.isEmpty()) {
        if (parser.isSet(steamInfo) || parser.isSet(listGames)
            || parser.isSet(steamClient) || parser.isSet(parseLaunchOptions)
            || parser.isSet(gogLoginUrl) || parser.isSet(gogStatus)
            || parser.isSet(storeList) || parser.isSet(gogPlan)
            || parser.isSet(gogInstall) || parser.isSet(gogUninstall)) {
            return fail("--set has no effect on this command", UsageError);
        }
        // Check the assignments before doing any work, so a bad key is reported
        // as such rather than after a discovery pass.
        DLSSSettings probe;
        QString error;
        if (!applyOverrides(probe, overrides, &error)) {
            return fail(error, UsageError);
        }
    }

    if (parser.isSet(gogLoginUrl)) return cmdGogLoginUrl();
    if (parser.isSet(gogStatus))   return cmdGogStatus();
    if (parser.isSet(storeList))   return cmdStoreList(parser.value(storeList));
    if (parser.isSet(gogPlan))     return cmdGogPlan(parser.value(gogPlan));
    if (parser.isSet(gogInstall))  return cmdGogInstall(parser.value(gogInstall));
    if (parser.isSet(gogUninstall)) return cmdGogUninstall(parser.value(gogUninstall));
    if (parser.isSet(steamInfo))   return cmdSteamInfo();
    if (parser.isSet(listGames))   return cmdListGames();
    if (parser.isSet(steamClient)) return cmdSteamClient();

    if (parser.isSet(printLaunchOptions)) {
        return cmdPrintLaunchOptions(parser.value(printLaunchOptions), overrides);
    }
    if (parser.isSet(parseLaunchOptions)) {
        return cmdParseLaunchOptions(parser.value(parseLaunchOptions));
    }
    if (parser.isSet(apply)) {
        return cmdApply(parser.value(apply), overrides);
    }
    if (parser.isSet(launch)) {
        bool ok = false;
        const int seconds = parser.value(timeout).toInt(&ok);
        if (!ok || seconds < 0) {
            return fail("--timeout expects a non-negative number of seconds", UsageError);
        }
        return cmdLaunch(parser.value(launch), overrides, parser.isSet(dryRun), seconds);
    }

    return fail("no command given (try --help)", UsageError);
}

} // namespace Cli
