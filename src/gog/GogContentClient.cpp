#include "GogContentClient.h"
#include "GogRequest.h"
#include "network/JsonDiskCache.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrl>

#include <zlib.h>

namespace {

const QString kCacheArea = QStringLiteral("gog");

// Build lists move when a game is patched; the objects they point at never do,
// because both are content-addressed.
constexpr int kBuildsTtlSecs = 6 * 60 * 60;
constexpr int kImmutableTtlSecs = 30 * 24 * 60 * 60;

const QString kContentSystem = QStringLiteral("https://content-system.gog.com");
const QString kCdn = QStringLiteral("https://cdn.gog.com");

QDateTime parseGogTimestamp(const QString& text)
{
    if (text.isEmpty()) {
        return QDateTime();
    }
    // "2021-05-19T13:32:22+0000" — ISO 8601 without the colon in the offset,
    // which Qt::ISODate does not accept.
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODate);
    if (parsed.isValid()) {
        return parsed;
    }
    return QDateTime::fromString(text, QStringLiteral("yyyy-MM-ddTHH:mm:sst"));
}

QStringList toStringList(const QJsonArray& array)
{
    QStringList list;
    for (const QJsonValue& value : array) {
        const QString item = value.toVariant().toString();
        if (!item.isEmpty()) {
            list << item;
        }
    }
    return list;
}

} // namespace

GogContentClient& GogContentClient::instance()
{
    static GogContentClient instance;
    return instance;
}

GogContentClient::GogContentClient()
    : m_networkManager(new QNetworkAccessManager(this))
{
}

// --- zlib --------------------------------------------------------------------

QByteArray GogContentClient::inflateData(const QByteArray& compressed, int windowBits)
{
    if (compressed.isEmpty()) {
        return QByteArray();
    }

    z_stream stream = {};
    // Not qUncompress: that expects Qt's own 4-byte big-endian length prefix,
    // which nothing outside Qt produces. And GOG sends no Content-Encoding, so
    // Qt's network layer will not have inflated it either.
    if (inflateInit2(&stream, windowBits) != Z_OK) {
        return QByteArray();
    }

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
    stream.avail_in = static_cast<uInt>(compressed.size());

    QByteArray out;
    QByteArray buffer(64 * 1024, Qt::Uninitialized);

    int status = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());

        status = ::inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            inflateEnd(&stream);
            return QByteArray();
        }

        const int produced = buffer.size() - static_cast<int>(stream.avail_out);
        if (produced > 0) {
            out.append(buffer.constData(), produced);
        }
        // Z_BUF_ERROR with nothing produced and nothing left to read means a
        // truncated stream, not a full output buffer.
        if (status == Z_BUF_ERROR && produced == 0) {
            inflateEnd(&stream);
            return QByteArray();
        }
    } while (status != Z_STREAM_END);

    inflateEnd(&stream);
    return out;
}

// --- URLs --------------------------------------------------------------------

QString GogContentClient::galaxyPath(const QString& hash)
{
    // Already a path, or too short to split — hand it back rather than build
    // something malformed.
    if (hash.contains('/') || hash.size() < 4) {
        return hash;
    }
    return hash.left(2) + "/" + hash.mid(2, 2) + "/" + hash;
}

QString GogContentClient::metaUrl(const QString& hash)
{
    if (hash.isEmpty()) {
        return QString();
    }
    return kCdn + "/content-system/v2/meta/" + galaxyPath(hash);
}

// --- parsers -----------------------------------------------------------------

QList<GogContentClient::Build> GogContentClient::parseBuilds(const QByteArray& json)
{
    QList<Build> builds;

    const QJsonArray items =
        QJsonDocument::fromJson(json).object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue& value : items) {
        const QJsonObject item = value.toObject();

        Build build;
        build.generation = item.value(QStringLiteral("generation")).toInt();
        // Generation 1 is a different CDN layout with XML manifests, and
        // ProtonForge does not speak it. Dropping those here means everything
        // downstream can assume v2 rather than re-checking.
        if (build.generation != 2) {
            continue;
        }

        build.buildId = item.value(QStringLiteral("build_id")).toVariant().toString();
        build.legacyBuildId = item.value(QStringLiteral("legacy_build_id")).toVariant().toString();
        build.link = item.value(QStringLiteral("link")).toString();
        build.versionName = item.value(QStringLiteral("version_name")).toString();
        build.branch = item.value(QStringLiteral("branch")).toString();
        build.os = item.value(QStringLiteral("os")).toString();
        build.isPublic = item.value(QStringLiteral("public")).toBool();
        build.datePublished =
            parseGogTimestamp(item.value(QStringLiteral("date_published")).toString());

        if (!build.link.isEmpty()) {
            builds.append(build);
        }
    }

    return builds;
}

GogContentClient::Build GogContentClient::newestPublicBuild(const QList<Build>& builds)
{
    Build newest;
    for (const Build& build : builds) {
        // A private branch is a beta the user did not ask for.
        if (!build.isPublic || !build.branch.isEmpty()) {
            continue;
        }
        if (!newest.datePublished.isValid()
            || build.datePublished > newest.datePublished) {
            newest = build;
        }
    }
    return newest;
}

GogContentClient::BuildMeta GogContentClient::parseBuildMeta(const QByteArray& inflated)
{
    BuildMeta meta;

    const QJsonObject root = QJsonDocument::fromJson(inflated).object();
    meta.baseProductId = root.value(QStringLiteral("baseProductId")).toVariant().toString();
    if (meta.baseProductId.isEmpty()) {
        return meta;
    }

    meta.buildId = root.value(QStringLiteral("buildId")).toVariant().toString();
    meta.installDirectory = root.value(QStringLiteral("installDirectory")).toString();
    meta.platform = root.value(QStringLiteral("platform")).toString();
    meta.dependencies = toStringList(root.value(QStringLiteral("dependencies")).toArray());

    const QJsonArray depots = root.value(QStringLiteral("depots")).toArray();
    for (const QJsonValue& value : depots) {
        const QJsonObject entry = value.toObject();

        DepotRef depot;
        depot.manifestHash = entry.value(QStringLiteral("manifest")).toString();
        depot.productId = entry.value(QStringLiteral("productId")).toVariant().toString();
        depot.languages = toStringList(entry.value(QStringLiteral("languages")).toArray());
        depot.osBitness = toStringList(entry.value(QStringLiteral("osBitness")).toArray());
        depot.size = entry.value(QStringLiteral("size")).toVariant().toLongLong();
        depot.compressedSize =
            entry.value(QStringLiteral("compressedSize")).toVariant().toLongLong();

        // No manifest, nothing to fetch.
        if (!depot.manifestHash.isEmpty()) {
            meta.depots.append(depot);
        }
    }

    meta.valid = true;
    return meta;
}

GogContentClient::DepotManifest GogContentClient::parseDepotManifest(const QByteArray& inflated)
{
    DepotManifest manifest;

    const QJsonObject root = QJsonDocument::fromJson(inflated).object();
    const QJsonObject depot = root.value(QStringLiteral("depot")).toObject();
    if (depot.isEmpty()) {
        return manifest;
    }

    manifest.productId = depot.value(QStringLiteral("productId")).toVariant().toString();

    const QJsonArray items = depot.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& value : items) {
        const QJsonObject entry = value.toObject();

        DepotItem item;
        item.path = entry.value(QStringLiteral("path")).toString();
        item.type = entry.value(QStringLiteral("type")).toString();
        item.md5 = entry.value(QStringLiteral("md5")).toString();
        item.linkTarget = entry.value(QStringLiteral("target")).toString();
        item.flags = toStringList(entry.value(QStringLiteral("flags")).toArray());

        const QJsonArray chunks = entry.value(QStringLiteral("chunks")).toArray();
        for (const QJsonValue& chunkValue : chunks) {
            const QJsonObject chunkObject = chunkValue.toObject();

            Chunk chunk;
            chunk.compressedMd5 = chunkObject.value(QStringLiteral("compressedMd5")).toString();
            chunk.md5 = chunkObject.value(QStringLiteral("md5")).toString();
            chunk.compressedSize =
                chunkObject.value(QStringLiteral("compressedSize")).toVariant().toLongLong();
            chunk.size = chunkObject.value(QStringLiteral("size")).toVariant().toLongLong();

            // Without a compressedMd5 there is nothing to fetch and nothing to
            // verify it against.
            if (!chunk.compressedMd5.isEmpty()) {
                item.chunks.append(chunk);
            }
        }

        if (!item.path.isEmpty()) {
            manifest.items.append(item);
        }
    }

    manifest.valid = true;
    return manifest;
}

GogContentClient::SecureLink GogContentClient::parseSecureLink(const QByteArray& json,
                                                               const QDateTime& now)
{
    SecureLink link;
    link.fetchedAt = now;

    const QJsonArray urls =
        QJsonDocument::fromJson(json).object().value(QStringLiteral("urls")).toArray();
    for (const QJsonValue& value : urls) {
        const QJsonObject entry = value.toObject();

        SecureEndpoint endpoint;
        endpoint.endpointName = entry.value(QStringLiteral("endpoint_name")).toString();
        endpoint.urlFormat = entry.value(QStringLiteral("url_format")).toString();
        endpoint.priority = entry.value(QStringLiteral("priority")).toInt();
        endpoint.maxFails = entry.value(QStringLiteral("max_fails")).toVariant().toLongLong();
        endpoint.fallbackOnly = entry.value(QStringLiteral("fallback_only")).toBool();

        const QJsonObject parameters = entry.value(QStringLiteral("parameters")).toObject();
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            endpoint.parameters.insert(it.key(), it.value().toVariant().toString());
        }

        if (!endpoint.urlFormat.isEmpty()) {
            link.endpoints.append(endpoint);
        }
    }

    // Priority order, fallback-only last. The downloader walks this list on a
    // connection failure, so the order is the retry policy.
    std::stable_sort(link.endpoints.begin(), link.endpoints.end(),
                     [](const SecureEndpoint& a, const SecureEndpoint& b) {
        if (a.fallbackOnly != b.fallbackOnly) {
            return !a.fallbackOnly;
        }
        return a.priority < b.priority;
    });

    // The earliest expiry across the endpoints: once one token lapses the URLs
    // have to be rebuilt anyway, and re-signing early costs one request.
    for (const SecureEndpoint& endpoint : std::as_const(link.endpoints)) {
        const QDateTime expiry = secureLinkExpiry(endpoint);
        if (expiry.isValid() && (!link.expiresAt.isValid() || expiry < link.expiresAt)) {
            link.expiresAt = expiry;
        }
    }

    link.valid = !link.endpoints.isEmpty();
    return link;
}

QDateTime GogContentClient::secureLinkExpiry(const SecureEndpoint& endpoint)
{
    // CDN token auth writes the deadline into the signed blob, but which name it
    // uses depends on the vendor: "exp" (Akamai) and "nva" — not valid after —
    // are the two GOG hands out.
    //
    // Anything else returns null on purpose. A guessed timestamp is worse than
    // none: it would suppress the downloader's conservative re-sign interval and
    // leave it discovering the expiry from a 403 mid-download.
    // '=' belongs in the leading delimiter set: GOG's fastly endpoint nests the
    // deadline as "token=nva=<unix>~dir=...", so requiring ~ & ? or start-of-
    // string ahead of it misses the real thing. The delimiter is still required
    // — without it "unexp=..." or "validuntil=..." would match.
    static const QRegularExpression pattern(
        QStringLiteral("(?:^|[~&?=])(?:exp|nva)=(\\d{9,12})(?:$|[~&])"));

    for (const QString& value : endpoint.parameters) {
        const QRegularExpressionMatch match = pattern.match(value);
        if (match.hasMatch()) {
            bool ok = false;
            const qint64 seconds = match.captured(1).toLongLong(&ok);
            if (ok && seconds > 0) {
                // QTimeZone::utc(), not QTimeZone::UTC: the enum constant arrived in
                // Qt 6.5 and two of the four packaging targets are still on 6.4.
                // The static method has been there since Qt 5.2 and is not
                // deprecated on new Qt either.
                return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::utc());
            }
        }
    }
    return QDateTime();
}

QString GogContentClient::buildChunkUrl(const SecureEndpoint& endpoint,
                                        const QString& compressedMd5)
{
    if (endpoint.urlFormat.isEmpty() || compressedMd5.isEmpty()) {
        return QString();
    }

    // The chunk's own location is appended to the signed path before the format
    // is filled in — the token covers the directory, not the file.
    QMap<QString, QString> parameters = endpoint.parameters;
    parameters[QStringLiteral("path")] =
        parameters.value(QStringLiteral("path")) + "/" + galaxyPath(compressedMd5);

    QString url = endpoint.urlFormat;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        url.replace("{" + it.key() + "}", it.value());
    }

    // A leftover placeholder means the endpoint described a parameter it did not
    // supply. Sending that produces an opaque 403 far from here.
    static const QRegularExpression leftover(QStringLiteral("\\{[A-Za-z0-9_]+\\}"));
    if (leftover.match(url).hasMatch()) {
        return QString();
    }

    return url;
}

QString GogContentClient::sanitizeDepotPath(const QString& depotPath)
{
    if (depotPath.isEmpty()) {
        return QString();
    }

    QString path = depotPath;
    path.replace('\\', '/');

    // A Windows drive letter is not a relative path by any reading.
    static const QRegularExpression driveLetter(QStringLiteral("^[A-Za-z]:"));
    if (driveLetter.match(path).hasMatch()) {
        return QString();
    }
    if (path.startsWith('/')) {
        return QString();
    }

    // Rejected rather than resolved. A manifest comes off the network, and the
    // only safe answer to a path trying to leave the install directory is to
    // refuse it — collapsing it would still honour part of the attempt.
    QStringList parts;
    for (const QString& segment : path.split('/', Qt::SkipEmptyParts)) {
        if (segment == QLatin1String("..")) {
            return QString();
        }
        if (segment == QLatin1String(".")) {
            continue;
        }
        parts << segment;
    }

    return parts.isEmpty() ? QString() : parts.join('/');
}

// --- async -------------------------------------------------------------------

void GogContentClient::fetchBuilds(const QString& productId, const QString& os)
{
    const QString cacheKey = QStringLiteral("builds-%1-%2").arg(productId, os);
    const QString cachePath = JsonDiskCache::filePath(kCacheArea, cacheKey);

    QByteArray cached;
    if (JsonDiskCache::load(cachePath, cached, kBuildsTtlSecs)) {
        emit buildsReady(productId, parseBuilds(cached));
        return;
    }

    const QUrl url(QStringLiteral("%1/products/%2/os/%3/builds?generation=2")
                       .arg(kContentSystem, productId, os));

    // No token needed here, and asking for one would make the whole content
    // system unusable while signed out.
    GogRequest::getPublic(m_networkManager, url, this,
                          [this, productId, cachePath](QNetworkReply* reply) {
        if (!reply) {
            emit buildsFailed(productId, QStringLiteral("request failed"));
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            // "This product has no builds for that platform" — which is an
            // answer, not a failure. It is also the normal reply for os/linux,
            // because GOG serves Linux through the content system for only a
            // minority of products. Reporting it as an error would make the
            // Windows fallback unreachable.
            emit buildsReady(productId, {});
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit buildsFailed(productId, reply->errorString());
            return;
        }

        const QByteArray body = reply->readAll();
        JsonDiskCache::save(cachePath, body);
        emit buildsReady(productId, parseBuilds(body));
    });
}

void GogContentClient::fetchBuildMeta(const QString& productId, const QString& metaLink)
{
    if (metaLink.isEmpty()) {
        emit buildMetaFailed(productId, QStringLiteral("no build meta link"));
        return;
    }

    // Content-addressed, so it can be cached until the cache is swept.
    const QString cachePath =
        JsonDiskCache::filePath(kCacheArea, QStringLiteral("meta-") + QUrl(metaLink).fileName());

    QByteArray cached;
    if (JsonDiskCache::load(cachePath, cached, kImmutableTtlSecs)) {
        const BuildMeta meta = parseBuildMeta(cached);
        if (meta.valid) {
            emit buildMetaReady(productId, meta);
            return;
        }
    }

    GogRequest::getPublic(m_networkManager, QUrl(metaLink), this,
                          [this, productId, cachePath](QNetworkReply* reply) {
        if (!reply || reply->error() != QNetworkReply::NoError) {
            emit buildMetaFailed(productId, reply ? reply->errorString()
                                                  : QStringLiteral("request failed"));
            return;
        }

        const QByteArray inflated = inflateData(reply->readAll());
        const BuildMeta meta = parseBuildMeta(inflated);
        if (!meta.valid) {
            emit buildMetaFailed(productId, QStringLiteral("could not read the build metadata"));
            return;
        }

        // The inflated form is cached: it is what the parser wants, and storing
        // it saves inflating the same bytes on every run.
        JsonDiskCache::save(cachePath, inflated);
        emit buildMetaReady(productId, meta);
    });
}

void GogContentClient::fetchDepotManifest(const QString& productId, const QString& manifestHash)
{
    if (manifestHash.isEmpty()) {
        emit depotManifestFailed(productId, manifestHash, QStringLiteral("no manifest hash"));
        return;
    }

    const QString cachePath =
        JsonDiskCache::filePath(kCacheArea, QStringLiteral("depot-") + manifestHash);

    QByteArray cached;
    if (JsonDiskCache::load(cachePath, cached, kImmutableTtlSecs)) {
        const DepotManifest manifest = parseDepotManifest(cached);
        if (manifest.valid) {
            emit depotManifestReady(productId, manifestHash, manifest);
            return;
        }
    }

    GogRequest::getPublic(m_networkManager, QUrl(metaUrl(manifestHash)), this,
                          [this, productId, manifestHash, cachePath](QNetworkReply* reply) {
        if (!reply || reply->error() != QNetworkReply::NoError) {
            emit depotManifestFailed(productId, manifestHash,
                                     reply ? reply->errorString()
                                           : QStringLiteral("request failed"));
            return;
        }

        const QByteArray inflated = inflateData(reply->readAll());
        const DepotManifest manifest = parseDepotManifest(inflated);
        if (!manifest.valid) {
            emit depotManifestFailed(productId, manifestHash,
                                     QStringLiteral("could not read the depot manifest"));
            return;
        }

        JsonDiskCache::save(cachePath, inflated);
        emit depotManifestReady(productId, manifestHash, manifest);
    });
}

void GogContentClient::fetchSecureLink(const QString& productId, const QString& path)
{
    const QUrl url(QStringLiteral("%1/products/%2/secure_link?generation=2&path=%3&_version=2")
                       .arg(kContentSystem, productId, path));

    // Never cached: the whole point of it is a short-lived signature.
    GogRequest::get(m_networkManager, url, this, [this, productId](QNetworkReply* reply) {
        if (!reply || reply->error() != QNetworkReply::NoError) {
            emit secureLinkFailed(productId, reply ? reply->errorString()
                                                   : QStringLiteral("not signed in to GOG"));
            return;
        }

        const SecureLink link =
            parseSecureLink(reply->readAll(), QDateTime::currentDateTimeUtc());
        if (!link.valid) {
            emit secureLinkFailed(productId, QStringLiteral("GOG returned no download endpoints"));
            return;
        }
        emit secureLinkReady(productId, link);
    });
}
