#ifndef GAME_H
#define GAME_H

#include <QString>
#include <QMetaType>

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

    QString installPath() const { return m_installPath; }
    void setInstallPath(const QString& path) { m_installPath = path; }

    QString executablePath() const { return m_executablePath; }
    void setExecutablePath(const QString& path) { m_executablePath = path; }

    qint64 sizeOnDisk() const { return m_sizeOnDisk; }
    void setSizeOnDisk(qint64 size) { m_sizeOnDisk = size; }

    QString imageUrl() const { return m_imageUrl; }
    void setImageUrl(const QString& url) { m_imageUrl = url; }

    QString libraryPath() const { return m_libraryPath; }
    void setLibraryPath(const QString& path) { m_libraryPath = path; }

    bool isNativeLinux() const { return m_isNativeLinux; }
    void setIsNativeLinux(bool isNative) { m_isNativeLinux = isNative; }

    // Unique key for settings lookup
    QString settingsKey() const;

    bool operator==(const Game& other) const;

private:
    QString m_id;
    QString m_name;
    QString m_launcher;
    QString m_installPath;
    QString m_executablePath;
    qint64 m_sizeOnDisk = 0;
    QString m_imageUrl;
    QString m_libraryPath;
    bool m_isNativeLinux = false;
};

Q_DECLARE_METATYPE(Game)

#endif // GAME_H
