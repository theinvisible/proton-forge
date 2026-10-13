#include "launchers/SteamArtwork.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include "network/JsonDiskCache.h"

namespace SteamArtwork {

namespace {

constexpr int kStoreHeaderTtlSecs = 90 * 24 * 60 * 60;

} // namespace

QString localHeaderPath(const QString& appId, const QString& steamRoot)
{
    if (appId.isEmpty() || steamRoot.isEmpty()) {
        return QString();
    }
    const QDir appDir(steamRoot + QStringLiteral("/appcache/librarycache/") + appId);
    if (!appDir.exists()) {
        return QString();
    }

    static const QStringList names = {QStringLiteral("header.jpg"),
                                      QStringLiteral("library_header.jpg")};
    QStringList candidates;
    for (const QString& name : names) {
        candidates << appDir.filePath(name);
    }
    for (const QString& sub : appDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        for (const QString& name : names) {
            candidates << appDir.filePath(sub + QLatin1Char('/') + name);
        }
    }

    QString newest;
    QDateTime newestTime;
    for (const QString& path : candidates) {
        const QFileInfo info(path);
        if (!info.isFile() || info.size() == 0) {
            continue;
        }
        if (newest.isEmpty() || info.lastModified() > newestTime) {
            newest = path;
            newestTime = info.lastModified();
        }
    }
    if (!newest.isEmpty()) {
        return newest;
    }

    // Some apps only have the banner in a language variant (header_german.jpg).
    const QStringList localized =
        appDir.entryList({QStringLiteral("header_*.jpg")}, QDir::Files, QDir::Name);
    return localized.isEmpty() ? QString() : appDir.filePath(localized.first());
}

QString appDetailsCachePath(const QString& appId)
{
    return JsonDiskCache::filePath(QStringLiteral("steam"), QStringLiteral("appdetails-") + appId);
}

QString headerFromAppDetails(const QByteArray& json, const QString& appId)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return QString();
    }
    const QJsonObject app = doc.object().value(appId).toObject();
    if (!app.value(QStringLiteral("success")).toBool()) {
        return QString();
    }
    return app.value(QStringLiteral("data")).toObject()
              .value(QStringLiteral("header_image")).toString();
}

QString storeHeaderUrl(const QString& appId)
{
    QByteArray cached;
    if (!JsonDiskCache::load(appDetailsCachePath(appId), cached, kStoreHeaderTtlSecs)) {
        return QString();
    }
    return headerFromAppDetails(cached, appId);
}

QString legacyUrl(const QString& appId)
{
    return QStringLiteral("https://steamcdn-a.akamaihd.net/steam/apps/%1/header.jpg").arg(appId);
}

QString appIdFromUrl(const QString& url)
{
    const QUrl parsed(url);
    if (parsed.scheme() != QLatin1String("https") && parsed.scheme() != QLatin1String("http")) {
        return QString();
    }
    static const QRegularExpression re(QStringLiteral("/steam/apps/(\\d+)/"));
    const QRegularExpressionMatch m = re.match(parsed.path());
    return m.hasMatch() ? m.captured(1) : QString();
}

QString resolve(const QString& appId, const QString& steamRoot)
{
    const QString local = localHeaderPath(appId, steamRoot);
    if (!local.isEmpty()) {
        return QUrl::fromLocalFile(local).toString();
    }
    const QString store = storeHeaderUrl(appId);
    if (!store.isEmpty()) {
        return store;
    }
    return legacyUrl(appId);
}

} // namespace SteamArtwork
