#ifndef PROTONDBCLIENT_H
#define PROTONDBCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QList>
#include <QString>

// Fetches data from ProtonDB for a given Steam appId.
//
// Two layers, by reliability:
//   * Summary  — the official public endpoint
//     (https://www.protondb.com/api/v1/reports/summaries/<appid>.json).
//     Returns a tier/score only. Always works; used for the tier badge.
//   * Reports  — per-game user reports including free-text notes. ProtonDB has
//     no reliable public endpoint for these, so the source is configurable via
//     the QSettings key "protondb/reportsBaseUrl". When unset (the default) or
//     unreachable, reportsUnavailable() is emitted and callers fall back to the
//     tier badge + a deep link. The expected response is a JSON array (or an
//     object with a "reports" array) of objects with a free-text "notes" field.
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
        QString notes;          // free-text user comment (may contain launch options)
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
    QString cacheFilePath(const QString& appId, const QString& kind) const;
    bool loadCached(const QString& path, QByteArray& out, int maxAgeSecs) const;
    void saveCached(const QString& path, const QByteArray& data) const;

    QString reportsBaseUrl() const;
    Summary parseSummary(const QByteArray& data) const;
    QList<Report> parseReports(const QByteArray& data) const;

    QNetworkAccessManager* m_networkManager;
};

Q_DECLARE_METATYPE(ProtonDBClient::Summary)
Q_DECLARE_METATYPE(ProtonDBClient::Report)

#endif // PROTONDBCLIENT_H
