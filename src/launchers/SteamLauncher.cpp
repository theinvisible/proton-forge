#include "SteamLauncher.h"
#include "SteamStoreService.h"
#include "parsers/VDFParser.h"
#include "utils/EnvBuilder.h"
#include "utils/SteamPaths.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QDirIterator>

namespace {

// Steam's AppState::StateFlags is a bitmask; bit 2 is "update required". This
// is the only place that bit is interpreted — Game::needsUpdate() is a plain
// stored bool, because the encoding means nothing to any other launcher.
constexpr int kStateUpdateRequired = 2;

bool updatePending(int stateFlags)
{
    return (stateFlags & kStateUpdateRequired) != 0;
}

} // namespace

SteamLauncher::SteamLauncher()
{
}

SteamLauncher::~SteamLauncher() = default;

IStoreService* SteamLauncher::storeService()
{
    if (!m_storeService) {
        m_storeService = std::make_unique<SteamStoreService>();
    }
    return m_storeService.get();
}

LauncherTraits SteamLauncher::traits() const
{
    LauncherTraits traits;
    traits.usesSteamEnv            = true;
    traits.requiresClientRunning   = true;
    traits.supportsLaunchOptionsIO = true;
    traits.providesUpdateState     = true;
    traits.idIsSteamAppId          = true;
    return traits;
}

bool SteamLauncher::isAvailable() const
{
    return !SteamPaths::steamRoot().isEmpty();
}

QString SteamLauncher::readLaunchOptions(const Game& game) const
{
    return readLaunchOptions(game.id());
}

bool SteamLauncher::refreshGameState(Game& game) const
{
    return checkUpdateStatus(game);
}

QString SteamLauncher::steamPath()
{
    return SteamPaths::steamRoot();
}

QString SteamLauncher::steamAppsPath()
{
    return SteamPaths::steamAppsPath();
}

QStringList SteamLauncher::libraryPaths()
{
    QStringList paths;

    const QString defaultPath = SteamPaths::steamAppsPath();
    if (defaultPath.isEmpty()) {
        return paths;
    }
    if (QDir(defaultPath).exists()) {
        paths << defaultPath;
    }

    // Parse libraryfolders.vdf for additional library folders
    QString libraryFoldersPath = defaultPath + "/libraryfolders.vdf";
    VDFParser parser;
    if (parser.parseFile(libraryFoldersPath)) {
        VDFNode root = parser.root();

        // libraryfolders.vdf structure:
        // "libraryfolders" { "0" { "path" "..." } "1" { "path" "..." } ... }
        if (root.hasChild("libraryfolders")) {
            VDFNode folders = root.child("libraryfolders");
            for (auto it = folders.children().constBegin(); it != folders.children().constEnd(); ++it) {
                // Keys are numeric indices
                bool isNumber;
                it.key().toInt(&isNumber);
                if (isNumber && it.value().hasChild("path")) {
                    QString libPath = it.value().getString("path");
                    QString steamApps = libPath + "/steamapps";
                    if (QDir(steamApps).exists() && !paths.contains(steamApps)) {
                        paths << steamApps;
                    }
                }
            }
        }
    }

    return paths;
}

QList<Game> SteamLauncher::discoverGames()
{
    QList<Game> games;
    QStringList libraries = libraryPaths();

    // Filter patterns for non-game apps
    QStringList filterPatterns = {
        "Steamworks Common Redistributables",
        "Steam Linux Runtime",
        "Proton",
        "SteamVR",
        "Steam Audio",
        "Steamworks Shared"
    };

    for (const QString& libraryPath : libraries) {
        QDir dir(libraryPath);
        QStringList manifests = dir.entryList({"appmanifest_*.acf"}, QDir::Files);

        for (const QString& manifest : manifests) {
            QString manifestPath = libraryPath + "/" + manifest;
            Game game = parseAppManifest(manifestPath, libraryPath);

            if (game.id().isEmpty() || game.name().isEmpty()) {
                continue;
            }

            // Filter out Steam tools and runtimes
            bool shouldFilter = false;
            for (const QString& pattern : filterPatterns) {
                if (game.name().contains(pattern, Qt::CaseInsensitive)) {
                    shouldFilter = true;
                    break;
                }
            }

            if (!shouldFilter) {
                games << game;
            }
        }
    }

    // Sort by name
    std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
        return a.name().toLower() < b.name().toLower();
    });

    return games;
}

Game SteamLauncher::parseAppManifest(const QString& manifestPath, const QString& libraryPath)
{
    Game game;
    VDFParser parser;

    if (!parser.parseFile(manifestPath)) {
        return game;
    }

    VDFNode root = parser.root();
    if (!root.hasChild("AppState")) {
        return game;
    }

    VDFNode appState = root.child("AppState");

    QString appId = appState.getString("appid");
    QString gameName = appState.getString("name");
    QString installDir = appState.getString("installdir");
    qint64 sizeOnDisk = appState.getInt("SizeOnDisk");
    int stateFlags = static_cast<int>(appState.getInt("StateFlags", 4));
    qint64 buildId = appState.getInt("buildid", 0);

    if (appId.isEmpty() || gameName.isEmpty()) {
        return game;
    }

    game.setId(appId);
    game.setName(gameName);
    // LauncherManager stamps this again centrally; using name() rather than a
    // literal keeps the two from ever disagreeing.
    game.setLauncher(name());
    game.setInstallPath(libraryPath + "/common/" + installDir);
    game.setSizeOnDisk(sizeOnDisk);
    game.setStateFlags(stateFlags);
    game.setNeedsUpdate(updatePending(stateFlags));
    game.setBuildId(buildId);
    game.setLibraryPath(libraryPath);

    // Detect if this is a native Linux game or Windows game running via Proton
    // Check for presence of .exe files - if found, it's a Windows game
    QString installPath = libraryPath + "/common/" + installDir;
    bool hasExeFiles = false;

    QDir gameDir(installPath);
    if (gameDir.exists()) {
        QDirIterator it(installPath, {"*.exe"}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            hasExeFiles = true;
        }
    }

    // Windows games have .exe files, native Linux games don't
    game.setIsNativeLinux(!hasExeFiles);

    // Steam CDN header image URL
    game.setImageUrl(QString("https://steamcdn-a.akamaihd.net/steam/apps/%1/header.jpg").arg(appId));

    return game;
}

QString SteamLauncher::getLaunchCommand(const Game& game, const DLSSSettings& settings)
{
    Q_UNUSED(game);
    return EnvBuilder::buildLaunchOptions(settings);
}

bool SteamLauncher::applySettings(const Game& game, const DLSSSettings& settings)
{
    QString launchOptions = getLaunchCommand(game, settings);
    return writeToLocalConfig(game.id(), launchOptions);
}

QString SteamLauncher::localConfigPath() const
{
    return SteamPaths::userDataPath();
}

namespace {

// --- editing localconfig.vdf in place --------------------------------------
//
// This is a surgical text edit rather than a parse-and-rewrite, and deliberately
// so: localconfig.vdf is Steam's file, it is large, and re-emitting it from a
// parse tree would mean taking responsibility for every key in it. Only the one
// value is touched and the rest of the bytes are left alone.
//
// It used to be done with the regex "\"<appid>\"\\s*\\{[^}]*\\}", which cannot
// work: a [^}]* body stops at the first closing brace, and real app sections
// contain nested blocks (BadgeData and friends) in no guaranteed order. Depending
// on where the nested block sat, the edit either landed inside it or the section
// was never found at all — and in the latter case the file was still rewritten
// and success still reported. Hence brace counting.

// Escape a value for a VDF quoted string, matching what VDFParser un-escapes.
QString vdfEscape(const QString& value)
{
    QString out;
    out.reserve(value.size() + 8);
    for (const QChar c : value) {
        switch (c.unicode()) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

// End of the quoted string that starts at `quote` (the index of its opening
// quote), i.e. the index of its closing quote. -1 when unterminated.
int endOfQuoted(const QString& text, int quote)
{
    for (int i = quote + 1; i < text.size(); ++i) {
        if (text.at(i) == '\\') {
            ++i;                    // skip the escaped character
            continue;
        }
        if (text.at(i) == '"') {
            return i;
        }
    }
    return -1;
}

// The '}' matching the '{' at `open`, skipping braces inside quoted strings.
// -1 when the block is unbalanced.
int matchingBrace(const QString& text, int open)
{
    int depth = 0;
    for (int i = open; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == '"') {
            const int end = endOfQuoted(text, i);
            if (end < 0) {
                return -1;
            }
            i = end;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (--depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

// Find the quoted key `key` at the top level of the block [from, to), i.e. not
// inside any nested block, comparing case-insensitively — Steam's key casing has
// varied between client versions. Returns the index of its opening quote, or -1.
int findKeyAtDepth(const QString& text, const QString& key, int from, int to)
{
    int depth = 0;
    for (int i = from; i < to && i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == '"') {
            const int end = endOfQuoted(text, i);
            if (end < 0) {
                return -1;
            }
            if (depth == 0
                && QStringView(text).mid(i + 1, end - i - 1)
                       .compare(key, Qt::CaseInsensitive) == 0) {
                return i;
            }
            i = end;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (depth == 0) {
                return -1;      // left the block we were searching
            }
            --depth;
        }
    }
    return -1;
}

// The block belonging to key `key` inside [from, to): returns its '{' and '}' in
// *open and *close. False when the key or its block is not there.
bool findBlock(const QString& text, const QString& key, int from, int to,
               int* open, int* close)
{
    const int keyQuote = findKeyAtDepth(text, key, from, to);
    if (keyQuote < 0) {
        return false;
    }
    const int keyEnd = endOfQuoted(text, keyQuote);
    if (keyEnd < 0) {
        return false;
    }
    int i = keyEnd + 1;
    while (i < to && text.at(i).isSpace()) {
        ++i;
    }
    if (i >= to || text.at(i) != '{') {
        return false;           // the key holds a value, not a block
    }
    const int end = matchingBrace(text, i);
    if (end < 0) {
        return false;
    }
    *open = i;
    *close = end;
    return true;
}

// Indentation of the line the given index sits on, so an inserted key lines up
// with its neighbours instead of standing out.
QString indentOf(const QString& text, int index)
{
    const int lineStart = text.lastIndexOf('\n', index) + 1;
    int i = lineStart;
    while (i < text.size() && (text.at(i) == '\t' || text.at(i) == ' ')) {
        ++i;
    }
    return text.mid(lineStart, i - lineStart);
}

} // namespace

bool SteamLauncher::writeToLocalConfig(const QString& appId, const QString& launchOptions)
{
    if (appId.isEmpty()) {
        return false;
    }

    QDir userDataDir(localConfigPath());
    if (!userDataDir.exists()) {
        return false;
    }

    const QStringList userDirs = userDataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (userDirs.isEmpty()) {
        return false;
    }

    // True only once a file has actually been changed. A run that finds nothing
    // to edit has to report failure — the caller is telling a user their settings
    // were applied to Steam, and there is no other way for it to find out.
    bool wrote = false;

    for (const QString& userId : userDirs) {
        const QString configPath = localConfigPath() + "/" + userId + "/config/localconfig.vdf";
        QFile file(configPath);
        if (!file.exists()) {
            continue;
        }
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning("SteamLauncher: cannot read %s", qUtf8Printable(configPath));
            continue;
        }
        QString content = QTextStream(&file).readAll();
        file.close();

        // Walk down to the apps block. Every level is matched case-insensitively
        // and only at its own depth, so a key of the same name elsewhere in the
        // file cannot be mistaken for it.
        int from = 0;
        int to = content.size();
        bool navigated = true;
        for (const QString& key : {QStringLiteral("UserLocalConfigStore"),
                                   QStringLiteral("Software"),
                                   QStringLiteral("Valve"),
                                   QStringLiteral("Steam"),
                                   QStringLiteral("apps")}) {
            int open = 0;
            int close = 0;
            if (!findBlock(content, key, from, to, &open, &close)) {
                qWarning("SteamLauncher: %s has no '%s' block",
                         qUtf8Printable(configPath), qUtf8Printable(key));
                navigated = false;
                break;
            }
            from = open + 1;
            to = close;
        }
        if (!navigated) {
            continue;
        }
        const int appsFrom = from;
        const int appsTo = to;

        const QString escaped = vdfEscape(launchOptions);

        int appOpen = 0;
        int appClose = 0;
        if (findBlock(content, appId, appsFrom, appsTo, &appOpen, &appClose)) {
            // The app has a section. Replace its LaunchOptions if it has one,
            // otherwise add one — in both cases at the section's own depth, so a
            // nested block cannot swallow the edit.
            const int keyQuote = findKeyAtDepth(content, QStringLiteral("LaunchOptions"),
                                                appOpen + 1, appClose);
            if (keyQuote >= 0) {
                const int keyEnd = endOfQuoted(content, keyQuote);
                int valueQuote = keyEnd + 1;
                while (valueQuote < appClose && content.at(valueQuote).isSpace()) {
                    ++valueQuote;
                }
                if (valueQuote >= appClose || content.at(valueQuote) != '"') {
                    qWarning("SteamLauncher: LaunchOptions for %s has no value",
                             qUtf8Printable(appId));
                    continue;
                }
                const int valueEnd = endOfQuoted(content, valueQuote);
                if (valueEnd < 0) {
                    continue;
                }
                content.replace(valueQuote, valueEnd - valueQuote + 1, "\"" + escaped + "\"");
            } else {
                const QString indent = indentOf(content, appOpen) + "\t";
                content.insert(appClose,
                               indent + "\"LaunchOptions\"\t\t\"" + escaped + "\"\n"
                                   + indentOf(content, appOpen));
            }
        } else {
            // No section for this app. Steam only writes one once there is
            // something to write, so this is the ordinary case for a game whose
            // launch options have never been set — it has to be created, not
            // silently skipped.
            const QString indent = indentOf(content, appsFrom > 0 ? appsFrom - 1 : 0) + "\t";
            const QString section = indent + "\"" + appId + "\"\n"
                                    + indent + "{\n"
                                    + indent + "\t\"LaunchOptions\"\t\t\"" + escaped + "\"\n"
                                    + indent + "}\n";
            content.insert(appsTo, section);
        }

        // Written through a temporary file and renamed over the original: this is
        // Steam's file, there is no backup, and a half-written localconfig.vdf
        // loses every game's settings rather than one game's.
        QSaveFile out(configPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning("SteamLauncher: cannot write %s", qUtf8Printable(configPath));
            continue;
        }
        QTextStream(&out) << content;
        if (!out.commit()) {
            qWarning("SteamLauncher: could not commit %s", qUtf8Printable(configPath));
            continue;
        }
        wrote = true;
    }

    return wrote;
}

namespace {
// localconfig.vdf key casing varies between Steam versions (e.g. "apps" vs
// "Apps", "valve" vs "Valve"), so navigate children case-insensitively.
VDFNode childCI(const VDFNode& node, const QString& key)
{
    if (node.hasChild(key)) {
        return node.child(key);
    }
    const QMap<QString, VDFNode> kids = node.children();
    for (auto it = kids.constBegin(); it != kids.constEnd(); ++it) {
        if (it.key().compare(key, Qt::CaseInsensitive) == 0) {
            return it.value();
        }
    }
    return VDFNode();
}

QString getStringCI(const VDFNode& node, const QString& key)
{
    if (node.hasChild(key)) {
        return node.getString(key);
    }
    const QMap<QString, VDFNode> kids = node.children();
    for (auto it = kids.constBegin(); it != kids.constEnd(); ++it) {
        if (it.key().compare(key, Qt::CaseInsensitive) == 0 && it.value().isValue()) {
            return it.value().value();
        }
    }
    return QString();
}
} // namespace

QString SteamLauncher::readLaunchOptions(const QString& appId)
{
    if (appId.isEmpty()) {
        return QString();
    }

    QDir userDataDir(SteamPaths::userDataPath());
    if (!userDataDir.exists()) {
        return QString();
    }

    const QStringList userDirs = userDataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& userId : userDirs) {
        const QString configPath = SteamPaths::userDataPath() + "/" + userId + "/config/localconfig.vdf";
        if (!QFile::exists(configPath)) {
            continue;
        }

        VDFParser parser;
        if (!parser.parseFile(configPath)) {
            continue;
        }

        // UserLocalConfigStore -> Software -> Valve -> Steam -> apps -> <appId> -> LaunchOptions
        VDFNode node = childCI(parser.root(), "UserLocalConfigStore");
        node = childCI(node, "Software");
        node = childCI(node, "Valve");
        node = childCI(node, "Steam");
        node = childCI(node, "apps");
        node = childCI(node, appId);

        const QString opts = getStringCI(node, "LaunchOptions");
        if (!opts.isEmpty()) {
            return opts;
        }
    }

    return QString();
}

bool SteamLauncher::checkUpdateStatus(Game& game)
{
    if (game.launcher() != "Steam" || game.libraryPath().isEmpty() || game.id().isEmpty()) {
        return false;
    }

    QString manifestPath = game.libraryPath() + "/appmanifest_" + game.id() + ".acf";

    VDFParser parser;
    if (!parser.parseFile(manifestPath)) {
        return false;
    }

    VDFNode root = parser.root();
    if (!root.hasChild("AppState")) {
        return false;
    }

    VDFNode appState = root.child("AppState");
    int newStateFlags = static_cast<int>(appState.getInt("StateFlags", 4));
    qint64 newBuildId = appState.getInt("buildid", 0);

    const bool changed = (newStateFlags != game.stateFlags()) || (newBuildId != game.buildId());

    // Always re-sync, even when nothing moved: needsUpdate is stored rather
    // than derived, so leaving it behind on the "unchanged" path is how it goes
    // stale. Callers act on the return value, not on whether we assigned.
    game.setStateFlags(newStateFlags);
    game.setBuildId(newBuildId);
    game.setNeedsUpdate(updatePending(newStateFlags));

    return changed;
}
