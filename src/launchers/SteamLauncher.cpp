#include "SteamLauncher.h"
#include "parsers/VDFParser.h"
#include "utils/EnvBuilder.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QDirIterator>

SteamLauncher::SteamLauncher()
{
}

bool SteamLauncher::isAvailable() const
{
    return QDir(steamPath()).exists();
}

QString SteamLauncher::steamPath()
{
    // Check common Steam installation paths
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

QString SteamLauncher::steamAppsPath()
{
    return steamPath() + "/steamapps";
}

QStringList SteamLauncher::libraryPaths()
{
    QStringList paths;

    // Always include default path
    QString defaultPath = steamAppsPath();
    if (QDir(defaultPath).exists()) {
        paths << defaultPath;
    }

    // Parse libraryfolders.vdf for additional library folders
    QString libraryFoldersPath = steamAppsPath() + "/libraryfolders.vdf";
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
    QString name = appState.getString("name");
    QString installDir = appState.getString("installdir");
    qint64 sizeOnDisk = appState.getInt("SizeOnDisk");

    if (appId.isEmpty() || name.isEmpty()) {
        return game;
    }

    game.setId(appId);
    game.setName(name);
    game.setLauncher("Steam");
    game.setInstallPath(libraryPath + "/common/" + installDir);
    game.setSizeOnDisk(sizeOnDisk);
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
    return steamPath() + "/userdata";
}

bool SteamLauncher::writeToLocalConfig(const QString& appId, const QString& launchOptions)
{
    // Find user directories in userdata
    QDir userDataDir(localConfigPath());
    if (!userDataDir.exists()) {
        return false;
    }

    QStringList userDirs = userDataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (userDirs.isEmpty()) {
        return false;
    }

    bool success = false;

    for (const QString& userId : userDirs) {
        QString configPath = localConfigPath() + "/" + userId + "/config/localconfig.vdf";
        QFile file(configPath);

        if (!file.exists()) {
            continue;
        }

        // Read existing config
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QString content = QTextStream(&file).readAll();
        file.close();

        // Find or create the app entry with LaunchOptions
        // This is a simplified approach - a full implementation would properly parse and modify VDF
        QString pattern = QString("\"%1\"\\s*\\{[^}]*\\}").arg(appId);
        QRegularExpression appRegex(pattern);
        QRegularExpressionMatch match = appRegex.match(content);

        if (match.hasMatch()) {
            // App entry exists, update LaunchOptions
            QString appSection = match.captured(0);

            // Check if LaunchOptions exists
            QRegularExpression launchRegex("\"LaunchOptions\"\\s*\"[^\"]*\"");
            if (launchRegex.match(appSection).hasMatch()) {
                // Update existing LaunchOptions
                QString replacement = QString("\"LaunchOptions\"\t\t\"%1\"").arg(launchOptions);
                appSection.replace(launchRegex, replacement);
            } else {
                // Add LaunchOptions before closing brace
                int lastBrace = appSection.lastIndexOf('}');
                QString insertion = QString("\n\t\t\t\t\t\"LaunchOptions\"\t\t\"%1\"\n\t\t\t\t").arg(launchOptions);
                appSection.insert(lastBrace, insertion);
            }

            content.replace(match.captured(0), appSection);
        }

        // Write back
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream(&file) << content;
            file.close();
            success = true;
        }
    }

    return success;
}
