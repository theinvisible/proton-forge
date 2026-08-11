#ifndef GAME_H
#define GAME_H

#include <QString>
#include <QStringList>
#include <QMetaType>

#include "core/LauncherTraits.h"

class Game {
public:
    Game() = default;
    Game(const QString& id, const QString& name, const QString& launcher);

    QString id() const { return m_id; }
    void setId(const QString& id) { m_id = id; }

    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    QString launcher() const { return m_launcher; }
    void setLauncher(const QString& launcher) { m_launcher = launcher; }

    // What this game's launcher needs from the rest of the app. Stamped at
    // discovery so nothing downstream has to compare launcher() to a literal.
    LauncherTraits traits() const { return m_traits; }
    void setTraits(const LauncherTraits& traits) { m_traits = traits; }

    QString installPath() const { return m_installPath; }
    void setInstallPath(const QString& path) { m_installPath = path; }

    QString executablePath() const { return m_executablePath; }
    void setExecutablePath(const QString& path) { m_executablePath = path; }

    // Where the game wants to be started from. Falls back to the executable's
    // own directory at launch time; a launcher only sets this when it knows
    // better, which for GOG's playTasks is often the case.
    QString workingDirectory() const { return m_workingDirectory; }
    void setWorkingDirectory(const QString& dir) { m_workingDirectory = dir; }

    // Arguments the launcher says the game needs, already split. They go before
    // any the user added, so the user's still win by coming last.
    QStringList launchArgs() const { return m_launchArgs; }
    void setLaunchArgs(const QStringList& args) { m_launchArgs = args; }

    qint64 sizeOnDisk() const { return m_sizeOnDisk; }
    void setSizeOnDisk(qint64 size) { m_sizeOnDisk = size; }

    QString imageUrl() const { return m_imageUrl; }
    void setImageUrl(const QString& url) { m_imageUrl = url; }

    QString libraryPath() const { return m_libraryPath; }
    void setLibraryPath(const QString& path) { m_libraryPath = path; }

    bool isNativeLinux() const { return m_isNativeLinux; }
    void setIsNativeLinux(bool isNative) { m_isNativeLinux = isNative; }

    int stateFlags() const { return m_stateFlags; }
    void setStateFlags(int flags) { m_stateFlags = flags; }

    qint64 buildId() const { return m_buildId; }
    void setBuildId(qint64 id) { m_buildId = id; }

    // Free-form version string. Steam has a numeric buildId; other stores hand
    // out opaque strings, so the two are kept apart rather than coerced.
    QString version() const { return m_version; }
    void setVersion(const QString& version) { m_version = version; }

    // Stored, not derived. It used to be (stateFlags & 2), which is Steam's ACF
    // bitmask and means nothing to any other launcher.
    bool needsUpdate() const { return m_needsUpdate; }
    void setNeedsUpdate(bool needsUpdate) { m_needsUpdate = needsUpdate; }

    // Notes from whoever installed this game about what it may still need —
    // GOG's Windows redistributables, which Proton usually but not always
    // provides. Empty for every launcher that only reads what is already there.
    QStringList installWarnings() const { return m_installWarnings; }
    void setInstallWarnings(const QStringList& warnings) { m_installWarnings = warnings; }

    // The Proton prefix (STEAM_COMPAT_DATA_PATH) and the shader cache. Both are
    // explicit when the launcher knows where it put them, and otherwise derived
    // from Steam's library layout. See Game.cpp for why empty is a valid answer.
    QString compatDataPath() const;
    void setCompatDataPath(const QString& path) { m_compatDataPath = path; }

    QString shaderCachePath() const;
    void setShaderCachePath(const QString& path) { m_shaderCachePath = path; }

    // Unique key for settings lookup
    QString settingsKey() const;

    bool operator==(const Game& other) const;

private:
    QString m_id;
    QString m_name;
    QString m_launcher;
    LauncherTraits m_traits;
    QString m_installPath;
    QString m_executablePath;
    QString m_workingDirectory;
    QStringList m_launchArgs;
    qint64 m_sizeOnDisk = 0;
    QString m_imageUrl;
    QString m_libraryPath;
    QStringList m_installWarnings;
    QString m_compatDataPath;
    QString m_shaderCachePath;
    bool m_isNativeLinux = false;
    int m_stateFlags = 4;      // Default: StateFullyInstalled
    qint64 m_buildId = 0;
    QString m_version;
    bool m_needsUpdate = false;
};

Q_DECLARE_METATYPE(Game)

#endif // GAME_H
