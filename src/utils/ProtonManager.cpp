#include "ProtonManager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>

ProtonManager& ProtonManager::instance()
{
    static ProtonManager instance;
    return instance;
}

ProtonManager::ProtonManager()
    : m_networkManager(new QNetworkAccessManager(this))
{
    m_downloadPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    // Register metatype for use in QVariant
    qRegisterMetaType<ProtonRelease>("ProtonRelease");
    qRegisterMetaType<ProtonRelease>("ProtonManager::ProtonRelease");
}

QString ProtonManager::protonCachyOSPath()
{
    return QDir::homePath() + "/.steam/root/compatibilitytools.d";
}

bool ProtonManager::isProtonCachyOSInstalled() const
{
    QDir dir(protonCachyOSPath());
    if (!dir.exists()) {
        return false;
    }

    QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        if (entry.startsWith("proton-cachyos", Qt::CaseInsensitive)) {
            // Verify it has a proton executable
            QString protonExe = protonCachyOSPath() + "/" + entry + "/proton";
            return QFile::exists(protonExe);
        }
    }

    return false;
}

QString ProtonManager::getInstalledVersion() const
{
    QDir dir(protonCachyOSPath());
    if (!dir.exists()) {
        return QString();
    }

    QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        if (entry.startsWith("proton-cachyos", Qt::CaseInsensitive)) {
            // Extract version from directory name
            // Format: proton-cachyos-10.0-20260127-slr
            QRegularExpression regex(R"(proton-cachyos-([0-9.]+)-(\d+))");
            QRegularExpressionMatch match = regex.match(entry);
            if (match.hasMatch()) {
                return match.captured(1) + "-" + match.captured(2);
            }
            return entry;
        }
    }

    return QString();
}

QVersionNumber ProtonManager::parseVersion(const QString& fileName) const
{
    // Extract version from filename like: proton-cachyos-10.0-20260127-slr-x86_64.tar.xz
    QRegularExpression regex(R"(proton-cachyos-([0-9]+)\.([0-9]+)-(\d+))");
    QRegularExpressionMatch match = regex.match(fileName);

    if (match.hasMatch()) {
        int major = match.captured(1).toInt();
        int minor = match.captured(2).toInt();
        int patch = match.captured(3).toInt();
        return QVersionNumber(major, minor, patch);
    }

    return QVersionNumber();
}

void ProtonManager::checkForUpdates()
{
    fetchLatestRelease();
}

void ProtonManager::fetchLatestRelease()
{
    QNetworkRequest request(QUrl("https://api.github.com/repos/CachyOS/proton-cachyos/releases/latest"));
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setRawHeader("User-Agent", "NvidiaAppLinux");

    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit updateCheckComplete(false, QString());
            return;
        }

        QByteArray data = reply->readAll();
        m_latestRelease = parseLatestRelease(data);

        if (m_latestRelease.downloadUrl.isEmpty()) {
            emit updateCheckComplete(false, QString());
            return;
        }

        // Check if update is available
        QString installedVersion = getInstalledVersion();
        bool updateAvailable = false;

        if (installedVersion.isEmpty()) {
            // Not installed, update available
            updateAvailable = true;
        } else {
            // Compare versions
            QString installedFileName = "proton-cachyos-" + installedVersion;
            QVersionNumber installedVer = parseVersion(installedFileName);

            if (m_latestRelease.versionNumber > installedVer) {
                updateAvailable = true;
            }
        }

        emit updateCheckComplete(updateAvailable, m_latestRelease.version);
    });
}

ProtonManager::ProtonRelease ProtonManager::parseLatestRelease(const QByteArray& jsonData)
{
    ProtonRelease release;

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isObject()) {
        return release;
    }

    QJsonObject root = doc.object();
    return parseReleaseFromJson(root);
}

ProtonManager::ProtonRelease ProtonManager::parseReleaseFromJson(const QJsonObject& root) const
{
    ProtonRelease release;
    release.version = root["tag_name"].toString();

    // Find x86_64 tar.xz asset
    QJsonArray assets = root["assets"].toArray();
    for (const QJsonValue& assetValue : assets) {
        QJsonObject asset = assetValue.toObject();
        QString name = asset["name"].toString();

        // Look for x86_64 tar.xz file
        if (name.contains("x86_64") && name.endsWith(".tar.xz") &&
            name.startsWith("proton-cachyos")) {
            release.fileName = name;
            release.downloadUrl = asset["browser_download_url"].toString();
            release.versionNumber = parseVersion(name);
            break;
        }
    }

    return release;
}

QList<ProtonManager::ProtonRelease> ProtonManager::parseReleases(const QByteArray& jsonData, int maxCount)
{
    QList<ProtonRelease> releases;

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isArray()) {
        return releases;
    }

    QJsonArray releasesArray = doc.array();
    int count = 0;

    for (const QJsonValue& releaseValue : releasesArray) {
        if (count >= maxCount) {
            break;
        }

        QJsonObject releaseObj = releaseValue.toObject();
        ProtonRelease release = parseReleaseFromJson(releaseObj);

        // Only add if we found a valid x86_64 package
        if (!release.downloadUrl.isEmpty()) {
            releases.append(release);
            count++;
        }
    }

    return releases;
}

void ProtonManager::fetchAvailableVersions()
{
    fetchReleases(5);
}

void ProtonManager::fetchReleases(int count)
{
    // Fetch multiple releases from GitHub API
    QString url = QString("https://api.github.com/repos/CachyOS/proton-cachyos/releases?per_page=%1").arg(count * 2); // Fetch extra in case some don't have x86_64 packages
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setRawHeader("User-Agent", "NvidiaAppLinux");

    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, count]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit availableVersionsFetched(QList<ProtonRelease>());
            return;
        }

        QByteArray data = reply->readAll();
        m_availableReleases = parseReleases(data, count);

        emit availableVersionsFetched(m_availableReleases);
    });
}

void ProtonManager::installProtonCachyOS()
{
    if (m_latestRelease.downloadUrl.isEmpty()) {
        // Fetch latest release first
        connect(this, &ProtonManager::updateCheckComplete, this,
                [this](bool available, const QString&) {
            if (available) {
                downloadRelease(m_latestRelease);
            }
        }, Qt::SingleShotConnection);

        fetchLatestRelease();
    } else {
        downloadRelease(m_latestRelease);
    }
}

void ProtonManager::installProtonCachyOS(const ProtonRelease& release)
{
    if (!release.downloadUrl.isEmpty()) {
        downloadRelease(release);
    }
}

void ProtonManager::updateProtonCachyOS()
{
    installProtonCachyOS();
}

void ProtonManager::downloadRelease(const ProtonRelease& release)
{
    emit installationStarted();

    QString filePath = m_downloadPath + "/" + release.fileName;

    QNetworkRequest request(QUrl(release.downloadUrl));
    request.setRawHeader("User-Agent", "NvidiaAppLinux");

    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit downloadProgress(received, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, filePath]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit installationComplete(false, "Download failed: " + reply->errorString());
            return;
        }

        // Save file
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            emit installationComplete(false, "Cannot write to: " + filePath);
            return;
        }

        file.write(reply->readAll());
        file.close();

        // Extract archive
        extractArchive(filePath);
    });
}

void ProtonManager::extractArchive(const QString& archivePath)
{
    // Ensure target directory exists
    QDir().mkpath(protonCachyOSPath());

    // Extract using tar
    QProcess* process = new QProcess(this);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, archivePath](int exitCode, QProcess::ExitStatus) {
        process->deleteLater();

        // Clean up downloaded archive
        QFile::remove(archivePath);

        if (exitCode == 0) {
            emit installationComplete(true, "Proton-CachyOS installed successfully");
        } else {
            emit installationComplete(false, "Extraction failed: " + process->errorString());
        }
    });

    // Extract to compatibilitytools.d
    process->setWorkingDirectory(protonCachyOSPath());
    process->start("tar", {"xf", archivePath});
}
