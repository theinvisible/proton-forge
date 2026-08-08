// GOG's content system, read side. This is where most of the risk in the whole
// GOG feature sits, because none of it fails loudly:
//
//   inflateData is the only way any of these bodies become readable at all.
//   sanitizeDepotPath is the boundary between a manifest off the network and
//     the user's filesystem.
//   buildChunkUrl produces a URL that either works or 403s opaquely.
//   secureLinkExpiry decides whether the downloader re-signs in time or
//     discovers the expiry from a 403 an hour into a 60 GB download.
//
// Fixtures live in tests/steam-lab/fixtures/gog and carry the shapes GOG really
// sends, including the awkward ones.

#include <QTest>
#include <QDateTime>
#include <QFile>

#include "gog/GogContentClient.h"

using Build = GogContentClient::Build;
using SecureEndpoint = GogContentClient::SecureEndpoint;

class TstGogContent : public QObject
{
    Q_OBJECT

private slots:
    void inflatesAContentSystemBody();
    void refusesQtsOwnCompressionFormat();
    void refusesTruncatedAndGarbageStreams();

    void splitsHashesIntoTheCdnLayout();
    void buildsMetaUrls();

    void dropsGenerationOneBuilds();
    void picksTheNewestPublicBuild();

    void parsesBuildMetaAndItsDepots();
    void parsesADepotManifest();
    void refusesManifestPathsThatEscape_data();
    void refusesManifestPathsThatEscape();

    void parsesSecureLinkAndOrdersEndpoints();
    void extractsTheTokenExpiry_data();
    void extractsTheTokenExpiry();
    void buildsAChunkUrl();
    void refusesAChunkUrlWithAnUnfilledPlaceholder();

    void survivesGarbage();

private:
    static QByteArray fixture(const QString& name)
    {
        QFile file(QStringLiteral(PROTONFORGE_FIXTURES_DIR) + "/gog/" + name);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    }
};

void TstGogContent::inflatesAContentSystemBody()
{
    const QByteArray compressed = fixture("build-meta.json.zlib");
    QVERIFY2(!compressed.isEmpty(), "fixture missing");

    const QByteArray inflated = GogContentClient::inflateData(compressed);
    QCOMPARE(inflated, fixture("build-meta.json"));
}

void TstGogContent::refusesQtsOwnCompressionFormat()
{
    // The trap this whole function exists to avoid. qCompress prepends a 4-byte
    // length that nothing outside Qt writes, and qUncompress requires it — so a
    // qUncompress() here would silently fail on every real response. Proving we
    // are *not* Qt-compatible is proving we are talking to zlib properly.
    const QByteArray qtStyle = qCompress(QByteArray("hello content system"), 9);
    QVERIFY(GogContentClient::inflateData(qtStyle).isEmpty());
}

void TstGogContent::refusesTruncatedAndGarbageStreams()
{
    const QByteArray good = fixture("build-meta.json.zlib");
    QVERIFY(GogContentClient::inflateData(good.left(good.size() / 2)).isEmpty());

    QVERIFY(GogContentClient::inflateData(QByteArray()).isEmpty());
    QVERIFY(GogContentClient::inflateData("not compressed at all").isEmpty());
    QVERIFY(GogContentClient::inflateData(QByteArray("\x00\x01\x02\x03", 4)).isEmpty());
}

void TstGogContent::splitsHashesIntoTheCdnLayout()
{
    QCOMPARE(GogContentClient::galaxyPath("92ab42631ff4742b309bb62c175e6306"),
             QStringLiteral("92/ab/92ab42631ff4742b309bb62c175e6306"));

    // Too short to split, and already a path: both must come back untouched
    // rather than turning into something malformed.
    QCOMPARE(GogContentClient::galaxyPath("abc"), QStringLiteral("abc"));
    QCOMPARE(GogContentClient::galaxyPath("ab/cd/abcd"), QStringLiteral("ab/cd/abcd"));
}

void TstGogContent::buildsMetaUrls()
{
    QCOMPARE(GogContentClient::metaUrl("92ab42631ff4742b309bb62c175e6306"),
             QStringLiteral("https://cdn.gog.com/content-system/v2/meta/"
                            "92/ab/92ab42631ff4742b309bb62c175e6306"));
    QVERIFY(GogContentClient::metaUrl("").isEmpty());
}

void TstGogContent::dropsGenerationOneBuilds()
{
    const QList<Build> builds = GogContentClient::parseBuilds(fixture("builds-generation2.json"));

    // The fixture has four items, one of them generation 1. That one uses XML
    // manifests and a different CDN layout, so it is dropped here rather than
    // failing later with something unrecognisable.
    QCOMPARE(builds.size(), 3);
    for (const Build& build : builds) {
        QCOMPARE(build.generation, 2);
    }
}

void TstGogContent::picksTheNewestPublicBuild()
{
    const QList<Build> builds = GogContentClient::parseBuilds(fixture("builds-generation2.json"));
    const Build newest = GogContentClient::newestPublicBuild(builds);

    // 2.0-beta is newer by date but sits on a branch the user did not ask for.
    QCOMPARE(newest.versionName, QStringLiteral("1.32"));
    QCOMPARE(newest.buildId, QStringLiteral("55151201432089565"));
    QVERIFY(newest.branch.isEmpty());
    QVERIFY(newest.link.contains("92/ab/"));

    // Nothing to choose from is not a crash.
    QVERIFY(GogContentClient::newestPublicBuild({}).buildId.isEmpty());
}

void TstGogContent::parsesBuildMetaAndItsDepots()
{
    const GogContentClient::BuildMeta meta =
        GogContentClient::parseBuildMeta(fixture("build-meta.json"));

    QVERIFY(meta.valid);
    QCOMPARE(meta.baseProductId, QStringLiteral("1207664663"));
    QCOMPARE(meta.installDirectory, QStringLiteral("The Witcher 3 Wild Hunt"));
    QCOMPARE(meta.platform, QStringLiteral("windows"));
    QCOMPARE(meta.dependencies, QStringList({"MSVC2019", "DirectX"}));
    QCOMPARE(meta.depots.size(), 5);

    QCOMPARE(meta.depots.first().languages, QStringList({"*"}));
    QCOMPARE(meta.depots.first().manifestHash,
             QStringLiteral("1111111111111111aaaaaaaaaaaaaaaa"));
    // A DLC's depot is listed in the base game's build, which is why selection
    // has to filter on ownership rather than trusting the list.
    QCOMPARE(meta.depots.at(3).productId, QStringLiteral("1207664703"));
}

void TstGogContent::parsesADepotManifest()
{
    const GogContentClient::DepotManifest manifest =
        GogContentClient::parseDepotManifest(fixture("depot-manifest.json"));

    QVERIFY(manifest.valid);

    // Every item is kept here, escaping paths included — rejecting them is the
    // planner's job, and this parser's job is to report faithfully.
    QCOMPARE(manifest.items.size(), 7);

    const GogContentClient::DepotItem& exe = manifest.items.first();
    QCOMPARE(exe.path, QStringLiteral("bin/x64/witcher3.exe"));
    QCOMPARE(exe.chunks.size(), 2);
    QCOMPARE(exe.chunks.first().compressedMd5,
             QStringLiteral("c0ffee00000000000000000000000001"));
    QCOMPARE(exe.chunks.first().size, 1000LL);
    QVERIFY(exe.flags.contains("executable"));

    QCOMPARE(manifest.items.at(2).type, QStringLiteral("DepotDirectory"));
    QCOMPARE(manifest.items.at(4).type, QStringLiteral("DepotLink"));
    QCOMPARE(manifest.items.at(4).linkTarget, QStringLiteral("docs/readme.txt"));
    // A file with no chunks is legal — an empty file.
    QVERIFY(manifest.items.at(5).chunks.isEmpty());
}

void TstGogContent::refusesManifestPathsThatEscape_data()
{
    QTest::addColumn<QString>("depotPath");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << "bin/x64/game.exe" << "bin/x64/game.exe";
    QTest::newRow("backslashes, as Windows depots use")
        << "content\\patch0.bundle" << "content/patch0.bundle";
    QTest::newRow("redundant segments collapse") << "./bin//x64/game.exe" << "bin/x64/game.exe";

    // Every one of these is a write outside the install directory.
    QTest::newRow("parent traversal") << "../../../.ssh/authorized_keys" << "";
    QTest::newRow("traversal in the middle") << "a/../../b" << "";
    QTest::newRow("exactly dotdot") << ".." << "";
    QTest::newRow("absolute") << "/etc/passwd" << "";
    QTest::newRow("windows absolute") << "C:\\Windows\\System32\\evil.dll" << "";
    QTest::newRow("windows absolute, forward slashes") << "C:/Windows/evil.dll" << "";
    QTest::newRow("empty") << "" << "";
    QTest::newRow("only separators") << "///" << "";
}

void TstGogContent::refusesManifestPathsThatEscape()
{
    QFETCH(QString, depotPath);
    QFETCH(QString, expected);
    QCOMPARE(GogContentClient::sanitizeDepotPath(depotPath), expected);
}

void TstGogContent::parsesSecureLinkAndOrdersEndpoints()
{
    const QDateTime now = QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC);
    const GogContentClient::SecureLink link =
        GogContentClient::parseSecureLink(fixture("secure-link.json"), now);

    QVERIFY(link.valid);
    QCOMPARE(link.endpoints.size(), 3);

    // Priority order, with the fallback-only endpoint last: this list is the
    // downloader's retry policy, not just a collection.
    QCOMPARE(link.endpoints.at(0).endpointName, QStringLiteral("fastly"));
    QCOMPARE(link.endpoints.at(1).endpointName, QStringLiteral("edgecast"));
    QCOMPARE(link.endpoints.at(2).endpointName, QStringLiteral("gog_cdn"));
    QVERIFY(link.endpoints.at(2).fallbackOnly);

    // The earliest of the endpoint expiries: once one lapses every URL has to be
    // rebuilt anyway.
    QCOMPARE(link.expiresAt, QDateTime::fromSecsSinceEpoch(1700003600, Qt::UTC));
}

void TstGogContent::extractsTheTokenExpiry_data()
{
    QTest::addColumn<QString>("parameterValue");
    QTest::addColumn<qint64>("expected");   // 0 = must return null

    QTest::newRow("akamai style exp=")
        << "exp=1700003600~acl=/*~hmac=deadbeef" << 1700003600LL;
    QTest::newRow("nva= — not valid after")
        << "token=nva=1700003600~dir=%2Fsomething" << 1700003600LL;
    QTest::newRow("already in the past is still an answer")
        << "exp=1000000000~acl=/*" << 1000000000LL;

    // Everything below must return null so the downloader falls back to its
    // conservative fixed interval. A guess here is worse than no answer: it
    // suppresses that fallback.
    QTest::newRow("no token at all") << "" << 0LL;
    QTest::newRow("a token with no timestamp") << "hmac=deadbeef~acl=/*" << 0LL;
    QTest::newRow("an unfamiliar field name") << "validuntil=1700003600" << 0LL;
    QTest::newRow("not a number") << "exp=tomorrow~acl=/*" << 0LL;
    QTest::newRow("too short to be a unix time") << "exp=42~acl=/*" << 0LL;
}

void TstGogContent::extractsTheTokenExpiry()
{
    QFETCH(QString, parameterValue);
    QFETCH(qint64, expected);

    SecureEndpoint endpoint;
    endpoint.urlFormat = "{base_url}/{path}?{token}";
    endpoint.parameters.insert("base_url", "https://cdn.example.com");
    endpoint.parameters.insert("path", "some/path");
    endpoint.parameters.insert("token", parameterValue);

    const QDateTime expiry = GogContentClient::secureLinkExpiry(endpoint);
    if (expected == 0) {
        QVERIFY2(!expiry.isValid(), qPrintable("guessed an expiry from: " + parameterValue));
    } else {
        QCOMPARE(expiry, QDateTime::fromSecsSinceEpoch(expected, Qt::UTC));
    }
}

void TstGogContent::buildsAChunkUrl()
{
    const QDateTime now = QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC);
    const GogContentClient::SecureLink link =
        GogContentClient::parseSecureLink(fixture("secure-link.json"), now);

    const QString url =
        GogContentClient::buildChunkUrl(link.endpoints.at(1),
                                        "c0ffee00000000000000000000000001");

    // The chunk's own galaxy path is appended to the signed path before the
    // format is filled in — the token covers the directory, not the file.
    QCOMPARE(url, QStringLiteral(
        "https://gog-cdn-edgecast.gog.com/content-system/v2/store/1207664663/windows/"
        "c0/ff/c0ffee00000000000000000000000001"
        "?exp=1700007200~acl=/*~hmac=ANOTHERFAKEHMACFORTESTS000000000"));

    QVERIFY(!url.contains('{'));
}

void TstGogContent::refusesAChunkUrlWithAnUnfilledPlaceholder()
{
    SecureEndpoint endpoint;
    endpoint.urlFormat = "{base_url}/{path}?{token}";
    endpoint.parameters.insert("base_url", "https://cdn.example.com");
    endpoint.parameters.insert("path", "store");
    // No "token" parameter — the endpoint described one and did not supply it.

    QVERIFY2(GogContentClient::buildChunkUrl(endpoint, "c0ffee00000000000000000000000001").isEmpty(),
             "a URL with a literal {token} in it earns an opaque 403 far from here");

    QVERIFY(GogContentClient::buildChunkUrl(endpoint, "").isEmpty());
    QVERIFY(GogContentClient::buildChunkUrl(SecureEndpoint(), "c0ffee").isEmpty());
}

void TstGogContent::survivesGarbage()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QByteArray& bad : {QByteArray(""), QByteArray("not json"), QByteArray("[]"),
                                  QByteArray("{}"), QByteArray("<html>404</html>")}) {
        QVERIFY2(GogContentClient::parseBuilds(bad).isEmpty(), "builds: " + bad);
        QVERIFY2(!GogContentClient::parseBuildMeta(bad).valid, "meta: " + bad);
        QVERIFY2(!GogContentClient::parseDepotManifest(bad).valid, "manifest: " + bad);
        QVERIFY2(!GogContentClient::parseSecureLink(bad, now).valid, "secure link: " + bad);
    }
}

QTEST_MAIN(TstGogContent)
#include "tst_gogcontent.moc"
