// Where a Steam game's banner comes from. The URL used to be derived from the
// appid alone, and for newer titles that URL is a 404 — Corsair Cove and
// Graveyard Keeper 2 showed a placeholder. These tests pin the order the
// banner is looked for in, and the shapes of Steam's librarycache that the
// local lookup has to cope with (all of them seen on a real install).

#include <QTest>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include "launchers/SteamArtwork.h"
#include "network/JsonDiskCache.h"

class TstSteamArtwork : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void localHeaderInTheAppDirectory();
    void localHeaderInAHashDirectory();
    void newestLocalHeaderWins();
    void localizedHeaderAsLastResort();
    void noLocalHeader();

    void headerFromAppDetails_data();
    void headerFromAppDetails();

    void appIdFromUrl_data();
    void appIdFromUrl();

    void resolveOrder();

private:
    // <root>/appcache/librarycache/<appId>/<relative>, with an mtime offset.
    QString touch(const QString& appId, const QString& relative, int ageSecs = 0);
    QString root() const { return m_root.path(); }

    QTemporaryDir m_root;
};

void TstSteamArtwork::initTestCase()
{
    // storeHeaderUrl() reads JsonDiskCache, under CacheLocation.
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_root.isValid());
}

void TstSteamArtwork::cleanupTestCase()
{
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
}

QString TstSteamArtwork::touch(const QString& appId, const QString& relative, int ageSecs)
{
    const QString path = root() + "/appcache/librarycache/" + appId + "/" + relative;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write("jpg");
    file.flush();   // or close() writes after the time is set, and resets it
    file.setFileTime(QDateTime::currentDateTime().addSecs(-ageSecs),
                     QFileDevice::FileModificationTime);
    file.close();
    return path;
}

void TstSteamArtwork::localHeaderInTheAppDirectory()
{
    // The common case: 1899 of 2492 apps on a real install.
    const QString path = touch("100", "header.jpg");
    touch("100", "library_600x900.jpg");
    QCOMPARE(SteamArtwork::localHeaderPath("100", root()), path);
}

void TstSteamArtwork::localHeaderInAHashDirectory()
{
    // How newer titles are kept, e.g. Corsair Cove.
    touch("1368140", "67f9cd820d47106dbc398e5624550833c4d01a19.jpg");   // the icon
    touch("1368140", "d790f92721b98aa82a07f74d6aebb77f27869574/library_hero.jpg");
    const QString path = touch("1368140", "cafda7c6ff9641cc9ede1823317b835a2b3f38ab/library_header.jpg");
    QCOMPARE(SteamArtwork::localHeaderPath("1368140", root()), path);
}

void TstSteamArtwork::newestLocalHeaderWins()
{
    // New artwork leaves the old copy behind.
    touch("200", "header.jpg", 30 * 24 * 3600);
    touch("200", "aaaa/header.jpg", 10 * 24 * 3600);
    const QString path = touch("200", "bbbb/library_header.jpg", 60);
    QCOMPARE(SteamArtwork::localHeaderPath("200", root()), path);
}

void TstSteamArtwork::localizedHeaderAsLastResort()
{
    touch("300", "library_600x900.jpg");
    const QString path = touch("300", "header_german.jpg");
    QCOMPARE(SteamArtwork::localHeaderPath("300", root()), path);
}

void TstSteamArtwork::noLocalHeader()
{
    touch("400", "library_600x900.jpg");
    QVERIFY(SteamArtwork::localHeaderPath("400", root()).isEmpty());
    QVERIFY(SteamArtwork::localHeaderPath("999999", root()).isEmpty());
    QVERIFY(SteamArtwork::localHeaderPath("100", QString()).isEmpty());   // no Steam
}

void TstSteamArtwork::headerFromAppDetails_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("expected");

    const QString url = QStringLiteral(
        "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1368140/"
        "cafda7c6ff9641cc9ede1823317b835a2b3f38ab/header.jpg?t=1791278917");
    QTest::newRow("answer")
        << QByteArray(R"({"1368140":{"success":true,"data":{"type":"game","header_image":")")
               + url.toUtf8() + R"("}}})"
        << url;
    QTest::newRow("unknown app") << QByteArray(R"({"1368140":{"success":false}})") << QString();
    QTest::newRow("other app")
        << QByteArray(R"({"10":{"success":true,"data":{"header_image":"x"}}})") << QString();
    QTest::newRow("no field")
        << QByteArray(R"({"1368140":{"success":true,"data":{}}})") << QString();
    QTest::newRow("not json") << QByteArray("<html>rate limited</html>") << QString();
}

void TstSteamArtwork::headerFromAppDetails()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, expected);
    QCOMPARE(SteamArtwork::headerFromAppDetails(json, "1368140"), expected);
}

void TstSteamArtwork::appIdFromUrl_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("appId");

    QTest::newRow("old scheme")
        << "https://steamcdn-a.akamaihd.net/steam/apps/1368140/header.jpg" << "1368140";
    QTest::newRow("store assets")
        << "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/4358690/"
           "d63c37d6289ef7c48d89c0521fb82ac51d7f5ce8/header.jpg?t=1791579476"
        << "4358690";
    QTest::newRow("gog") << "https://images.gog-statics.com/abc_product_card_v2_mobile_slider_639.jpg"
                         << QString();
    QTest::newRow("local file")
        << "file:///home/u/.local/share/Steam/appcache/librarycache/1368140/header.jpg"
        << QString();
}

void TstSteamArtwork::appIdFromUrl()
{
    QFETCH(QString, url);
    QFETCH(QString, appId);
    QCOMPARE(SteamArtwork::appIdFromUrl(url), appId);
}

void TstSteamArtwork::resolveOrder()
{
    // 1. Steam's own copy, as a file:// URL.
    const QString local = touch("500", "header.jpg");
    QCOMPARE(SteamArtwork::resolve("500", root()), QUrl::fromLocalFile(local).toString());

    // 3. Nothing known: the old scheme.
    QCOMPARE(SteamArtwork::resolve("600", root()), SteamArtwork::legacyUrl("600"));

    // 2. A cached store answer, when there is no local copy.
    const QString url = QStringLiteral("https://shared.akamai.steamstatic.com/x/600/h.jpg");
    JsonDiskCache::save(SteamArtwork::appDetailsCachePath("600"),
                        R"({"600":{"success":true,"data":{"header_image":")" + url.toUtf8() + R"("}}})");
    QCOMPARE(SteamArtwork::storeHeaderUrl("600"), url);
    QCOMPARE(SteamArtwork::resolve("600", root()), url);

    // ...but the local copy still comes first.
    JsonDiskCache::save(SteamArtwork::appDetailsCachePath("500"),
                        R"({"500":{"success":true,"data":{"header_image":"https://x/500.jpg"}}})");
    QCOMPARE(SteamArtwork::resolve("500", root()), QUrl::fromLocalFile(local).toString());
}

QTEST_MAIN(TstSteamArtwork)
#include "tst_steamartwork.moc"
