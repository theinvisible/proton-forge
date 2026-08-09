#include "GogStoreService.h"
#include "GogAuth.h"
#include "GogContentClient.h"
#include "GogDownloader.h"
#include "GogInstallRegistry.h"

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes >= 1073741824LL) {
        return QStringLiteral("%1 GB").arg(bytes / 1073741824.0, 0, 'f', 1);
    }
    if (bytes >= 1048576LL) {
        return QStringLiteral("%1 MB").arg(bytes / 1048576.0, 0, 'f', 0);
    }
    return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
}

QString describe(const GogDownloader::Progress& progress)
{
    if (progress.paused) {
        return QStringLiteral("Paused");
    }
    switch (progress.stage) {
    case GogDownloader::Stage::Idle:
        return QString();
    case GogDownloader::Stage::Resolving:
    case GogDownloader::Stage::Preflight:
    case GogDownloader::Stage::Finalizing:
        return progress.detail;
    case GogDownloader::Stage::Downloading:
        break;
    }

    QString text = QStringLiteral("%1 of %2 files — %3 of %4")
                       .arg(progress.filesDone)
                       .arg(progress.filesTotal)
                       .arg(humanSize(progress.bytesDone), humanSize(progress.bytesTotal));
    if (progress.bytesPerSecond > 0) {
        text += QStringLiteral(" (%1/s)").arg(humanSize(progress.bytesPerSecond));
    }
    return text;
}

} // namespace

GogStoreService::GogStoreService()
{
    GogAuth& auth = GogAuth::instance();

    connect(&auth, &GogAuth::loginSucceeded, this, [this](const QString&) {
        emit authStateChanged(true);
    });
    connect(&auth, &GogAuth::loggedOut, this, [this]() {
        emit authStateChanged(false);
    });
    connect(&auth, &GogAuth::sessionRestored, this, [this](bool loggedIn) {
        emit authStateChanged(loggedIn);
    });
    // The stored refresh token was rejected. As far as everything above is
    // concerned that is indistinguishable from signing out, and treating it the
    // same means the game list and the dialog need no special case.
    connect(&auth, &GogAuth::reauthenticationRequired, this, [this]() {
        emit authStateChanged(false);
    });

    GogApiClient& api = GogApiClient::instance();
    connect(&api, &GogApiClient::libraryReady, this,
            [this](const QList<GogApiClient::Product>& products) {
        QList<StoreEntry> entries;
        entries.reserve(products.size());
        m_titles.clear();
        m_images.clear();
        for (const GogApiClient::Product& product : products) {
            entries.append(toEntry(product));
            m_titles.insert(product.id, product.title);
            m_images.insert(product.id, product.imageUrl);
        }
        emit libraryReady(entries);

        // The listing already carries a banner for everything owned, so an
        // install that has none can be filled in from here for free — no
        // per-product request at all.
        adoptArtworkFromLibrary();
    });
    connect(&api, &GogApiClient::libraryFailed, this, &GogStoreService::libraryFailed);

    // The downloader is app-global and outlives this dialog, so its progress is
    // forwarded rather than owned.
    GogDownloader& downloader = GogDownloader::instance();
    connect(&downloader, &GogDownloader::installProgress, this,
            [this](const QString& productId, const GogDownloader::Progress& progress) {
        StoreInstallProgress out;
        out.detail = describe(progress);
        out.bytesDone = progress.bytesDone;
        out.bytesTotal = progress.bytesTotal;
        out.bytesPerSecond = progress.bytesPerSecond;
        out.paused = progress.paused;
        emit installProgress(productId, out);
    });
    connect(&downloader, &GogDownloader::installFinished, this,
            [this](const QString& productId, const QString&) {
        emit installFinished(productId);
    });
    connect(&downloader, &GogDownloader::installFailed, this, &GogStoreService::installFailed);

    GogContentClient& content = GogContentClient::instance();
    connect(&content, &GogContentClient::buildsReady, this,
            [this](const QString& productId, const QList<GogContentClient::Build>& builds) {
        if (!m_awaitingBuilds.remove(productId)) {
            return;   // somebody else asked; not ours to record
        }
        const GogContentClient::Build newest = GogContentClient::newestPublicBuild(builds);
        if (!newest.buildId.isEmpty()) {
            GogInstallRegistry::instance().setLatestBuild(productId, newest.buildId);
        }
    });
    connect(&content, &GogContentClient::buildsFailed, this,
            [this](const QString& productId, const QString&) {
        // Unknown stays unknown. Leaving latestBuildId alone means the game
        // simply does not claim an update, which is the right answer when we
        // could not find out.
        m_awaitingBuilds.remove(productId);
    });

    // Artwork lookups, connected once here for the same reason the build ones
    // are: a connection made per batch and torn down on a counter outlives the
    // batch whenever a lookup fails instead of answering.
    connect(&api, &GogApiClient::productReady, this,
            [this](const QString& productId, const GogApiClient::ProductDetail& detail) {
        if (!m_awaitingProducts.contains(productId)) {
            return;   // somebody else asked; not ours to record
        }
        recordArtwork(productId, detail.imageUrl);
        finishArtworkLookup(productId);
    });
    connect(&api, &GogApiClient::productFailed, this,
            [this](const QString& productId, const QString&) {
        // Left empty rather than filled with a guess, so the next start tries
        // again instead of caching a URL that resolves to nothing.
        finishArtworkLookup(productId);
    });
}

void GogStoreService::recordArtwork(const QString& productId, const QString& imageUrl)
{
    if (GogInstallRegistry::instance().setImageUrl(productId, imageUrl)) {
        m_artworkChanged = true;
    }
}

void GogStoreService::finishArtworkLookup(const QString& productId)
{
    m_awaitingProducts.remove(productId);
    if (!m_awaitingProducts.isEmpty() || !m_artworkChanged) {
        return;
    }
    // Once, when the last one settles: the game list redraws from a fresh
    // discovery pass, and doing that per product would repeat it N times to
    // reach the same picture.
    m_artworkChanged = false;
    emit installedMetadataChanged();
}

void GogStoreService::adoptArtworkFromLibrary()
{
    GogInstallRegistry& registry = GogInstallRegistry::instance();
    registry.load();

    for (const GogInstallRegistry::Entry& entry : registry.completeEntries()) {
        if (entry.imageUrl.isEmpty()) {
            recordArtwork(entry.productId, m_images.value(entry.productId));
        }
    }

    if (m_awaitingProducts.isEmpty() && m_artworkChanged) {
        m_artworkChanged = false;
        emit installedMetadataChanged();
    }
}

void GogStoreService::refreshInstalledArtwork()
{
    GogInstallRegistry& registry = GogInstallRegistry::instance();
    registry.load();

    GogApiClient& api = GogApiClient::instance();

    for (const GogInstallRegistry::Entry& entry : registry.completeEntries()) {
        if (!entry.imageUrl.isEmpty()) {
            continue;   // already known, and it does not go stale
        }
        if (m_awaitingProducts.contains(entry.productId)) {
            continue;   // already asked; the answer serves both callers
        }

        m_awaitingProducts.insert(entry.productId);
        // Disk-cached for a week, so this costs one request per game once.
        api.fetchProduct(entry.productId);
    }
}

bool GogStoreService::isAuthenticated() const
{
    return GogAuth::instance().isLoggedIn();
}

void GogStoreService::signOut()
{
    GogAuth::instance().logout();
    GogApiClient::instance().clearCache();
}

void GogStoreService::fetchLibrary()
{
    if (!isAuthenticated()) {
        emit libraryFailed(QStringLiteral("Not signed in to GOG."));
        return;
    }
    GogApiClient::instance().fetchLibrary();
    refreshUpdateState();
}

void GogStoreService::install(const QString& id)
{
    GogDownloader::Request request;
    request.productId = id;
    request.title = m_titles.value(id);
    request.imageUrl = m_images.value(id);
    GogDownloader::instance().enqueue(request);
}

void GogStoreService::uninstall(const QString& id)
{
    QString error;
    if (!GogDownloader::instance().uninstall(id, &error)) {
        emit installFailed(id, error);
        return;
    }
    emit installFinished(id);   // the list has changed either way
}

void GogStoreService::cancelInstall(const QString& id, bool discard)
{
    if (discard) {
        GogDownloader::instance().cancelAndDiscard(id);
    } else {
        GogDownloader::instance().cancel(id);
    }
}

void GogStoreService::pauseInstall(const QString& id)
{
    GogDownloader::instance().pause(id);
}

void GogStoreService::resumeInstall(const QString& id)
{
    GogDownloader::instance().resume(id);
}

bool GogStoreService::isInstalling(const QString& id) const
{
    return GogDownloader::instance().queuedProductIds().contains(id);
}

void GogStoreService::refreshUpdateState()
{
    GogInstallRegistry& registry = GogInstallRegistry::instance();
    registry.load();

    GogContentClient& content = GogContentClient::instance();

    for (const GogInstallRegistry::Entry& entry : registry.completeEntries()) {
        // Never while it is being installed — the download is already resolving
        // the newest build and would race this into the registry.
        if (GogDownloader::instance().isActive(entry.productId)) {
            continue;
        }
        if (m_awaitingBuilds.contains(entry.productId)) {
            continue;   // already asked; the answer serves both callers
        }

        m_awaitingBuilds.insert(entry.productId);
        // Disk-cached with the same TTL as the rest of the content system, so
        // opening the dialog twice in an hour costs nothing.
        content.fetchBuilds(entry.productId,
                            entry.platform.isEmpty() ? QStringLiteral("windows")
                                                     : entry.platform);
    }
}

StoreEntry GogStoreService::toEntry(const GogApiClient::Product& product)
{
    StoreEntry entry;
    entry.id = product.id;
    entry.title = product.title;
    entry.imageUrl = product.imageUrl;
    entry.storeUrl = GogApiClient::storeUrl(product.slug);
    entry.supportsWindows = product.supportsWindows;
    entry.supportsLinux = product.supportsLinux;
    // installUrl stays empty: ProtonForge installs GOG games itself rather than
    // handing off to a client that does not exist on Linux.
    //
    // "Installable" here means "worth trying". Whether a product has a
    // generation-2 build is only knowable by asking the content system per
    // product, and doing that for a 500-game library to paint a list would be
    // several hundred requests. So the answer is optimistic and the failure is
    // specific: a generation-1-only product fails at install time saying exactly
    // that, rather than the row lying about it.
    entry.installable = product.supportsWindows || product.supportsLinux;
    return entry;
}
