#include "ProtonDBClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
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

namespace {
// --- ProtonDB gameId derivation -------------------------------------------
// Reproduced from ProtonDB's web bundle. The site computes a report "gameId"
// from the Steam appId and two salts (counts.reports, counts.timestamp):
//
//   R(e,t,n) = `${t}p${e*(t%n)}`
//   I(s)     = abs( reduce over (s+"m"): acc = (acc<<5) - acc + charCode )   // int32 wrap
//   gameId   = I("p" + R(appId,reports,ts) + "*vRT" + R(1,appId,ts) + "undefined")
//
// Verified against many live games (e.g. Elden Ring 1245620 -> 2033117161).
QString protonR(qint64 e, qint64 t, qint64 n)
{
    const qint64 prod = (n != 0) ? e * (t % n) : 0;
    return QString::number(t) + "p" + QString::number(prod);
}

qint32 protonI(const QString& in)
{
    const QString s = in + "m";
    quint32 acc = 0;  // 32-bit wraparound mirrors JS's "|0"
    for (const QChar ch : s) {
        acc = (acc << 5) - acc + ch.unicode();
    }
    return static_cast<qint32>(acc);
}
} // namespace

qint64 ProtonDBClient::computeGameId(qint64 appId, qint64 reports, qint64 timestamp)
{
    const QString s = "p" + protonR(appId, reports, timestamp)
                    + "*vRT" + protonR(1, appId, timestamp) + "undefined";
    return qAbs(static_cast<qint64>(protonI(s)));
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
    bool isNumeric = false;
    const qint64 appIdNum = appId.toLongLong(&isNumeric);
    if (appId.isEmpty() || !isNumeric) {
        emit reportsUnavailable(appId, "No Steam game selected.");
        return;
    }

    // Step 1: fetch counts.json fresh — it carries the per-build salts that the
    // gameId depends on, so caching it could desync from the current report files.
    QNetworkRequest request(QUrl("https://www.protondb.com/data/counts.json"));
    request.setRawHeader("User-Agent", "ProtonForge");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId, appIdNum]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit reportsUnavailable(appId, "Could not reach ProtonDB: " + reply->errorString());
            return;
        }
        const QJsonObject counts = QJsonDocument::fromJson(reply->readAll()).object();
        const qint64 reports = counts.value("reports").toVariant().toLongLong();
        const qint64 timestamp = counts.value("timestamp").toVariant().toLongLong();
        if (reports == 0 || timestamp == 0) {
            emit reportsUnavailable(appId, "ProtonDB returned unexpected data.");
            return;
        }
        fetchReportFile(appId, computeGameId(appIdNum, reports, timestamp));
    });
}

void ProtonDBClient::fetchReportFile(const QString& appId, qint64 gameId)
{
    // Report files are keyed by the (per-build) gameId, so the cache key is too.
    const QString cachePath = cacheFilePath(QString::number(gameId), "reports");
    QByteArray cached;
    if (loadCached(cachePath, cached, kCacheTtlSecs)) {
        const QList<Report> reports = parseReports(cached);
        if (!reports.isEmpty()) {
            emit reportsReady(appId, reports);
            return;
        }
    }

    const QString url = QStringLiteral(
        "https://www.protondb.com/data/reports/all-devices/app/%1.json").arg(gameId);
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
                "No ProtonDB reports are available for this game (or ProtonDB changed "
                "its data layout). The tier and a link to ProtonDB are still available.");
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
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const QJsonArray array = root.value("reports").toArray();

    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        const QJsonObject responses = obj.value("responses").toObject();

        Report report;
        report.launchOptions = responses.value("launchOptions").toString().trimmed();
        report.notes = responses.value("concludingNotes").toString().trimmed();

        // Prefer a user-entered custom Proton version, else the selected one.
        report.protonVersion = responses.value("customProtonVersion").toString().trimmed();
        if (report.protonVersion.isEmpty()) {
            report.protonVersion = responses.value("protonVersion").toString().trimmed();
        }

        const QJsonObject steam = obj.value("device").toObject()
                                     .value("inferred").toObject()
                                     .value("steam").toObject();
        report.gpuDriver = steam.value("gpuDriver").toString().trimmed();
        report.timestamp = obj.value("timestamp").toVariant().toLongLong();

        // Keep reports that carry something usable for mining.
        if (!report.launchOptions.isEmpty() || !report.notes.isEmpty()) {
            reports << report;
        }
    }

    return reports;
}
