#ifndef GOGDOWNLOADER_H
#define GOGDOWNLOADER_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include "gog/GogContentClient.h"
#include "gog/GogInstallPlan.h"

class QNetworkReply;

// Fetching a GOG game and putting it on disk.
//
// The awkward part is not the downloading, it is that two clocks expire under
// it. The OAuth access token lasts an hour and GogAuth handles that; the signed
// CDN token inside the chunk URLs is separate, shorter and nobody else's
// problem to renew. Both reference implementations ignore the second one:
// gogdl never reads the expiry out of the secure link and re-queues any failed
// chunk without re-signing, so once the token lapses every chunk 403s, gets
// re-queued, and 403s again — an unbounded loop that makes no progress and
// reports nothing. lgogdownloader templates the same URLs and likewise never
// parses an expiry. That works right up until the download is slow, the game is
// large, or it was paused overnight, which is exactly the shape of the
// "stuck at 47% forever" reports those tools collect.
//
// So this engine does four things differently, and they are the reason the class
// is as involved as it is:
//
//   It parses the expiry (GogContentClient::secureLinkExpiry) and re-signs
//   *before* the token lapses, falling back to a conservative fixed interval
//   when the token shape is not one it recognises — never assuming validity.
//
//   It re-signs reactively on a 401/403 too, because clocks disagree.
//
//   It bounds that. A chunk that 401s against a URL signed *after* it started
//   is not an expiry problem, and the install fails then and there with a
//   message rather than spinning.
//
//   It rotates through the CDN endpoints secure_link offers on a connection
//   failure, before spending one of the chunk's three attempts.
//
// Everything else follows the house rules: verification and inflation run off
// the GUI thread, progress is aggregated onto a timer rather than emitted per
// readyRead, and the journal lives inside the install directory so that
// deleting the folder is complete cleanup.
class GogDownloader : public QObject
{
    Q_OBJECT

public:
    enum class Stage {
        Idle,
        Resolving,     // which build, which depots, which files
        Preflight,     // disk space, collisions, directories
        Downloading,
        Finalizing,    // permissions, symlinks, play tasks, registry
    };

    struct Progress {
        Stage stage = Stage::Idle;
        QString detail;
        qint64 bytesDone = 0;
        qint64 bytesTotal = 0;
        int filesDone = 0;
        int filesTotal = 0;
        qint64 bytesPerSecond = 0;
        bool paused = false;
    };

    struct Request {
        QString productId;
        QString title;
        QStringList languages;    // empty means en-US plus whatever is shared
        QStringList dlcIds;
        QString installRoot;      // empty means the configured one
        int bitness = 64;
    };

    static GogDownloader& instance();

    void enqueue(const Request& request);
    void pause(const QString& productId);
    void resume(const QString& productId);

    // Stops, and leaves the partial install where it is — the journal makes it
    // resumable.
    void cancel(const QString& productId);

    // Stops and deletes. Guarded by isSafeToDiscard(), because this is a
    // recursive delete of a path the user chose.
    void cancelAndDiscard(const QString& productId);

    bool isBusy() const;
    bool isActive(const QString& productId) const;
    QStringList queuedProductIds() const;
    Progress progressFor(const QString& productId) const;

    // Remove an installed game. Same guard as cancelAndDiscard, and it drops the
    // registry entry whether or not the directory was still there — a user who
    // deleted the folder by hand should be able to make ProtonForge agree.
    bool uninstall(const QString& productId, QString* error = nullptr);

    // --- the parts worth testing without a socket ---

    static QString journalDirName();

    // Where each of a file's chunks lands, paired with the key the journal
    // records it under. Offsets accumulate the *inflated* size — using the
    // compressed one instead produces a file of the right length full of
    // overlapping garbage, with nothing to show for it but a game that will not
    // start.
    struct ChunkPlacement {
        QString journalKey;
        qint64 offset = 0;
    };
    static QList<ChunkPlacement> chunkPlacements(const GogInstallPlan::FileTask& file);

    // Verify a downloaded chunk and put it in the file. Returns a null QString
    // on success, an error otherwise. Public because this is where a bad byte
    // becomes a broken install, and it is worth being able to prove against a
    // handmade chunk rather than a 24 GB download.
    //
    // Runs on a worker thread in normal use; it holds no state and opens its
    // own handle, so chunks of the same file may be written concurrently.
    //
    // `retriable` says whether fetching the chunk again could plausibly help:
    // true for a corrupt or undecompressable body, false for a file that cannot
    // be written. The caller needs that distinction and must not have to infer
    // it from the wording of the message.
    struct ChunkResult {
        QString error;         // null on success
        bool retriable = false;
        bool ok() const { return error.isNull(); }
    };
    static ChunkResult writeChunk(const QByteArray& compressed,
                                  const QString& expectedCompressedMd5,
                                  const QString& expectedMd5,
                                  const QString& filePath,
                                  qint64 offset);

    // Whether a recursive delete of `path` is allowed. True only for a directory
    // that either carries our journal or sits at least two levels under the
    // install root — never the root itself, never $HOME, never "/".
    static bool isSafeToDiscard(const QString& path, const QString& installRoot);

signals:
    void installProgress(const QString& productId, const GogDownloader::Progress& progress);
    void installFinished(const QString& productId, const QString& installPath);
    void installFailed(const QString& productId, const QString& reason);
    void queueChanged();

private:
    GogDownloader();
    ~GogDownloader() override = default;
    GogDownloader(const GogDownloader&) = delete;
    GogDownloader& operator=(const GogDownloader&) = delete;

    struct ChunkTask {
        int fileIndex = 0;
        int chunkIndex = 0;
        qint64 offset = 0;             // where in the file this chunk lands
        GogContentClient::Chunk chunk;
        int attempts = 0;
        int rotations = 0;
        // The link generation this task's URL was built from. A 401 on a task
        // whose generation equals the current one means re-signing did not help.
        quint64 linkGeneration = 0;
        bool resigned = false;
    };

    struct Job {
        Request request;
        QString installPath;
        QString os = QStringLiteral("linux");
        bool triedWindows = false;

        GogContentClient::BuildMeta meta;
        QString versionName;        // from the build listing; the info file has none
        QList<GogContentClient::DepotRef> depots;
        QHash<QString, GogContentClient::DepotManifest> manifests;
        int manifestsPending = 0;
        GogInstallPlan::Plan plan;

        GogContentClient::SecureLink link;
        quint64 linkGeneration = 0;
        int endpointIndex = 0;
        bool resignInFlight = false;

        QList<ChunkTask> tasks;
        int nextTask = 0;
        QSet<QString> done;            // journal keys of chunks already on disk

        // Chunks still outstanding per file, so "142 of 1563 files" is a count
        // rather than a guess.
        QHash<int, int> remainingChunks;

        // Refused with a stale signature, waiting for a fresh one. Held rather
        // than re-queued, or they would 403 again immediately and burn their
        // three attempts before the new link ever arrived.
        QList<ChunkTask> heldForResign;

        qint64 bytesCompleted = 0;
        qint64 bytesTotal = 0;
        int filesTotal = 0;
        int filesDone = 0;

        Stage stage = Stage::Idle;
        QString detail;
        bool paused = false;
        bool finished = false;
    };

    // --- resolve ---
    void startNext();
    void resolveBuilds();
    void onBuilds(const QList<GogContentClient::Build>& builds);
    void onBuildMeta(const GogContentClient::BuildMeta& meta);
    void onManifest(const QString& hash, const GogContentClient::DepotManifest& manifest);
    void buildPlan();

    // --- transfer ---
    bool preflight(QString* error);
    void requestSecureLink();
    void onSecureLink(const GogContentClient::SecureLink& link);
    void buildChunkQueue();
    void pump();
    void startChunk(int taskIndex);
    void onChunkReply(int taskIndex, QNetworkReply* reply);
    void onChunkVerified(int taskIndex, const ChunkResult& result);
    void retryChunk(int taskIndex, bool rotateEndpoint);
    void requeue(int taskIndex);

    // --- finish ---
    void finalizeInstall();
    void failJob(const QString& reason);
    void endJob();

    // --- journal ---
    QString journalPath() const;
    void writePlanJournal();
    void loadStateJournal();
    void saveStateJournal(bool force = false);
    void removeJournal();

    void emitProgress();
    void abortTransfers();
    void disconnectContent();

    QNetworkAccessManager* m_networkManager;
    QThreadPool m_pool;
    QTimer m_progressTimer;
    QTimer m_resignTimer;

    QList<Request> m_pending;
    Job* m_job = nullptr;

    QHash<int, QNetworkReply*> m_replies;      // task index -> in-flight reply
    QHash<int, qint64> m_inFlightBytes;
    int m_verifying = 0;

    // Verifies outlive the job that started them — the watchers are children of
    // this object, not of the job. Without a generation to check against, a
    // verify from a cancelled install would decrement the *next* install's
    // outstanding count and let it finalize while its own chunks were still in
    // the pool.
    quint64 m_jobGeneration = 0;

    QList<QMetaObject::Connection> m_contentConnections;

    qint64 m_lastTickBytes = 0;
    QDateTime m_lastTickAt;
    QDateTime m_lastJournalWrite;
    QHash<QString, Progress> m_lastProgress;
};

Q_DECLARE_METATYPE(GogDownloader::Progress)

#endif // GOGDOWNLOADER_H
