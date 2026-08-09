// GOG's account endpoints, parsed. All of this is data shape rather than
// logic, which is exactly why it needs pinning: a field that quietly becomes
// empty produces a library with no artwork or a product that cannot be
// installed, and neither looks like a parsing bug from the outside.
//
// normalizeImageUrl gets the most attention because GOG serves artwork
// protocol-relative and without an extension, so the raw value is never usable
// as-is.

#include <QTest>

#include "gog/GogApiClient.h"

class TstGogApi : public QObject
{
    Q_OBJECT

private slots:
    void parsesOwnedIds();
    void parsesAPageOfProducts();
    void reportsHowManyPagesThereAre();
    void skipsProductsWithoutAnId();
    void parsesProductDetail();
    void separatesContentSystemFromStorePlatforms();

    void normalizesImageUrls_data();
    void normalizesImageUrls();

    void buildsBannerUrls_data();
    void buildsBannerUrls();

    void buildsStoreUrls();
    void survivesGarbage();
};

void TstGogApi::parsesOwnedIds()
{
    // GOG sends these as numbers; everything downstream keys on strings, so the
    // conversion has to happen here rather than at each call site.
    const QByteArray json = R"({"owned": [1207664663, 1495134320, 2093619782]})";
    QCOMPARE(GogApiClient::parseOwnedIds(json),
             QStringList({"1207664663", "1495134320", "2093619782"}));
}

void TstGogApi::parsesAPageOfProducts()
{
    const QByteArray json = R"({
        "totalPages": 1,
        "products": [
            {
                "id": 1207664663,
                "title": "The Witcher 3: Wild Hunt",
                "slug": "the_witcher_3_wild_hunt",
                "image": "//images.gog-statics.com/abc123def456",
                "worksOn": {"Windows": true, "Mac": false, "Linux": true}
            }
        ]
    })";

    int pages = 0;
    const QList<GogApiClient::Product> products =
        GogApiClient::parseFilteredProducts(json, &pages);

    QCOMPARE(products.size(), 1);
    QCOMPARE(products.first().id, QStringLiteral("1207664663"));
    QCOMPARE(products.first().title, QStringLiteral("The Witcher 3: Wild Hunt"));
    QCOMPARE(products.first().slug, QStringLiteral("the_witcher_3_wild_hunt"));
    QVERIFY(products.first().supportsWindows);
    QVERIFY(products.first().supportsLinux);
    QVERIFY(!products.first().supportsMac);
    QCOMPARE(products.first().imageUrl,
             QStringLiteral("https://images.gog-statics.com/abc123def456_product_tile_256.jpg"));
}

void TstGogApi::reportsHowManyPagesThereAre()
{
    // The caller keeps asking until it has them all, so this number decides
    // whether a large library arrives complete or truncated at fifty.
    int pages = 0;
    GogApiClient::parseFilteredProducts(R"({"totalPages": 7, "products": []})", &pages);
    QCOMPARE(pages, 7);

    // Absent means one page, not zero — zero would stop before the first.
    GogApiClient::parseFilteredProducts(R"({"products": []})", &pages);
    QCOMPARE(pages, 1);
}

void TstGogApi::skipsProductsWithoutAnId()
{
    const QByteArray json = R"({
        "products": [
            {"title": "No id here"},
            {"id": 42, "title": "Fine"}
        ]
    })";

    int pages = 0;
    const QList<GogApiClient::Product> products =
        GogApiClient::parseFilteredProducts(json, &pages);

    // An entry with no id cannot be installed, looked up or told apart from
    // another, so it is not a product.
    QCOMPARE(products.size(), 1);
    QCOMPARE(products.first().id, QStringLiteral("42"));
}

void TstGogApi::parsesProductDetail()
{
    const QByteArray json = R"({
        "id": 1207664663,
        "title": "The Witcher 3",
        "slug": "the_witcher_3_wild_hunt",
        "images": {"logo2x": "//images-2.gog-statics.com/1db8a603abf8305f210da1f9b9d2ecd3132354642a5baab1ac5feb773204262e_glx_logo_2x.jpg"},
        "content_system_compatibility": {"windows": true, "osx": false, "linux": false},
        "expanded_dlcs": [{"id": 1207664703}, {"id": 1207664713}]
    })";

    const GogApiClient::ProductDetail detail = GogApiClient::parseProduct(json);

    QVERIFY(detail.valid);
    QCOMPARE(detail.id, QStringLiteral("1207664663"));
    QCOMPARE(detail.dlcIds, QStringList({"1207664703", "1207664713"}));
    // The wide banner, not the logo GOG names in this field: logo2x is 200x120
    // where the game list's tile and the detail panel are both cut for a Steam
    // header's proportions. Rebuilt from the hash, because the value arrives
    // complete with its own suffix.
    QCOMPARE(detail.imageUrl,
             QStringLiteral("https://images.gog-statics.com/"
                            "1db8a603abf8305f210da1f9b9d2ecd3132354642a5baab1ac5feb773204262e"
                            "_product_tile_256.jpg"));
}

void TstGogApi::separatesContentSystemFromStorePlatforms()
{
    // The distinction that decides whether ProtonForge can install a game at
    // all: a product's store page can offer Linux while the Galaxy content
    // system serves only Windows, because GOG ships Linux as .sh installers.
    const QByteArray json = R"({
        "id": 1,
        "content_system_compatibility": {"windows": true, "linux": false}
    })";

    const GogApiClient::ProductDetail detail = GogApiClient::parseProduct(json);
    QVERIFY(detail.contentSystemWindows);
    QVERIFY(!detail.contentSystemLinux);
}

void TstGogApi::normalizesImageUrls_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("variant");
    QTest::addColumn<QString>("expected");

    QTest::newRow("protocol-relative, the usual case")
        << "//images.gog-statics.com/abc123" << "_product_tile_256.jpg"
        << "https://images.gog-statics.com/abc123_product_tile_256.jpg";
    QTest::newRow("already absolute")
        << "https://images.gog-statics.com/abc123" << "_tile.jpg"
        << "https://images.gog-statics.com/abc123_tile.jpg";
    QTest::newRow("a bare hash gets a host")
        << "abc123" << "_tile.jpg"
        << "https://images.gog-statics.com/abc123_tile.jpg";
    QTest::newRow("already has an extension, left alone")
        << "//images.gog-statics.com/abc123.jpg" << "_tile.jpg"
        << "https://images.gog-statics.com/abc123.jpg";
    QTest::newRow("no variant wanted")
        << "//images.gog-statics.com/abc123" << ""
        << "https://images.gog-statics.com/abc123";
    // A game with no artwork must stay without artwork, not acquire a URL that
    // 404s and leaves the tile shimmering forever.
    QTest::newRow("empty stays empty") << "" << "_tile.jpg" << "";
}

void TstGogApi::normalizesImageUrls()
{
    QFETCH(QString, raw);
    QFETCH(QString, variant);
    QFETCH(QString, expected);
    QCOMPARE(GogApiClient::normalizeImageUrl(raw, variant), expected);
}

void TstGogApi::buildsBannerUrls_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("expected");

    const QString hash =
        QStringLiteral("1db8a603abf8305f210da1f9b9d2ecd3132354642a5baab1ac5feb773204262e");
    const QString tile =
        QStringLiteral("https://images.gog-statics.com/%1_product_tile_256.jpg").arg(hash);

    // The shape that made this function necessary: a complete URL carrying the
    // wrong variant. normalizeImageUrl would hand it straight back, because
    // appending a second suffix to something ending in .jpg produces a 404.
    QTest::newRow("a logo URL becomes the banner")
        << QStringLiteral("//images-2.gog-statics.com/%1_glx_logo_2x.jpg").arg(hash) << tile;
    QTest::newRow("any other variant, same answer")
        << QStringLiteral("https://images.gog-statics.com/%1_bg_crop_1366x655.jpg").arg(hash)
        << tile;
    // Idempotent: re-reading a cached product must not compound suffixes.
    QTest::newRow("already the banner") << tile << tile;
    QTest::newRow("a bare hash, as the library listing sends it") << hash << tile;
    // The numbered image hosts are interchangeable; one host in the output
    // keeps ImageCache from holding the same picture several times over.
    QTest::newRow("host is normalised away")
        << QStringLiteral("//images-4.gog-statics.com/%1_glx_logo_2x.jpg").arg(hash) << tile;

    // No hash to rebuild from. Handing it back is the honest answer — inventing
    // one would produce a confident 404 and a tile that shimmers forever.
    QTest::newRow("no hash, left as it came")
        << QStringLiteral("//images.gog-statics.com/logo2x.png")
        << QStringLiteral("https://images.gog-statics.com/logo2x.png");
    QTest::newRow("empty stays empty") << QString() << QString();
}

void TstGogApi::buildsBannerUrls()
{
    QFETCH(QString, raw);
    QFETCH(QString, expected);
    QCOMPARE(GogApiClient::bannerImageUrl(raw), expected);
}

void TstGogApi::buildsStoreUrls()
{
    QCOMPARE(GogApiClient::storeUrl("the_witcher_3_wild_hunt"),
             QStringLiteral("https://www.gog.com/game/the_witcher_3_wild_hunt"));
    // No slug is not a reason to build a broken link.
    QCOMPARE(GogApiClient::storeUrl(""), QStringLiteral("https://www.gog.com/games"));
}

void TstGogApi::survivesGarbage()
{
    for (const QByteArray& bad : {QByteArray(""), QByteArray("not json"), QByteArray("[]"),
                                  QByteArray("{}"), QByteArray("<html>404</html>")}) {
        QVERIFY2(GogApiClient::parseOwnedIds(bad).isEmpty(), "owned ids: " + bad);

        int pages = 0;
        QVERIFY2(GogApiClient::parseFilteredProducts(bad, &pages).isEmpty(), "products: " + bad);

        QVERIFY2(!GogApiClient::parseProduct(bad).valid, "product: " + bad);
    }
}

QTEST_MAIN(TstGogApi)
#include "tst_gogapi.moc"
