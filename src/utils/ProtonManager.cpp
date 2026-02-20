#include "ProtonManager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
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

bool ProtonManager::isProtonGEInstalled() const
{
    QDir dir(protonCachyOSPath());
    if (!dir.exists())
        return false;

    for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.startsWith("GE-Proton", Qt::CaseInsensitive)) {
            QString protonExe = protonCachyOSPath() + "/" + entry + "/proton";
            if (QFile::exists(protonExe))
                return true;
        }
    }
    return false;
}

QString ProtonManager::getInstalledGEVersion() const
{
    QDir dir(protonCachyOSPath());
    if (!dir.exists())
        return QString();

    QVersionNumber highest;
    QString highestName;

    for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.startsWith("GE-Proton", Qt::CaseInsensitive)) {
            QVersionNumber v = parseProtonGEVersion(entry);
            if (v > highest) {
                highest     = v;
                highestName = entry;
            }
        }
    }
    return highestName;
}

QString ProtonManager::getInstalledVersion() const
{
    QDir dir(protonCachyOSPath());
    if (!dir.exists()) {
        return QString();
    }

    QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QVersionNumber highestVersion;
    QString highestVersionString;

    QRegularExpression regex(R"(proton-cachyos-([0-9.]+)-(\d+))");

    for (const QString& entry : entries) {
        if (entry.startsWith("proton-cachyos", Qt::CaseInsensitive)) {
            // Extract version from directory name
            // Format: proton-cachyos-10.0-20260127-slr
            QRegularExpressionMatch match = regex.match(entry);
            if (match.hasMatch()) {
                QString versionStr = match.captured(1) + "-" + match.captured(2);
                QVersionNumber version = parseVersion(entry);

                if (version > highestVersion) {
                    highestVersion = version;
                    highestVersionString = versionStr;
                }
            }
        }
    }

    return highestVersionString;
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

void ProtonManager::checkForGEUpdates()
{
    QNetworkRequest request(
        QUrl("https://api.github.com/repos/GloriousEggroll/proton-ge-custom/releases/latest"));
    applyGitHubHeaders(request);

    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit geUpdateCheckComplete(false, QString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit geUpdateCheckComplete(false, QString());
            return;
        }

        ProtonRelease latest = parseProtonGEReleaseFromJson(doc.object());
        if (latest.downloadUrl.isEmpty()) {
            emit geUpdateCheckComplete(false, QString());
            return;
        }

        QString installedName = getInstalledGEVersion();
        bool updateAvailable  = false;

        if (!installedName.isEmpty()) {
            QVersionNumber installedVer = parseProtonGEVersion(installedName);
            updateAvailable = latest.versionNumber > installedVer;
        }

        emit geUpdateCheckComplete(updateAvailable, latest.version);
    });
}

void ProtonManager::fetchLatestRelease()
{
    QNetworkRequest request(QUrl("https://api.github.com/repos/CachyOS/proton-cachyos/releases/latest"));
    applyGitHubHeaders(request);

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
    release.type = ProtonCachyOS;
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

            // Extract version for display name
            QRegularExpression regex(R"(proton-cachyos-([0-9.]+)-(\d+))");
            QRegularExpressionMatch match = regex.match(name);
            if (match.hasMatch()) {
                release.displayName = QString("Proton-CachyOS %1 (%2)").arg(match.captured(1), match.captured(2));
            } else {
                release.displayName = QString("Proton-CachyOS %1").arg(release.version);
            }
            break;
        }
    }

    release.changelog = root["body"].toString();
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

// ---------------------------------------------------------------------------
// Extract a human-readable error from a GitHub API error response.
// GitHub always returns JSON with a "message" field on errors.
// ---------------------------------------------------------------------------
QString ProtonManager::extractApiError(QNetworkReply* reply)
{
    QByteArray body = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        QString msg = doc.object()["message"].toString();
        if (!msg.isEmpty())
            return msg;
    }
    return reply->errorString();
}

void ProtonManager::applyGitHubHeaders(QNetworkRequest& request, bool acceptJson) const
{
    if (acceptJson)
        request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setRawHeader("User-Agent", "ProtonForge");
    QSettings s;
    QString token = s.value("github/apiToken").toString().trimmed();
    if (!token.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
}

void ProtonManager::fetchAvailableVersions()
{
    // Fetch both CachyOS and GE releases
    m_pendingRequests = 2;
    m_pendingCachyOSReleases.clear();
    m_availableReleases.clear();
    m_lastFetchError.clear();

    fetchReleases(5);
    fetchProtonGEReleases(5);
}

void ProtonManager::fetchReleases(int count)
{
    // Fetch multiple releases from GitHub API (Proton-CachyOS)
    QString url = QString("https://api.github.com/repos/CachyOS/proton-cachyos/releases?per_page=%1").arg(count * 2);
    QNetworkRequest request{QUrl(url)};
    applyGitHubHeaders(request);

    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, count]() {
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            m_pendingCachyOSReleases = parseReleases(data, count);
        } else {
            m_lastFetchError = extractApiError(reply);
        }

        m_pendingRequests--;
        if (m_pendingRequests == 0) {
            // Combine CachyOS and GE releases
            m_availableReleases = m_pendingCachyOSReleases + m_availableReleases;
            emit availableVersionsFetched(m_availableReleases);
        }
    });
}

void ProtonManager::fetchProtonGEReleases(int count)
{
    // Fetch Proton-GE releases from GitHub API
    QString url = QString("https://api.github.com/repos/GloriousEggroll/proton-ge-custom/releases?per_page=%1").arg(count * 2);
    QNetworkRequest request{QUrl(url)};
    applyGitHubHeaders(request);

    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, count]() {
        reply->deleteLater();

        QList<ProtonRelease> geReleases;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            geReleases = parseProtonGEReleases(data, count);
        } else {
            m_lastFetchError = extractApiError(reply);
        }

        m_pendingRequests--;
        if (m_pendingRequests == 0) {
            // Combine CachyOS and GE releases
            m_availableReleases = m_pendingCachyOSReleases + geReleases;
            emit availableVersionsFetched(m_availableReleases);
        } else {
            // Store GE releases temporarily
            m_availableReleases = geReleases;
        }
    });
}

QList<ProtonManager::ProtonRelease> ProtonManager::parseProtonGEReleases(const QByteArray& jsonData, int maxCount)
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
        ProtonRelease release = parseProtonGEReleaseFromJson(releaseObj);

        if (!release.downloadUrl.isEmpty()) {
            releases.append(release);
            count++;
        }
    }

    return releases;
}

ProtonManager::ProtonRelease ProtonManager::parseProtonGEReleaseFromJson(const QJsonObject& root) const
{
    ProtonRelease release;
    release.type = ProtonGE;
    release.version = root["tag_name"].toString();

    // Find tar.gz asset
    QJsonArray assets = root["assets"].toArray();
    for (const QJsonValue& assetValue : assets) {
        QJsonObject asset = assetValue.toObject();
        QString name = asset["name"].toString();

        // Look for GE-Proton*.tar.gz file (not sha512sum)
        if (name.startsWith("GE-Proton") && name.endsWith(".tar.gz") && !name.contains("sha512sum")) {
            release.fileName = name;
            release.downloadUrl = asset["browser_download_url"].toString();
            release.versionNumber = parseProtonGEVersion(release.version);
            release.displayName = QString("Proton-GE %1").arg(release.version);
            break;
        }
    }

    release.changelog = root["body"].toString();
    return release;
}

QVersionNumber ProtonManager::parseProtonGEVersion(const QString& tagName) const
{
    // Parse version from tag like "GE-Proton9-20" or "GE-Proton8-25"
    QRegularExpression regex(R"(GE-Proton(\d+)-(\d+))");
    QRegularExpressionMatch match = regex.match(tagName);

    if (match.hasMatch()) {
        int major = match.captured(1).toInt();
        int minor = match.captured(2).toInt();
        return QVersionNumber(major, minor, 0);
    }

    return QVersionNumber();
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
    applyGitHubHeaders(request, false);

    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this, [this, release](qint64 received, qint64 total) {
        QString name = release.type == ProtonGE ? "Proton-GE" : "Proton-CachyOS";
        emit downloadProgress(received, total, name);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, filePath, release]() {
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
        extractArchive(filePath, release);
    });
}

void ProtonManager::extractArchive(const QString& archivePath, const ProtonRelease& release)
{
    // Ensure target directory exists
    QDir().mkpath(protonCachyOSPath());

    // Extract using tar
    QProcess* process = new QProcess(this);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, archivePath, release](int exitCode, QProcess::ExitStatus) {
        process->deleteLater();

        // Clean up downloaded archive
        QFile::remove(archivePath);

        if (exitCode == 0) {
            QString name = release.type == ProtonGE ? "Proton-GE" : "Proton-CachyOS";
            emit installationComplete(true, name + " installed successfully");
        } else {
            emit installationComplete(false, "Extraction failed: " + process->errorString());
        }
    });

    // Extract to compatibilitytools.d
    process->setWorkingDirectory(protonCachyOSPath());
    emit extractionStarted();
    process->start("tar", {"xf", archivePath});
}

bool ProtonManager::deleteProtonVersion(const ProtonRelease& release)
{
    // Extract directory name from fileName
    QString dirName = release.fileName;

    // Remove archive extensions
    if (dirName.endsWith(".tar.xz")) {
        dirName.chop(7);
    } else if (dirName.endsWith(".tar.gz")) {
        dirName.chop(7);
    }

    QString protonPath = protonCachyOSPath() + "/" + dirName;
    QDir protonDir(protonPath);

    if (!protonDir.exists()) {
        return false;
    }

    // Remove the directory and all its contents
    return protonDir.removeRecursively();
}
