#ifndef PROTONMANAGER_H
#define PROTONMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QVersionNumber>

class ProtonManager : public QObject {
    Q_OBJECT

public:
    static ProtonManager& instance();

    struct ProtonRelease {
        QString version;
        QString downloadUrl;
        QString fileName;
        QVersionNumber versionNumber;
    };

    // Check if proton-cachyos is installed
    bool isProtonCachyOSInstalled() const;

    // Get currently installed version
    QString getInstalledVersion() const;

    // Check for available updates
    void checkForUpdates();

    // Download and install proton-cachyos
    void installProtonCachyOS();

    // Update to latest version
    void updateProtonCachyOS();

    // Get installation directory
    static QString protonCachyOSPath();

signals:
    void updateCheckComplete(bool updateAvailable, const QString& latestVersion);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void installationComplete(bool success, const QString& message);
    void installationStarted();

private:
    ProtonManager();
    ~ProtonManager() = default;
    ProtonManager(const ProtonManager&) = delete;
    ProtonManager& operator=(const ProtonManager&) = delete;

    void fetchLatestRelease();
    void downloadRelease(const ProtonRelease& release);
    void extractArchive(const QString& archivePath);
    ProtonRelease parseLatestRelease(const QByteArray& jsonData);
    QVersionNumber parseVersion(const QString& fileName) const;

    QNetworkAccessManager* m_networkManager;
    ProtonRelease m_latestRelease;
    QString m_downloadPath;
};

#endif // PROTONMANAGER_H
