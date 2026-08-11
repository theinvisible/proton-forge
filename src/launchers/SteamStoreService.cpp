#include "SteamStoreService.h"
#include "core/SecretStore.h"
#include "network/JsonDiskCache.h"
#include "parsers/VDFParser.h"
#include "utils/SteamPaths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

namespace {

const QString kCacheArea = QStringLiteral("steam");
constexpr int kLibraryTtlSecs = 6 * 60 * 60;
// A description, its genres and its languages do not change hour to hour, and
// this endpoint is rate-limited per IP — so a week, not six hours.
constexpr int kDetailsTtlSecs = 7 * 24 * 60 * 60;

// Steam states these as store "categories", mixed in with two dozen entries about
// trading cards, remote play and accessibility. Only the ones that say something
// about playing the game are kept, and they are renamed to GOG's shorter
// vocabulary so the same game reads the same in both stores.
QString canonicalFeature(const QString& category)
{
    if (category == QLatin1String("Single-player"))            return QStringLiteral("Single-player");
    if (category == QLatin1String("Multi-player"))             return QStringLiteral("Multi-player");
    if (category == QLatin1String("PvP"))                      return QStringLiteral("Multi-player");
    if (category == QLatin1String("Co-op"))                    return QStringLiteral("Co-op");
    if (category == QLatin1String("Steam Cloud"))              return QStringLiteral("Cloud saves");
    if (category == QLatin1String("Full controller support"))
        return QStringLiteral("Controller support");
    if (category == QLatin1String("Partial Controller Support"))
        return QStringLiteral("Controller support (partial)");
    return QString();
}

} // namespace

SteamStoreService::SteamStoreService()
    : m_networkManager(new QNetworkAccessManager(this))
{
}

bool SteamStoreService::isAuthenticated() const
{
    // Read from SecretStore's in-memory cache — this is called while the dialog
    // paints, so it must not block.
    return !SecretStore::instance().value(SecretStore::Key::SteamWebApiKey).trimmed().isEmpty();
}

QString SteamStoreService::authenticationHint() const
{
    return QStringLiteral(
        "Steam has no sign-in here. Add your Steam Web API key in Settings → Steam "
        "to see the games you own but have not installed.");
}

QString SteamStoreService::headerImageUrl(const QString& appId)
{
    // The same CDN pattern SteamLauncher already uses for installed games, so
    // ImageCache treats both identically.
    return QStringLiteral("https://steamcdn-a.akamaihd.net/steam/apps/%1/header.jpg").arg(appId);
}

QString SteamStoreService::steamIdFromLoginUsers(const QByteArray& vdf)
{
    VDFParser parser;
    if (!parser.parse(QString::fromUtf8(vdf))) {
        return QString();
    }

    // "users" { "<steamid64>" { "AccountName" "..." "MostRecent" "1" } }
    VDFNode root = parser.root();
    if (!root.hasChild(QStringLiteral("users"))) {
        return QString();
    }

    const QMap<QString, VDFNode> users = root.child(QStringLiteral("users")).children();
    if (users.isEmpty()) {
        return QString();
    }

    for (auto it = users.constBegin(); it != users.constEnd(); ++it) {
        if (it.value().getString(QStringLiteral("MostRecent")) == QLatin1String("1")) {
            return it.key();
        }
    }

    // Nobody flagged: unambiguous only when there is exactly one account. With
    // several and no flag, guessing would silently show a stranger's library.
    return users.size() == 1 ? users.firstKey() : QString();
}

QString SteamStoreService::resolveSteamId()
{
    const QString configured = QSettings().value(QStringLiteral("steam/steamId64")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    const QString path = SteamPaths::loginUsersPath();
    if (path.isEmpty()) {
        return QString();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return steamIdFromLoginUsers(file.readAll());
}

QList<StoreEntry> SteamStoreService::parseOwnedGames(const QByteArray& json, int* gameCount)
{
    QList<StoreEntry> entries;

    const QJsonObject response =
        QJsonDocument::fromJson(json).object().value(QStringLiteral("response")).toObject();
    if (gameCount) {
        *gameCount = response.value(QStringLiteral("game_count")).toInt(0);
    }

    const QJsonArray games = response.value(QStringLiteral("games")).toArray();
    for (const QJsonValue& value : games) {
        const QJsonObject game = value.toObject();
        const QString appId = game.value(QStringLiteral("appid")).toVariant().toString();
        if (appId.isEmpty()) {
            continue;
        }

        StoreEntry entry;
        entry.id = appId;
        entry.title = game.value(QStringLiteral("name")).toString();
        entry.imageUrl = headerImageUrl(appId);
        entry.storeUrl = QStringLiteral("https://store.steampowered.com/app/%1").arg(appId);
        // We never install a Steam game ourselves; the client does.
        entry.installUrl = QStringLiteral("steam://install/%1").arg(appId);
        entry.installable = true;
        // The Web API says nothing about platforms, and guessing would put a
        // wrong badge on every row. Left false rather than invented.
        entries.append(entry);
    }

    // Here rather than at the two emit sites, so the cache path and the network
    // path cannot disagree.
    sortEntriesByTitle(entries);
    return entries;
}

QStringList SteamStoreService::parseSupportedLanguages(const QString& html, QStringList* voiced)
{
    if (voiced) {
        voiced->clear();
    }

    // The footnote explaining the asterisk is not a language. Cut at the <br>
    // that introduces it rather than trying to recognise its wording, which is
    // translated along with the rest of the store.
    QString list = html;
    const int footnote = list.indexOf(QLatin1String("<br"), 0, Qt::CaseInsensitive);
    if (footnote >= 0) {
        list = list.left(footnote);
    }

    QStringList languages;
    for (const QString& raw : list.split(QLatin1Char(','))) {
        // The asterisk is wrapped in <strong> and marks full audio. Note it
        // before the tags go, then strip everything that looks like markup.
        const bool hasAudio = raw.contains(QLatin1Char('*'));
        QString name = raw;
        name.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
        name.remove(QLatin1Char('*'));
        name = name.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        languages << name;
        if (hasAudio && voiced) {
            *voiced << name;
        }
    }
    return languages;
}

StoreEntryDetails SteamStoreService::parseAppDetails(const QByteArray& json, const QString& appId)
{
    StoreEntryDetails details;

    const QJsonObject wrapper = QJsonDocument::fromJson(json).object().value(appId).toObject();
    // success=false in a 200 body is how this endpoint says "no such app, or not
    // visible in this region". Nothing to show, and nothing worth an error.
    if (!wrapper.value(QStringLiteral("success")).toBool()) {
        return details;
    }
    const QJsonObject data = wrapper.value(QStringLiteral("data")).toObject();
    if (data.isEmpty()) {
        return details;
    }

    details.shortDescription = data.value(QStringLiteral("short_description")).toString();

    for (const QJsonValue& value : data.value(QStringLiteral("genres")).toArray()) {
        const QString name = value.toObject().value(QStringLiteral("description")).toString();
        if (!name.isEmpty()) {
            details.genres << name;
        }
    }

    bool achievementCategory = false;
    for (const QJsonValue& value : data.value(QStringLiteral("categories")).toArray()) {
        const QString category = value.toObject().value(QStringLiteral("description")).toString();
        if (category == QLatin1String("Steam Achievements")) {
            achievementCategory = true;
            continue;
        }
        const QString feature = canonicalFeature(category);
        if (!feature.isEmpty() && !details.features.contains(feature)) {
            details.features << feature;
        }
    }

    details.achievementCount =
        data.value(QStringLiteral("achievements")).toObject()
            .value(QStringLiteral("total")).toInt(0);
    // Either source is enough: a game can carry the category with the count
    // missing from this response, and vice versa.
    details.hasAchievements = achievementCategory || details.achievementCount > 0;

    details.languages = parseSupportedLanguages(
        data.value(QStringLiteral("supported_languages")).toString(), &details.voiceLanguages);

    details.releaseDate = data.value(QStringLiteral("release_date")).toObject()
                              .value(QStringLiteral("date")).toString();
    for (const QJsonValue& value : data.value(QStringLiteral("developers")).toArray()) {
        details.developers << value.toString();
    }
    for (const QJsonValue& value : data.value(QStringLiteral("publishers")).toArray()) {
        details.publishers << value.toString();
    }

    const QJsonObject platforms = data.value(QStringLiteral("platforms")).toObject();
    details.supportsWindows = platforms.value(QStringLiteral("windows")).toBool();
    details.supportsLinux = platforms.value(QStringLiteral("linux")).toBool();
    details.supportsMac = platforms.value(QStringLiteral("mac")).toBool();

    details.valid = true;
    return details;
}

void SteamStoreService::fetchDetails(const QString& id)
{
    if (id.isEmpty()) {
        emit detailsFailed(id, QStringLiteral("No app id."));
        return;
    }

    const QString cachePath =
        JsonDiskCache::filePath(kCacheArea, QStringLiteral("appdetails-") + id);

    QByteArray cached;
    if (JsonDiskCache::load(cachePath, cached, kDetailsTtlSecs)) {
        const StoreEntryDetails details = parseAppDetails(cached, id);
        if (details.valid) {
            emit detailsReady(id, details);
            return;
        }
    }

    // The storefront endpoint, not the Web API: no key, and l=english so the
    // languages come back under names this parser can also read back.
    QUrl url(QStringLiteral("https://store.steampowered.com/api/appdetails"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("appids"), id);
    query.addQueryItem(QStringLiteral("l"), QStringLiteral("english"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "ProtonForge");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, cachePath]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit detailsFailed(id, QStringLiteral("Could not reach the Steam store page data."));
            return;
        }

        const QByteArray body = reply->readAll();
        const StoreEntryDetails details = parseAppDetails(body, id);
        if (!details.valid) {
            emit detailsFailed(id, QStringLiteral("Steam has no store details for this title."));
            return;
        }

        JsonDiskCache::save(cachePath, body);
        emit detailsReady(id, details);
    });
}

void SteamStoreService::fetchLibrary()
{
    const QString key = SecretStore::instance().value(SecretStore::Key::SteamWebApiKey).trimmed();
    if (key.isEmpty()) {
        emit libraryFailed(authenticationHint());
        return;
    }

    const QString steamId = resolveSteamId();
    if (steamId.isEmpty()) {
        emit libraryFailed(QStringLiteral(
            "ProtonForge could not work out which Steam account to ask about. "
            "Enter your SteamID64 in Settings → Steam."));
        return;
    }

    const QString cachePath = JsonDiskCache::filePath(kCacheArea, QStringLiteral("owned-") + steamId);

    QByteArray cached;
    if (JsonDiskCache::load(cachePath, cached, kLibraryTtlSecs)) {
        int count = 0;
        const QList<StoreEntry> entries = parseOwnedGames(cached, &count);
        if (!entries.isEmpty()) {
            emit libraryReady(entries);
            return;
        }
    }

    QUrl url(QStringLiteral("https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key);
    query.addQueryItem(QStringLiteral("steamid"), steamId);
    query.addQueryItem(QStringLiteral("include_appinfo"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("include_played_free_games"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "ProtonForge");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cachePath]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // The key is a query parameter, so it is in the reply's URL and
            // therefore in errorString(). Never let that reach the user.
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit libraryFailed(status == 403
                ? QStringLiteral("Steam rejected the Web API key. Check it in Settings → Steam.")
                : QStringLiteral("Could not reach Steam (HTTP %1).").arg(status));
            return;
        }

        const QByteArray body = reply->readAll();
        int count = 0;
        const QList<StoreEntry> entries = parseOwnedGames(body, &count);

        if (entries.isEmpty()) {
            // Almost always the privacy setting rather than an empty account,
            // and the API gives no way to tell them apart — so say the thing
            // the user can act on instead of showing a blank list.
            emit libraryFailed(QStringLiteral(
                "Steam returned no games. Check that 'Game details' is set to Public "
                "in your Steam privacy settings."));
            return;
        }

        JsonDiskCache::save(cachePath, body);
        emit libraryReady(entries);
    });
}
