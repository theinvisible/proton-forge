#include "GogApiClient.h"
#include "GogRequest.h"
#include "network/JsonDiskCache.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrl>

namespace {

const QString kCacheArea = QStringLiteral("gog");

// The owned library changes when the user buys something; six hours is a
// compromise between "my new game isn't here" and re-running ten paginated
// requests every time the dialog opens. Refresh forces a re-fetch either way.
constexpr int kLibraryTtlSecs = 6 * 60 * 60;

// Product metadata is effectively immutable once published.
constexpr int kProductTtlSecs = 7 * 24 * 60 * 60;

// 50 is the server's page size; asking for more is ignored.
constexpr int kPageSize = 50;

// A guard, not a limit: 200 pages is 10,000 products, well past any real
// library. It exists so a server that keeps reporting more pages cannot spin
// forever.
constexpr int kMaxPages = 200;

QString productsUrl(int page)
{
    return QStringLiteral(
        "https://embed.gog.com/account/getFilteredProducts"
        "?mediaType=1&page=%1&sortBy=title").arg(page);
}

} // namespace

GogApiClient& GogApiClient::instance()
{
    static GogApiClient instance;
    return instance;
}

GogApiClient::GogApiClient()
    : m_networkManager(new QNetworkAccessManager(this))
{
}

// --- pure --------------------------------------------------------------------

QStringList GogApiClient::parseOwnedIds(const QByteArray& json)
{
    QStringList ids;
    const QJsonArray owned =
        QJsonDocument::fromJson(json).object().value(QStringLiteral("owned")).toArray();
    for (const QJsonValue& value : owned) {
        // GOG sends these as numbers; everything downstream keys on strings.
        const QString id = value.toVariant().toString();
        if (!id.isEmpty()) {
            ids << id;
        }
    }
    return ids;
}

QString GogApiClient::normalizeImageUrl(const QString& raw, const QString& variant)
{
    if (raw.isEmpty()) {
        return QString();
    }

    QString url = raw;
    // Protocol-relative, which is how GOG serves nearly all of it.
    if (url.startsWith(QLatin1String("//"))) {
        url.prepend(QStringLiteral("https:"));
    }
    if (!url.startsWith(QLatin1String("http"))) {
        url = QStringLiteral("https://images.gog-statics.com/") + url;
    }

    // Already carries a size suffix, or an extension — leave it be.
    if (url.endsWith(QLatin1String(".jpg")) || url.endsWith(QLatin1String(".png"))) {
        return url;
    }
    return variant.isEmpty() ? url : url + variant;
}

QString GogApiClient::bannerImageUrl(const QString& raw)
{
    if (raw.isEmpty()) {
        return QString();
    }

    // GOG names every size of an image after the same content hash, so the
    // variant is chosen by rebuilding the URL rather than by asking for it.
    // Rebuilding is the only option here: `images.logo2x` arrives complete,
    // suffix and all, and normalizeImageUrl leaves anything ending in .jpg
    // alone — correctly, since appending a second suffix would 404.
    //
    // The variant is the one the library dialog already uses. It is also the
    // one that fits: 256x117 is the shape of a Steam header (2.19 against
    // 2.14), where logo2x is 200x120 — a different aspect and too small for
    // the 230x107 detail panel.
    static const QRegularExpression hashPattern(QStringLiteral("[0-9a-f]{64}"));
    const QRegularExpressionMatch match = hashPattern.match(raw);
    if (!match.hasMatch()) {
        // An unfamiliar shape. Handing it back untouched shows whatever GOG
        // meant; guessing a hash out of it would produce a confident 404.
        return normalizeImageUrl(raw, QStringLiteral("_product_tile_256.jpg"));
    }

    return QStringLiteral("https://images.gog-statics.com/%1_product_tile_256.jpg")
        .arg(match.captured(0));
}

QString GogApiClient::storeUrl(const QString& slug)
{
    return slug.isEmpty() ? QStringLiteral("https://www.gog.com/games")
                          : QStringLiteral("https://www.gog.com/game/%1").arg(slug);
}

QList<GogApiClient::Product> GogApiClient::parseFilteredProducts(const QByteArray& json,
                                                                 int* totalPages)
{
    QList<Product> products;

    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (totalPages) {
        *totalPages = root.value(QStringLiteral("totalPages")).toInt(1);
    }

    const QJsonArray entries = root.value(QStringLiteral("products")).toArray();
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();

        Product product;
        product.id = entry.value(QStringLiteral("id")).toVariant().toString();
        product.title = entry.value(QStringLiteral("title")).toString();
        product.slug = entry.value(QStringLiteral("slug")).toString();
        product.imageUrl = normalizeImageUrl(entry.value(QStringLiteral("image")).toString(),
                                             QStringLiteral("_product_tile_256.jpg"));

        const QJsonObject worksOn = entry.value(QStringLiteral("worksOn")).toObject();
        product.supportsWindows = worksOn.value(QStringLiteral("Windows")).toBool();
        product.supportsLinux = worksOn.value(QStringLiteral("Linux")).toBool();
        product.supportsMac = worksOn.value(QStringLiteral("Mac")).toBool();

        // An entry with no id is not addressable, so it is not a product.
        if (!product.id.isEmpty()) {
            products.append(product);
        }
    }

    return products;
}

GogApiClient::ProductDetail GogApiClient::parseProduct(const QByteArray& json)
{
    ProductDetail detail;

    const QJsonObject root = QJsonDocument::fromJson(json).object();
    detail.id = root.value(QStringLiteral("id")).toVariant().toString();
    if (detail.id.isEmpty()) {
        return detail;
    }

    detail.title = root.value(QStringLiteral("title")).toString();
    detail.slug = root.value(QStringLiteral("slug")).toString();

    const QJsonObject images = root.value(QStringLiteral("images")).toObject();
    detail.imageUrl = bannerImageUrl(images.value(QStringLiteral("logo2x")).toString());

    // This is the field that decides whether we can install at all, and it is
    // not the same as the store page's platform list.
    const QJsonObject compat =
        root.value(QStringLiteral("content_system_compatibility")).toObject();
    detail.contentSystemWindows = compat.value(QStringLiteral("windows")).toBool();
    detail.contentSystemLinux = compat.value(QStringLiteral("linux")).toBool();

    const QJsonArray dlcs =
        root.value(QStringLiteral("expanded_dlcs")).toArray();
    for (const QJsonValue& value : dlcs) {
        const QString dlcId = value.toObject().value(QStringLiteral("id")).toVariant().toString();
        if (!dlcId.isEmpty()) {
            detail.dlcIds << dlcId;
        }
    }

    detail.valid = true;
    return detail;
}

// --- async -------------------------------------------------------------------

void GogApiClient::fetchLibrary()
{
    QByteArray cached;
    const QString path = JsonDiskCache::filePath(kCacheArea, QStringLiteral("library"));
    if (JsonDiskCache::load(path, cached, kLibraryTtlSecs)) {
        int pages = 1;
        const QList<Product> products = parseFilteredProducts(cached, &pages);
        if (!products.isEmpty()) {
            emit libraryReady(products);
            return;
        }
    }

    fetchLibraryPage(1, {});
}

void GogApiClient::fetchLibraryPage(int page, QList<Product> collected)
{
    GogRequest::get(m_networkManager, QUrl(productsUrl(page)), this,
                    [this, page, collected](QNetworkReply* reply) mutable {
        if (!reply) {
            emit libraryFailed(QStringLiteral("Not signed in to GOG."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit libraryFailed(QStringLiteral("Could not reach GOG: %1").arg(reply->errorString()));
            return;
        }

        const QByteArray body = reply->readAll();
        int totalPages = 1;
        const QList<Product> pageProducts = parseFilteredProducts(body, &totalPages);
        collected.append(pageProducts);

        // Cache the first page as-is: it is the only one whose raw body still
        // parses into a usable library on its own, and re-fetching a large
        // library from page one is what the TTL is there to avoid.
        if (page == 1) {
            JsonDiskCache::save(JsonDiskCache::filePath(kCacheArea, QStringLiteral("library")),
                                body);
        }

        // Stop on an empty page too: a server that keeps claiming more pages
        // while returning nothing would otherwise never finish.
        if (page < totalPages && page < kMaxPages && !pageProducts.isEmpty()) {
            fetchLibraryPage(page + 1, collected);
            return;
        }

        emit libraryReady(collected);
    });
}

void GogApiClient::fetchProduct(const QString& productId)
{
    if (productId.isEmpty()) {
        emit productFailed(productId, QStringLiteral("No product id."));
        return;
    }

    const QString path = JsonDiskCache::filePath(kCacheArea, QStringLiteral("product-") + productId);

    QByteArray cached;
    if (JsonDiskCache::load(path, cached, kProductTtlSecs)) {
        const ProductDetail detail = parseProduct(cached);
        if (detail.valid) {
            emit productReady(productId, detail);
            return;
        }
    }

    const QUrl url(QStringLiteral(
        "https://api.gog.com/products/%1?expand=downloads,expanded_dlcs").arg(productId));

    // Catalogue data, not account data: this endpoint answers without a token.
    // Asking for one would make artwork — and installability — unknowable while
    // signed out, which is precisely when a DRM-free library is still listed.
    GogRequest::getPublic(m_networkManager, url, this,
                          [this, productId, path](QNetworkReply* reply) {
        if (!reply || reply->error() != QNetworkReply::NoError) {
            emit productFailed(productId, reply ? reply->errorString()
                                                : QStringLiteral("request failed"));
            return;
        }

        const QByteArray body = reply->readAll();
        const ProductDetail detail = parseProduct(body);
        if (!detail.valid) {
            emit productFailed(productId, QStringLiteral("GOG returned no usable product data."));
            return;
        }

        JsonDiskCache::save(path, body);
        emit productReady(productId, detail);
    });
}

void GogApiClient::clearCache()
{
    JsonDiskCache::remove(JsonDiskCache::filePath(kCacheArea, QStringLiteral("library")));
}
