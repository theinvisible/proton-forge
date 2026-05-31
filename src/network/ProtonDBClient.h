#ifndef PROTONDBCLIENT_H
#define PROTONDBCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QList>
#include <QString>

// Fetches data from ProtonDB for a given Steam appId.
//
// Two layers:
//   * Summary  — the public endpoint
//     (https://www.protondb.com/api/v1/reports/summaries/<appid>.json).
//     Returns a tier/score only. Used for the tier badge.
//   * Reports  — per-game user reports including the free-text "concludingNotes"
//     and an explicit "launchOptions" field. ProtonDB serves these as static
//     JSON at /data/reports/all-devices/app/<gameId>.json, where <gameId> is an
//     obfuscated hash of (appId, counts.reports, counts.timestamp). Those two
//     salts come from /data/counts.json and change on every ProtonDB build (so
//     the gameId rotates per build). We therefore fetch counts.json at runtime,
//     recompute the gameId, and fetch the matching report file. If the report
//     file is missing (e.g. ProtonDB changed the hashing), reportsUnavailable()
//     is emitted and callers fall back to the tier badge + a deep link.
//
// Responses are cached on disk under ~/.cache/ProtonForge/protondb/ with a TTL.
class ProtonDBClient : public QObject {
    Q_OBJECT

public:
    static ProtonDBClient& instance();

    struct Summary {
        QString tier;          // e.g. "platinum", "gold", "pending"
        QString confidence;    // e.g. "strong", "good"
        double score = 0.0;
        int total = 0;
        bool valid = false;
    };

    struct Report {
        QString launchOptions;  // explicit launch-options field (highest value)
        QString notes;          // free-text user comment ("concludingNotes")
        QString gpuDriver;
        QString protonVersion;
        QString tier;
        qint64 timestamp = 0;
    };

    // Async fetch of the tier/score summary. Emits summaryReady or summaryFailed.
    void fetchSummary(const QString& appId);

    // Async fetch of user reports with notes. Emits reportsReady or
    // reportsUnavailable (when no source is configured or the fetch fails).
    void fetchReports(const QString& appId);

    // Public ProtonDB page for a game (for "View on ProtonDB" links).
    static QString appUrl(const QString& appId);

signals:
    void summaryReady(const QString& appId, const ProtonDBClient::Summary& summary);
    void summaryFailed(const QString& appId);
    void reportsReady(const QString& appId, const QList<ProtonDBClient::Report>& reports);
    void reportsUnavailable(const QString& appId, const QString& reason);

private:
    ProtonDBClient();
    ~ProtonDBClient() = default;
    ProtonDBClient(const ProtonDBClient&) = delete;
    ProtonDBClient& operator=(const ProtonDBClient&) = delete;

    static QString cacheDir();
    QString cacheFilePath(const QString& key, const QString& kind) const;
    bool loadCached(const QString& path, QByteArray& out, int maxAgeSecs) const;
    void saveCached(const QString& path, const QByteArray& data) const;

    // Fetch the report file for an already-computed gameId, parse, and emit.
    void fetchReportFile(const QString& appId, qint64 gameId);

    Summary parseSummary(const QByteArray& data) const;
    QList<Report> parseReports(const QByteArray& data) const;

    // Reproduces ProtonDB's client-side gameId derivation from a Steam appId and
    // the two salts in counts.json. See ProtonDBClient.cpp for the algorithm.
    static qint64 computeGameId(qint64 appId, qint64 reports, qint64 timestamp);

    QNetworkAccessManager* m_networkManager;
};

Q_DECLARE_METATYPE(ProtonDBClient::Summary)
Q_DECLARE_METATYPE(ProtonDBClient::Report)

#endif // PROTONDBCLIENT_H
