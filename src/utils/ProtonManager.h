#ifndef PROTONMANAGER_H
#define PROTONMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QVersionNumber>
#include <QList>
#include <QJsonObject>

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

    // Fetch available versions (latest 5)
    void fetchAvailableVersions();

    // Download and install proton-cachyos
    void installProtonCachyOS();
    void installProtonCachyOS(const ProtonRelease& release);

    // Update to latest version
    void updateProtonCachyOS();

    // Get installation directory
    static QString protonCachyOSPath();

    // Get available releases
    QList<ProtonRelease> availableReleases() const { return m_availableReleases; }

signals:
    void updateCheckComplete(bool updateAvailable, const QString& latestVersion);
    void availableVersionsFetched(const QList<ProtonRelease>& releases);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void installationComplete(bool success, const QString& message);
    void installationStarted();

private:
    ProtonManager();
    ~ProtonManager() = default;
    ProtonManager(const ProtonManager&) = delete;
    ProtonManager& operator=(const ProtonManager&) = delete;

    void fetchLatestRelease();
    void fetchReleases(int count = 5);
    void downloadRelease(const ProtonRelease& release);
    void extractArchive(const QString& archivePath);
    ProtonRelease parseLatestRelease(const QByteArray& jsonData);
    QList<ProtonRelease> parseReleases(const QByteArray& jsonData, int maxCount = 5);
    ProtonRelease parseReleaseFromJson(const QJsonObject& releaseObj) const;
    QVersionNumber parseVersion(const QString& fileName) const;

    QNetworkAccessManager* m_networkManager;
    ProtonRelease m_latestRelease;
    QList<ProtonRelease> m_availableReleases;
    QString m_downloadPath;
};

Q_DECLARE_METATYPE(ProtonManager::ProtonRelease)

#endif // PROTONMANAGER_H
