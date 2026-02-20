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

    enum ProtonType {
        ProtonCachyOS,
        ProtonGE
    };

    struct ProtonRelease {
        QString version;
        QString downloadUrl;
        QString fileName;
        QVersionNumber versionNumber;
        ProtonType type = ProtonCachyOS;
        QString displayName;  // Human-readable name
        QString changelog;    // body field from GitHub API
    };

    // Check if proton-cachyos is installed
    bool isProtonCachyOSInstalled() const;

    // Check if any Proton-GE version is installed
    bool isProtonGEInstalled() const;

    // Get currently installed CachyOS version
    QString getInstalledVersion() const;

    // Get highest installed Proton-GE directory name (e.g. "GE-Proton9-20")
    QString getInstalledGEVersion() const;

    // Check for available updates
    void checkForUpdates();

    // Check for Proton-GE updates (only meaningful when GE is installed)
    void checkForGEUpdates();

    // Fetch available versions (latest 5)
    void fetchAvailableVersions();

    // Download and install proton-cachyos
    void installProtonCachyOS();
    void installProtonCachyOS(const ProtonRelease& release);

    // Update to latest version
    void updateProtonCachyOS();

    // Delete a specific Proton version
    bool deleteProtonVersion(const ProtonRelease& release);

    // Get installation directory
    static QString protonCachyOSPath();

    // Get available releases
    QList<ProtonRelease> availableReleases() const { return m_availableReleases; }

signals:
    void updateCheckComplete(bool updateAvailable, const QString& latestVersion);
    void geUpdateCheckComplete(bool updateAvailable, const QString& latestVersion);
    void availableVersionsFetched(const QList<ProtonRelease>& releases);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal, const QString& protonName);
    void installationComplete(bool success, const QString& message);
    void installationStarted();
    void extractionStarted();

private:
    ProtonManager();
    ~ProtonManager() = default;
    ProtonManager(const ProtonManager&) = delete;
    ProtonManager& operator=(const ProtonManager&) = delete;

    void fetchLatestRelease();
    void fetchReleases(int count = 5);
    void fetchProtonGEReleases(int count = 5);
    void downloadRelease(const ProtonRelease& release);
    void extractArchive(const QString& archivePath, const ProtonRelease& release);
    ProtonRelease parseLatestRelease(const QByteArray& jsonData);
    QList<ProtonRelease> parseReleases(const QByteArray& jsonData, int maxCount = 5);
    QList<ProtonRelease> parseProtonGEReleases(const QByteArray& jsonData, int maxCount = 5);
    ProtonRelease parseReleaseFromJson(const QJsonObject& releaseObj) const;
    ProtonRelease parseProtonGEReleaseFromJson(const QJsonObject& releaseObj) const;
    QVersionNumber parseVersion(const QString& fileName) const;
    QVersionNumber parseProtonGEVersion(const QString& tagName) const;

    QNetworkAccessManager* m_networkManager;
    ProtonRelease m_latestRelease;
    QList<ProtonRelease> m_availableReleases;
    QList<ProtonRelease> m_pendingCachyOSReleases;
    QString m_downloadPath;
    int m_pendingRequests = 0;
};

Q_DECLARE_METATYPE(ProtonManager::ProtonRelease)

#endif // PROTONMANAGER_H
