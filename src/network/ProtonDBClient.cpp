#include "ProtonDBClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUrl>

namespace {
constexpr int kCacheTtlSecs = 60 * 60 * 24;  // 24h — ProtonDB data changes slowly
}

ProtonDBClient& ProtonDBClient::instance()
{
    static ProtonDBClient instance;
    return instance;
}

ProtonDBClient::ProtonDBClient()
    : m_networkManager(new QNetworkAccessManager(this))
{
    QDir().mkpath(cacheDir());
}

QString ProtonDBClient::appUrl(const QString& appId)
{
    return QStringLiteral("https://www.protondb.com/app/%1").arg(appId);
}

QString ProtonDBClient::cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/ProtonForge/protondb";
}

QString ProtonDBClient::cacheFilePath(const QString& appId, const QString& kind) const
{
    return cacheDir() + "/" + appId + "-" + kind + ".json";
}

bool ProtonDBClient::loadCached(const QString& path, QByteArray& out, int maxAgeSecs) const
{
    QFileInfo info(path);
    if (!info.exists()) {
        return false;
    }
    if (info.lastModified().secsTo(QDateTime::currentDateTime()) > maxAgeSecs) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    out = file.readAll();
    return !out.isEmpty();
}

void ProtonDBClient::saveCached(const QString& path, const QByteArray& data) const
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
    }
}

QString ProtonDBClient::reportsBaseUrl() const
{
    // Empty by default: ProtonDB exposes no reliable public per-game reports
    // endpoint. A user/maintainer can point this at a compatible source (e.g. a
    // self-hosted ProtonDB community API). The appId is appended to this base.
    QSettings s;
    return s.value("protondb/reportsBaseUrl").toString().trimmed();
}

void ProtonDBClient::fetchSummary(const QString& appId)
{
    if (appId.isEmpty()) {
        emit summaryFailed(appId);
        return;
    }

    const QString cachePath = cacheFilePath(appId, "summary");
    QByteArray cached;
    if (loadCached(cachePath, cached, kCacheTtlSecs)) {
        Summary summary = parseSummary(cached);
        if (summary.valid) {
            emit summaryReady(appId, summary);
            return;
        }
    }

    QNetworkRequest request(QUrl(
        QStringLiteral("https://www.protondb.com/api/v1/reports/summaries/%1.json").arg(appId)));
    request.setRawHeader("User-Agent", "ProtonForge");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId, cachePath]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit summaryFailed(appId);
            return;
        }
        const QByteArray data = reply->readAll();
        Summary summary = parseSummary(data);
        if (!summary.valid) {
            emit summaryFailed(appId);
            return;
        }
        saveCached(cachePath, data);
        emit summaryReady(appId, summary);
    });
}

void ProtonDBClient::fetchReports(const QString& appId)
{
    if (appId.isEmpty()) {
        emit reportsUnavailable(appId, "No game selected.");
        return;
    }

    const QString base = reportsBaseUrl();
    if (base.isEmpty()) {
        emit reportsUnavailable(appId,
            "No ProtonDB reports source is configured. ProtonDB does not expose a "
            "public endpoint for report text, so launch-option mining is disabled "
            "until a reports source is set (protondb/reportsBaseUrl). You can still "
            "read reports directly on ProtonDB.");
        return;
    }

    const QString cachePath = cacheFilePath(appId, "reports");
    QByteArray cached;
    if (loadCached(cachePath, cached, kCacheTtlSecs)) {
        const QList<Report> reports = parseReports(cached);
        if (!reports.isEmpty()) {
            emit reportsReady(appId, reports);
            return;
        }
    }

    QString url = base;
    if (!url.endsWith('/')) {
        url += '/';
    }
    url += appId;

    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", "ProtonForge");
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId, cachePath]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit reportsUnavailable(appId,
                "Could not reach the configured ProtonDB reports source: " + reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        const QList<Report> reports = parseReports(data);
        if (reports.isEmpty()) {
            emit reportsUnavailable(appId, "No usable reports were returned for this game.");
            return;
        }
        saveCached(cachePath, data);
        emit reportsReady(appId, reports);
    });
}

ProtonDBClient::Summary ProtonDBClient::parseSummary(const QByteArray& data) const
{
    Summary summary;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return summary;
    }
    QJsonObject root = doc.object();
    if (!root.contains("tier")) {
        return summary;
    }
    summary.tier = root.value("tier").toString();
    summary.confidence = root.value("confidence").toString();
    summary.score = root.value("score").toDouble();
    summary.total = root.value("total").toInt();
    summary.valid = !summary.tier.isEmpty();
    return summary;
}

QList<ProtonDBClient::Report> ProtonDBClient::parseReports(const QByteArray& data) const
{
    QList<Report> reports;
    QJsonDocument doc = QJsonDocument::fromJson(data);

    QJsonArray array;
    if (doc.isArray()) {
        array = doc.array();
    } else if (doc.isObject()) {
        // Accept {"reports": [...]} as well.
        array = doc.object().value("reports").toArray();
    }

    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject obj = value.toObject();

        Report report;
        // Tolerate a few common field-name variants across community sources.
        report.notes = obj.value("notes").toString();
        if (report.notes.isEmpty()) {
            report.notes = obj.value("note").toString();
        }
        report.gpuDriver = obj.value("gpuDriver").toString();
        if (report.gpuDriver.isEmpty()) {
            report.gpuDriver = obj.value("gpu_driver").toString();
        }
        report.protonVersion = obj.value("protonVersion").toString();
        if (report.protonVersion.isEmpty()) {
            report.protonVersion = obj.value("proton_version").toString();
        }
        report.tier = obj.value("tier").toString();
        report.timestamp = obj.value("timestamp").toVariant().toLongLong();

        if (!report.notes.trimmed().isEmpty()) {
            reports << report;
        }
    }

    return reports;
}
