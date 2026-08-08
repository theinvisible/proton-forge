#include "GogDownloader.h"
#include "gog/GogInstallRegistry.h"
#include "gog/GogPlayTasks.h"
#include "gog/GogRequest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSettings>
#include <QStorageInfo>
#include <QtConcurrent>

namespace {

// Qt's per-host HTTP/1.1 limit is six, and GOG starts answering 429 around
// there. Four is the default because it saturates a normal connection without
// getting anywhere near either ceiling.
constexpr int kDefaultParallel = 4;
constexpr int kMaxParallel = 6;
constexpr int kMaxAttemptsPerChunk = 3;

// How long before a signed link lapses we ask for a new one. Two minutes is
// comfortably longer than a chunk takes and comfortably shorter than any TTL
// GOG has been observed to hand out.
constexpr int kResignMarginMs = 120 * 1000;
constexpr int kResignFloorMs = 30 * 1000;

// Used when the token carries no expiry we recognise. Deliberately conservative:
// re-signing needlessly costs one request, while assuming validity costs the
// download.
constexpr int kBlindResignIntervalMs = 20 * 60 * 1000;

constexpr int kJournalWriteIntervalMs = 2000;

const char* const kJournalDir = ".protonforge-gog";

QString md5Hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

} // namespace

GogDownloader& GogDownloader::instance()
{
    static GogDownloader downloader;
    return downloader;
}

GogDownloader::GogDownloader()
    : m_networkManager(new QNetworkAccessManager(this))
{
    qRegisterMetaType<GogDownloader::Progress>("GogDownloader::Progress");

    // Qt leaves transfer timeouts off by default, which means a CDN node that
    // accepts the connection and then goes quiet stalls its chunk forever —
    // and with four slots, four such nodes stall the whole download with no
    // error anywhere. That is the same silent no-progress state this class
    // exists to avoid, so a stalled chunk has to become an error the retry
    // path can act on. Sixty seconds is far longer than a 10 MB chunk needs on
    // any connection worth downloading a game over.
    m_networkManager->setTransferTimeout(60 * 1000);

    // One slot per possible in-flight chunk, so a finished download never waits
    // on a verify thread while its socket sits idle.
    m_pool.setMaxThreadCount(kMaxParallel);

    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, [this]() { emitProgress(); });

    m_resignTimer.setSingleShot(true);
    connect(&m_resignTimer, &QTimer::timeout, this, [this]() {
        if (m_job && m_job->stage == Stage::Downloading && !m_job->finished) {
            // Proactive: nothing has failed yet and nothing is held. If this
            // request fails we simply keep using the link we have until it
            // actually stops working.
            requestSecureLink();
        }
    });
}

QString GogDownloader::journalDirName()
{
    return QString::fromLatin1(kJournalDir);
}

QList<GogDownloader::ChunkPlacement>
GogDownloader::chunkPlacements(const GogInstallPlan::FileTask& file)
{
    QList<ChunkPlacement> placements;
    qint64 offset = 0;

    for (int i = 0; i < file.chunks.size(); ++i) {
        ChunkPlacement placement;
        placement.journalKey = QStringLiteral("%1#%2").arg(file.relPath).arg(i);
        placement.offset = offset;
        placements.append(placement);
        offset += file.chunks.at(i).size;
    }
    return placements;
}

GogDownloader::ChunkResult GogDownloader::writeChunk(const QByteArray& compressed,
                                                     const QString& expectedCompressedMd5,
                                                     const QString& expectedMd5,
                                                     const QString& filePath,
                                                     qint64 offset)
{
    // The compressed bytes first: CDN corruption is the common failure, and md5
    // of 10 MB is far cheaper than inflating it only to throw the result away.
    if (!expectedCompressedMd5.isEmpty()
        && md5Hex(compressed).compare(expectedCompressedMd5, Qt::CaseInsensitive) != 0) {
        return {QStringLiteral("the CDN sent a corrupt chunk"), true};
    }

    const QByteArray inflated = GogContentClient::inflateData(compressed);
    if (inflated.isNull()) {
        return {QStringLiteral("a chunk could not be decompressed"), true};
    }

    if (!expectedMd5.isEmpty()
        && md5Hex(inflated).compare(expectedMd5, Qt::CaseInsensitive) != 0) {
        return {QStringLiteral("a chunk did not match its checksum after decompressing"), true};
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadWrite)) {
        return {QStringLiteral("cannot write %1: %2").arg(filePath, file.errorString()), false};
    }
    if (!file.seek(offset) || file.write(inflated) != inflated.size()) {
        return {QStringLiteral("writing to %1 failed: %2").arg(filePath, file.errorString()), false};
    }
    return {};
}

bool GogDownloader::isSafeToDiscard(const QString& path, const QString& installRoot)
{
    const QString clean = QDir::cleanPath(path);
    const QString root = QDir::cleanPath(installRoot);

    if (clean.isEmpty() || !clean.startsWith('/')) {
        return false;
    }
    // The obvious catastrophes, spelled out rather than relied upon to fall out
    // of the arithmetic below.
    if (clean == QLatin1String("/") || clean == QDir::homePath()
        || clean == QDir::rootPath() || clean == root) {
        return false;
    }

    // Our own journal is proof we created this directory, wherever it is —
    // which covers an install the user pointed at a second drive.
    if (QDir(clean + "/" + kJournalDir).exists()) {
        return true;
    }

    if (root.isEmpty() || !clean.startsWith(root + "/")) {
        return false;
    }
    // At least <root>/<store>/<game>: one level under the root is the store
    // directory, which holds every other install.
    return clean.mid(root.size() + 1).contains('/');
}

// ---------------------------------------------------------------- queue

void GogDownloader::enqueue(const Request& request)
{
    if (request.productId.isEmpty()) {
        emit installFailed(request.productId, QStringLiteral("No product id to install."));
        return;
    }
    if (isActive(request.productId)) {
        return;   // already on its way; asking twice is not an error
    }
    for (const Request& queued : std::as_const(m_pending)) {
        if (queued.productId == request.productId) {
            return;
        }
    }

    m_pending.append(request);
    emit queueChanged();

    if (!m_job) {
        startNext();
    }
}

bool GogDownloader::isBusy() const
{
    return m_job != nullptr || !m_pending.isEmpty();
}

bool GogDownloader::isActive(const QString& productId) const
{
    return m_job && m_job->request.productId == productId;
}

QStringList GogDownloader::queuedProductIds() const
{
    QStringList ids;
    if (m_job) {
        ids << m_job->request.productId;
    }
    for (const Request& request : m_pending) {
        ids << request.productId;
    }
    return ids;
}

GogDownloader::Progress GogDownloader::progressFor(const QString& productId) const
{
    return m_lastProgress.value(productId);
}

void GogDownloader::pause(const QString& productId)
{
    if (!isActive(productId) || m_job->paused) {
        return;
    }
    m_job->paused = true;
    // In-flight replies are abandoned rather than drained: their chunks go back
    // on the queue and are re-fetched on resume. A part-received chunk is worth
    // nothing, since verification is whole-chunk.
    abortTransfers();
    saveStateJournal(true);
    emitProgress();
}

void GogDownloader::resume(const QString& productId)
{
    if (!isActive(productId) || !m_job->paused) {
        return;
    }
    m_job->paused = false;
    emitProgress();
    pump();
}

void GogDownloader::cancel(const QString& productId)
{
    if (!isActive(productId)) {
        // Not started yet — dropping it from the queue is the whole job.
        for (int i = 0; i < m_pending.size(); ++i) {
            if (m_pending.at(i).productId == productId) {
                m_pending.removeAt(i);
                emit queueChanged();
                return;
            }
        }
        return;
    }

    abortTransfers();
    saveStateJournal(true);
    emit installFailed(productId, QStringLiteral("Installation cancelled."));
    endJob();
}

void GogDownloader::cancelAndDiscard(const QString& productId)
{
    QString installPath;
    QString root;
    if (isActive(productId)) {
        installPath = m_job->installPath;
        root = m_job->request.installRoot.isEmpty() ? GogInstallRegistry::installRoot()
                                                    : m_job->request.installRoot;
    } else {
        const GogInstallRegistry::Entry entry = GogInstallRegistry::instance().entry(productId);
        installPath = entry.installPath;
        root = GogInstallRegistry::installRoot();
    }

    cancel(productId);

    if (!installPath.isEmpty() && isSafeToDiscard(installPath, root)) {
        QDir(installPath).removeRecursively();
    }
    GogInstallRegistry::instance().remove(productId);
}

bool GogDownloader::uninstall(const QString& productId, QString* error)
{
    GogInstallRegistry& registry = GogInstallRegistry::instance();
    const GogInstallRegistry::Entry entry = registry.entry(productId);
    if (!entry.valid) {
        if (error) {
            *error = QStringLiteral("ProtonForge has no record of installing that game.");
        }
        return false;
    }

    if (isActive(productId)) {
        cancel(productId);
    }

    if (QDir(entry.installPath).exists()) {
        if (!isSafeToDiscard(entry.installPath, GogInstallRegistry::installRoot())) {
            // Refusing is the right answer: this path came out of a file, and
            // removeRecursively() on the wrong one is not recoverable.
            if (error) {
                *error = QStringLiteral("Refusing to delete %1 — it is not inside the "
                                        "ProtonForge install root.").arg(entry.installPath);
            }
            return false;
        }
        if (!QDir(entry.installPath).removeRecursively()) {
            if (error) {
                *error = QStringLiteral("Could not delete %1.").arg(entry.installPath);
            }
            return false;
        }
    }

    // The prefix is ours too, and leaving a 3 GB one behind after an uninstall
    // is not what anybody means by uninstall.
    const QString prefix = GogInstallRegistry::prefixPathFor(productId);
    if (QDir(prefix).exists() && isSafeToDiscard(prefix, GogInstallRegistry::installRoot())) {
        QDir(prefix).removeRecursively();
    }

    // Dropped whether or not the directory was still there, so a user who
    // deleted it by hand can make ProtonForge agree with reality.
    registry.remove(productId);
    return true;
}

// ---------------------------------------------------------------- resolve

void GogDownloader::startNext()
{
    if (m_job || m_pending.isEmpty()) {
        return;
    }

    ++m_jobGeneration;
    m_verifying = 0;

    m_job = new Job;
    m_job->request = m_pending.takeFirst();
    if (m_job->request.languages.isEmpty()) {
        m_job->request.languages = {QStringLiteral("en-US"), QStringLiteral("en")};
    }
    m_job->stage = Stage::Resolving;
    m_job->detail = QStringLiteral("Looking up the build…");
    emit queueChanged();
    emitProgress();

    GogContentClient& content = GogContentClient::instance();
    const QString productId = m_job->request.productId;

    m_contentConnections << connect(&content, &GogContentClient::buildsReady, this,
                                    [this, productId](const QString& id,
                                                      const QList<GogContentClient::Build>& builds) {
        if (m_job && id == productId) onBuilds(builds);
    });
    m_contentConnections << connect(&content, &GogContentClient::buildsFailed, this,
                                    [this, productId](const QString& id, const QString& reason) {
        if (m_job && id == productId) failJob(reason);
    });
    m_contentConnections << connect(&content, &GogContentClient::buildMetaReady, this,
                                    [this, productId](const QString& id,
                                                      const GogContentClient::BuildMeta& meta) {
        if (m_job && id == productId) onBuildMeta(meta);
    });
    m_contentConnections << connect(&content, &GogContentClient::buildMetaFailed, this,
                                    [this, productId](const QString& id, const QString& reason) {
        if (m_job && id == productId) failJob(reason);
    });
    m_contentConnections << connect(&content, &GogContentClient::depotManifestReady, this,
                                    [this, productId](const QString& id, const QString& hash,
                                                      const GogContentClient::DepotManifest& m) {
        if (m_job && id == productId) onManifest(hash, m);
    });
    m_contentConnections << connect(&content, &GogContentClient::depotManifestFailed, this,
                                    [this, productId](const QString& id, const QString& hash,
                                                      const QString& reason) {
        if (m_job && id == productId) {
            failJob(QStringLiteral("depot %1: %2").arg(hash, reason));
        }
    });
    m_contentConnections << connect(&content, &GogContentClient::secureLinkReady, this,
                                    [this, productId](const QString& id,
                                                      const GogContentClient::SecureLink& link) {
        if (m_job && id == productId) onSecureLink(link);
    });
    m_contentConnections << connect(&content, &GogContentClient::secureLinkFailed, this,
                                    [this, productId](const QString& id, const QString& reason) {
        if (!m_job || id != productId) {
            return;
        }
        m_job->resignInFlight = false;
        if (!m_job->link.valid) {
            failJob(QStringLiteral("could not obtain a download link: %1").arg(reason));
        } else if (!m_job->heldForResign.isEmpty()) {
            // Chunks are waiting on a signature that did not arrive. Failing is
            // the honest outcome — retrying forever is the bug this class was
            // written to avoid.
            failJob(QStringLiteral("GOG would not re-sign the download link: %1").arg(reason));
        }
    });

    resolveBuilds();
}

void GogDownloader::resolveBuilds()
{
    // Linux first. Most products answer 404 or an empty generation-2 list, and
    // then the Windows build under Proton is the right answer — but the ones
    // that do have a native build should get it.
    GogContentClient::instance().fetchBuilds(m_job->request.productId, m_job->os);
}

void GogDownloader::onBuilds(const QList<GogContentClient::Build>& builds)
{
    const GogContentClient::Build build = GogContentClient::newestPublicBuild(builds);

    if (build.buildId.isEmpty()) {
        if (!m_job->triedWindows) {
            m_job->triedWindows = true;
            m_job->os = QStringLiteral("windows");
            m_job->detail = QStringLiteral("No Linux build — installing the Windows version, "
                                           "which will run through Proton.");
            emitProgress();
            resolveBuilds();
            return;
        }
        failJob(QStringLiteral("GOG has no generation-2 build for this product, so ProtonForge "
                               "cannot install it."));
        return;
    }

    // Kept here because the build meta does not repeat it and goggame-*.info
    // does not carry it either — this listing is the only place it appears.
    m_job->versionName = build.versionName;

    m_job->detail = QStringLiteral("Reading the build manifest…");
    emitProgress();
    GogContentClient::instance().fetchBuildMeta(m_job->request.productId, build.link);
}

void GogDownloader::onBuildMeta(const GogContentClient::BuildMeta& meta)
{
    m_job->meta = meta;
    m_job->depots = GogInstallPlan::selectDepots(meta, m_job->request.languages,
                                                 m_job->request.dlcIds, m_job->request.bitness);
    if (m_job->depots.isEmpty()) {
        failJob(QStringLiteral("This build has no depots for the selected language."));
        return;
    }

    m_job->manifestsPending = static_cast<int>(m_job->depots.size());
    m_job->detail = QStringLiteral("Reading %1 depots…").arg(m_job->depots.size());
    emitProgress();

    for (const GogContentClient::DepotRef& depot : std::as_const(m_job->depots)) {
        GogContentClient::instance().fetchDepotManifest(m_job->request.productId,
                                                        depot.manifestHash);
    }
}

void GogDownloader::onManifest(const QString& hash, const GogContentClient::DepotManifest& manifest)
{
    m_job->manifests.insert(hash, manifest);
    if (--m_job->manifestsPending == 0) {
        buildPlan();
    }
}

void GogDownloader::buildPlan()
{
    // Depot order decides which file wins when two provide the same path, so the
    // manifests have to go in exactly the order selectDepots returned them.
    QList<GogContentClient::DepotManifest> ordered;
    for (const GogContentClient::DepotRef& depot : std::as_const(m_job->depots)) {
        if (m_job->manifests.contains(depot.manifestHash)) {
            ordered.append(m_job->manifests.value(depot.manifestHash));
        }
    }

    m_job->plan = GogInstallPlan::build(m_job->meta, ordered);
    if (!m_job->plan.valid || m_job->plan.files.isEmpty()) {
        failJob(QStringLiteral("The build manifest described no files to install."));
        return;
    }

    const QString root = m_job->request.installRoot.isEmpty()
                             ? GogInstallRegistry::installRoot()
                             : m_job->request.installRoot;
    m_job->installPath = GogInstallRegistry::storeDirectory(root) + "/"
                         + m_job->plan.installDirectory;

    m_job->stage = Stage::Preflight;
    m_job->detail = QStringLiteral("Preparing %1 files…").arg(m_job->plan.files.size());
    emitProgress();

    QString error;
    if (!preflight(&error)) {
        failJob(error);
        return;
    }

    // An incomplete entry, written before the first byte: this is what makes an
    // interrupted install resumable rather than an orphaned directory.
    GogInstallRegistry::Entry entry = GogInstallRegistry::instance().entry(m_job->request.productId);
    entry.productId   = m_job->request.productId;
    entry.title       = m_job->request.title.isEmpty() ? m_job->meta.installDirectory
                                                       : m_job->request.title;
    entry.installPath = m_job->installPath;
    entry.buildId     = m_job->meta.buildId;
    entry.platform    = m_job->os;
    entry.languages   = m_job->request.languages;
    entry.dlcIds      = m_job->request.dlcIds;
    entry.size        = m_job->plan.totalSize;
    entry.complete    = false;
    GogInstallRegistry::instance().put(entry);

    writePlanJournal();
    loadStateJournal();
    requestSecureLink();
}

// ---------------------------------------------------------------- transfer

bool GogDownloader::preflight(QString* error)
{
    QString collision;
    if (GogInstallPlan::wouldCollideCaseInsensitively(m_job->plan, &collision)) {
        *error = QStringLiteral("This game contains files whose names differ only in case (%1). "
                                "They cannot both exist on a case-insensitive drive such as "
                                "NTFS or exFAT — choose an install location on a Linux "
                                "filesystem.").arg(collision);
        return false;
    }

    if (!QDir().mkpath(m_job->installPath)) {
        *error = QStringLiteral("Could not create %1.").arg(m_job->installPath);
        return false;
    }

    // Five percent of headroom: the depots are sparse files until written, and
    // a filesystem that fills at 99 % takes the install down with it.
    const QStorageInfo storage(m_job->installPath);
    const qint64 needed = m_job->plan.totalSize + m_job->plan.totalSize / 20;
    if (storage.isValid() && storage.bytesAvailable() > 0 && storage.bytesAvailable() < needed) {
        *error = QStringLiteral("Not enough free space on %1: %2 GB needed, %3 GB available.")
                     .arg(QString::fromUtf8(storage.rootPath().toUtf8()))
                     .arg(needed / 1073741824.0, 0, 'f', 1)
                     .arg(storage.bytesAvailable() / 1073741824.0, 0, 'f', 1);
        return false;
    }

    for (const QString& directory : std::as_const(m_job->plan.directories)) {
        QDir().mkpath(m_job->installPath + "/" + directory);
    }

    // Create every file at its final size up front. Sparse, so it costs nothing,
    // and it means ENOSPC surfaces here rather than eight gigabytes in.
    for (const GogInstallPlan::FileTask& task : std::as_const(m_job->plan.files)) {
        const QString path = m_job->installPath + "/" + task.relPath;
        QDir().mkpath(QFileInfo(path).absolutePath());

        if (!task.linkTarget.isEmpty()) {
            continue;   // symlinks are made in finalize, once their targets exist
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadWrite)) {
            *error = QStringLiteral("Could not create %1: %2").arg(path, file.errorString());
            return false;
        }
        if (file.size() != task.size && !file.resize(task.size)) {
            *error = QStringLiteral("Could not reserve space for %1: %2")
                         .arg(path, file.errorString());
            return false;
        }
    }

    return true;
}

void GogDownloader::requestSecureLink()
{
    if (!m_job || m_job->resignInFlight) {
        return;   // coalesced: one signature request at a time, never two
    }
    m_job->resignInFlight = true;
    GogContentClient::instance().fetchSecureLink(m_job->request.productId);
}

void GogDownloader::onSecureLink(const GogContentClient::SecureLink& link)
{
    m_job->resignInFlight = false;
    m_job->link = link;
    ++m_job->linkGeneration;
    m_job->endpointIndex = 0;

    // Re-sign before it lapses. When the token carries no expiry we recognise,
    // a fixed conservative interval — never an assumption that it is still good.
    qint64 delay = kBlindResignIntervalMs;
    if (link.expiresAt.isValid()) {
        const qint64 until = QDateTime::currentDateTimeUtc().msecsTo(link.expiresAt);
        delay = qMax<qint64>(kResignFloorMs, until - kResignMarginMs);
    }
    m_resignTimer.start(static_cast<int>(qMin<qint64>(delay, kBlindResignIntervalMs)));

    // Chunks that were refused with the old signature go back on the queue now
    // that there is a new one.
    if (!m_job->heldForResign.isEmpty()) {
        m_job->tasks.append(m_job->heldForResign);
        m_job->heldForResign.clear();
    }

    if (m_job->stage != Stage::Downloading) {
        m_job->stage = Stage::Downloading;
        m_job->detail.clear();
        buildChunkQueue();
        m_progressTimer.start();
    }
    pump();
}

void GogDownloader::buildChunkQueue()
{
    m_job->filesTotal = static_cast<int>(m_job->plan.files.size());
    m_job->bytesTotal = 0;
    m_job->bytesCompleted = 0;
    m_job->filesDone = 0;

    for (int fileIndex = 0; fileIndex < m_job->plan.files.size(); ++fileIndex) {
        const GogInstallPlan::FileTask& file = m_job->plan.files.at(fileIndex);

        const QList<ChunkPlacement> placements = chunkPlacements(file);
        int remaining = 0;
        for (int chunkIndex = 0; chunkIndex < file.chunks.size(); ++chunkIndex) {
            const GogContentClient::Chunk& chunk = file.chunks.at(chunkIndex);
            m_job->bytesTotal += chunk.compressedSize;

            if (m_job->done.contains(placements.at(chunkIndex).journalKey)) {
                // Already on disk from an earlier run — counted towards the bar
                // so a resumed download does not restart at zero.
                m_job->bytesCompleted += chunk.compressedSize;
                continue;
            }

            ChunkTask task;
            task.fileIndex = fileIndex;
            task.chunkIndex = chunkIndex;
            task.offset = placements.at(chunkIndex).offset;
            task.chunk = chunk;
            m_job->tasks.append(task);
            ++remaining;
        }

        if (remaining > 0) {
            m_job->remainingChunks.insert(fileIndex, remaining);
        } else {
            ++m_job->filesDone;
        }
    }

    m_lastTickBytes = m_job->bytesCompleted;
    m_lastTickAt = QDateTime::currentDateTime();
}

void GogDownloader::pump()
{
    if (!m_job || m_job->finished) {
        return;
    }

    const int parallel = qBound(1, QSettings().value("gog/parallelDownloads", kDefaultParallel).toInt(),
                                kMaxParallel);

    while (!m_job->paused && m_replies.size() < parallel && m_job->nextTask < m_job->tasks.size()) {
        startChunk(m_job->nextTask++);
    }

    const bool queueDrained = m_job->nextTask >= m_job->tasks.size();
    if (queueDrained && m_replies.isEmpty() && m_verifying == 0
        && m_job->heldForResign.isEmpty() && !m_job->paused) {
        finalizeInstall();
    }
}

void GogDownloader::startChunk(int taskIndex)
{
    ChunkTask& task = m_job->tasks[taskIndex];

    if (m_job->link.endpoints.isEmpty()) {
        failJob(QStringLiteral("GOG returned no download endpoints."));
        return;
    }

    const int endpoint = m_job->endpointIndex % m_job->link.endpoints.size();
    const QString url = GogContentClient::buildChunkUrl(m_job->link.endpoints.at(endpoint),
                                                        task.chunk.compressedMd5);
    if (url.isEmpty()) {
        failJob(QStringLiteral("GOG's download link was missing a value ProtonForge needs to "
                               "build the URL."));
        return;
    }
    task.linkGeneration = m_job->linkGeneration;

    // Chunk URLs carry their own signature; no bearer token belongs on them.
    QNetworkReply* reply = m_networkManager->get(GogRequest::make(QUrl(url)));
    m_replies.insert(taskIndex, reply);
    m_inFlightBytes.insert(taskIndex, 0);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, taskIndex](qint64 received, qint64) {
        if (m_inFlightBytes.contains(taskIndex)) {
            m_inFlightBytes[taskIndex] = received;
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, taskIndex, reply]() {
        onChunkReply(taskIndex, reply);
    });
}

void GogDownloader::onChunkReply(int taskIndex, QNetworkReply* reply)
{
    reply->deleteLater();

    // Aborted by pause or cancel: the task index may no longer mean anything.
    if (!m_job || m_job->finished || !m_replies.contains(taskIndex)
        || m_replies.value(taskIndex) != reply) {
        return;
    }
    m_replies.remove(taskIndex);
    m_inFlightBytes.remove(taskIndex);

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 401 || status == 403) {
        ChunkTask task = m_job->tasks.at(taskIndex);

        if (task.resigned) {
            // This URL was built from a signature obtained *after* the previous
            // refusal, so expiry is not the explanation. Spinning here is the
            // failure mode this whole design exists to avoid.
            failJob(QStringLiteral("GOG refused the download even with a freshly signed link "
                                   "(HTTP %1). Signing in again may help.").arg(status));
            return;
        }

        task.resigned = true;
        m_job->heldForResign.append(task);
        requestSecureLink();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const bool connectionProblem =
            reply->error() == QNetworkReply::ConnectionRefusedError
            || reply->error() == QNetworkReply::RemoteHostClosedError
            || reply->error() == QNetworkReply::HostNotFoundError
            || reply->error() == QNetworkReply::TimeoutError
            || reply->error() == QNetworkReply::TemporaryNetworkFailureError
            || reply->error() == QNetworkReply::NetworkSessionFailedError
            || reply->error() == QNetworkReply::ProxyConnectionRefusedError
            || reply->error() == QNetworkReply::ServiceUnavailableError;
        retryChunk(taskIndex, connectionProblem);
        return;
    }

    const QByteArray body = reply->readAll();
    const ChunkTask task = m_job->tasks.at(taskIndex);
    const QString filePath =
        m_job->installPath + "/" + m_job->plan.files.at(task.fileIndex).relPath;

    // md5, inflate and write are 30–60 ms for a 10 MB chunk. Four of those on
    // the GUI thread is a visible freeze, so they go to the pool while the
    // network stays here.
    ++m_verifying;
    const quint64 generation = m_jobGeneration;
    auto* watcher = new QFutureWatcher<ChunkResult>(this);
    connect(watcher, &QFutureWatcher<ChunkResult>::finished, this,
            [this, watcher, taskIndex, generation]() {
        const ChunkResult result = watcher->result();
        watcher->deleteLater();
        // A verify belonging to a job that has since ended: neither its count
        // nor its outcome has anything to do with whatever is running now.
        if (generation != m_jobGeneration) {
            return;
        }
        --m_verifying;
        onChunkVerified(taskIndex, result);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, &GogDownloader::writeChunk, body,
                                         task.chunk.compressedMd5, task.chunk.md5,
                                         filePath, task.offset));
}

void GogDownloader::onChunkVerified(int taskIndex, const ChunkResult& result)
{
    if (!m_job || m_job->finished) {
        return;
    }

    if (!result.ok()) {
        // A disk that will not take the write is not going to take it on the
        // third try either, and three rounds of downloading ten megabytes to
        // fail identically is worse than saying so at once.
        if (!result.retriable) {
            failJob(result.error);
            return;
        }
        retryChunk(taskIndex, true);
        return;
    }

    const ChunkTask& task = m_job->tasks.at(taskIndex);
    const GogInstallPlan::FileTask& file = m_job->plan.files.at(task.fileIndex);

    m_job->done.insert(chunkPlacements(file).at(task.chunkIndex).journalKey);
    m_job->bytesCompleted += task.chunk.compressedSize;

    const int remaining = m_job->remainingChunks.value(task.fileIndex, 0) - 1;
    if (remaining <= 0) {
        m_job->remainingChunks.remove(task.fileIndex);
        ++m_job->filesDone;
    } else {
        m_job->remainingChunks[task.fileIndex] = remaining;
    }

    saveStateJournal();
    pump();
}

void GogDownloader::retryChunk(int taskIndex, bool rotateEndpoint)
{
    ChunkTask task = m_job->tasks.at(taskIndex);

    // Rotation first: a dead endpoint should cost the next one a try, not one of
    // this chunk's three attempts.
    const int endpointCount = static_cast<int>(m_job->link.endpoints.size());
    if (rotateEndpoint && endpointCount > 1 && task.rotations < endpointCount - 1) {
        ++task.rotations;
        m_job->endpointIndex = (m_job->endpointIndex + 1) % endpointCount;
    } else {
        ++task.attempts;
    }

    if (task.attempts >= kMaxAttemptsPerChunk) {
        failJob(QStringLiteral("A piece of %1 could not be downloaded after %2 attempts.")
                    .arg(m_job->plan.files.at(task.fileIndex).relPath)
                    .arg(kMaxAttemptsPerChunk));
        return;
    }

    m_job->tasks.append(task);
    pump();
}

// ---------------------------------------------------------------- finish

void GogDownloader::finalizeInstall()
{
    m_job->stage = Stage::Finalizing;
    m_job->detail = QStringLiteral("Finishing up…");
    m_progressTimer.stop();
    m_resignTimer.stop();
    emitProgress();

    for (const GogInstallPlan::FileTask& file : std::as_const(m_job->plan.files)) {
        const QString path = m_job->installPath + "/" + file.relPath;

        if (!file.linkTarget.isEmpty()) {
            QFile::remove(path);   // idempotent: a re-install must not fail here
            QFile::link(file.linkTarget, path);
            continue;
        }
        if (file.executable) {
            QFile target(path);
            target.setPermissions(target.permissions() | QFileDevice::ExeOwner
                                  | QFileDevice::ExeGroup | QFileDevice::ExeOther);
        }
    }

    // How the game is actually started. Without this, GameRunner would fall back
    // to its filename heuristic, which skips anything called "launcher" — and
    // several GOG entry points are called exactly that.
    const QString productId = m_job->request.productId;
    QString executable;
    QString workingDirectory;
    QStringList launchArgs;
    bool nativeLinux = false;

    QFile infoFile(m_job->installPath + "/" + GogPlayTasks::infoFileName(productId));
    if (infoFile.open(QIODevice::ReadOnly)) {
        const GogPlayTasks::Info info = GogPlayTasks::parseInfoFile(infoFile.readAll());
        const GogPlayTasks::PlayTask task = GogPlayTasks::primaryTask(info);
        if (!task.path.isEmpty()) {
            executable = GogPlayTasks::resolveExecutableOnDisk(m_job->installPath, task.path);
            launchArgs = task.arguments;
            nativeLinux = GogPlayTasks::looksNativeLinux(task.path);
            if (!task.workingDir.isEmpty()) {
                workingDirectory =
                    GogPlayTasks::resolveExecutableOnDisk(m_job->installPath, task.workingDir);
            }
        }
    }

    GogInstallRegistry& registry = GogInstallRegistry::instance();
    GogInstallRegistry::Entry entry = registry.entry(productId);
    entry.productId        = productId;
    entry.installPath      = m_job->installPath;
    entry.buildId          = m_job->meta.buildId;
    entry.latestBuildId    = m_job->meta.buildId;   // just fetched — it is the newest
    entry.latestCheckedAt  = QDateTime::currentDateTime();
    entry.versionName      = m_job->versionName;
    entry.platform         = m_job->os;
    entry.languages        = m_job->request.languages;
    entry.dlcIds           = m_job->request.dlcIds;
    entry.size             = m_job->plan.totalSize;
    entry.executablePath   = executable;
    entry.workingDirectory = workingDirectory;
    entry.launchArgs       = launchArgs;
    entry.nativeLinux      = nativeLinux;
    entry.warnings         = m_job->plan.warnings;
    entry.complete         = true;
    if (entry.title.isEmpty()) {
        entry.title = m_job->request.title.isEmpty() ? m_job->plan.installDirectory
                                                     : m_job->request.title;
    }
    registry.put(entry);

    removeJournal();

    const QString installPath = m_job->installPath;
    emit installFinished(productId, installPath);
    endJob();
}

void GogDownloader::failJob(const QString& reason)
{
    if (!m_job || m_job->finished) {
        return;
    }
    const QString productId = m_job->request.productId;

    abortTransfers();
    // The journal stays: whatever arrived is still on disk and still correct, so
    // the next attempt resumes rather than starting over.
    saveStateJournal(true);

    emit installFailed(productId, reason);
    endJob();
}

void GogDownloader::endJob()
{
    if (!m_job) {
        return;
    }
    m_job->finished = true;
    ++m_jobGeneration;   // orphans any verify still in the pool

    m_progressTimer.stop();
    m_resignTimer.stop();
    abortTransfers();
    disconnectContent();

    delete m_job;
    m_job = nullptr;

    emit queueChanged();
    startNext();
}

void GogDownloader::abortTransfers()
{
    const QList<int> indices = m_replies.keys();
    const QList<QNetworkReply*> replies = m_replies.values();
    m_replies.clear();
    m_inFlightBytes.clear();

    // Whatever was in flight goes back on the queue before the aborts land: a
    // partially received chunk verifies as nothing, so it has to be fetched
    // again in full, and dropping it here is how a paused download would resume
    // with a hole in the middle of a file.
    if (m_job && !m_job->finished) {
        for (int index : indices) {
            m_job->tasks.append(m_job->tasks.at(index));
        }
    }

    for (QNetworkReply* reply : replies) {
        reply->abort();
    }
}

void GogDownloader::disconnectContent()
{
    for (const QMetaObject::Connection& connection : std::as_const(m_contentConnections)) {
        disconnect(connection);
    }
    m_contentConnections.clear();
}

// ---------------------------------------------------------------- journal

QString GogDownloader::journalPath() const
{
    return m_job->installPath + "/" + kJournalDir;
}

void GogDownloader::writePlanJournal()
{
    QDir().mkpath(journalPath());

    QJsonArray files;
    for (const GogInstallPlan::FileTask& task : std::as_const(m_job->plan.files)) {
        QJsonObject object;
        object["path"] = task.relPath;
        object["size"] = static_cast<double>(task.size);
        files.append(object);
    }

    QJsonObject root;
    root["productId"] = m_job->request.productId;
    root["buildId"]   = m_job->meta.buildId;
    root["os"]        = m_job->os;
    root["totalSize"] = static_cast<double>(m_job->plan.totalSize);
    root["files"]     = files;

    QSaveFile file(journalPath() + "/plan.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void GogDownloader::loadStateJournal()
{
    QFile file(journalPath() + "/state.json");
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    // A journal from a different build describes different bytes at different
    // offsets. Ignoring it costs a re-download; trusting it corrupts the install.
    if (root.value("buildId").toString() != m_job->meta.buildId) {
        return;
    }
    for (const QJsonValue& value : root.value("done").toArray()) {
        m_job->done.insert(value.toString());
    }
}

void GogDownloader::saveStateJournal(bool force)
{
    if (!m_job) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    if (!force && m_lastJournalWrite.isValid()
        && m_lastJournalWrite.msecsTo(now) < kJournalWriteIntervalMs) {
        return;
    }
    m_lastJournalWrite = now;

    QJsonArray done;
    for (const QString& key : std::as_const(m_job->done)) {
        done.append(key);
    }

    QJsonObject root;
    root["buildId"] = m_job->meta.buildId;
    root["done"]    = done;

    QDir().mkpath(journalPath());
    QSaveFile file(journalPath() + "/state.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void GogDownloader::removeJournal()
{
    QDir(journalPath()).removeRecursively();
}

// ---------------------------------------------------------------- progress

void GogDownloader::emitProgress()
{
    if (!m_job) {
        return;
    }

    qint64 inFlight = 0;
    for (auto it = m_inFlightBytes.cbegin(); it != m_inFlightBytes.cend(); ++it) {
        inFlight += it.value();
    }

    Progress progress;
    progress.stage      = m_job->stage;
    progress.detail     = m_job->detail;
    progress.bytesDone  = m_job->bytesCompleted + inFlight;
    progress.bytesTotal = m_job->bytesTotal;
    progress.filesDone  = m_job->filesDone;
    progress.filesTotal = m_job->filesTotal;
    progress.paused     = m_job->paused;

    const QDateTime now = QDateTime::currentDateTime();
    if (m_lastTickAt.isValid()) {
        const qint64 elapsed = m_lastTickAt.msecsTo(now);
        if (elapsed >= 1000) {
            progress.bytesPerSecond = (progress.bytesDone - m_lastTickBytes) * 1000 / elapsed;
            m_lastTickBytes = progress.bytesDone;
            m_lastTickAt = now;
        } else {
            progress.bytesPerSecond = m_lastProgress.value(m_job->request.productId).bytesPerSecond;
        }
    }

    m_lastProgress.insert(m_job->request.productId, progress);
    emit installProgress(m_job->request.productId, progress);
}
