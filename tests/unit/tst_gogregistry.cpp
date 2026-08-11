// What ProtonForge believes it owns on disk, and what it will therefore delete.
//
// Two subjects, and they are the same subject: the registry is the only record
// of which directories are ours, and isSafeToDiscard is what stands between a
// wrong answer and removeRecursively() on a path a user typed.
//
// The registry rules that fail quietly:
//   An incomplete entry is a download in progress, not a game — listing it
//     would offer a half-written executable to launch.
//   hasUpdate compares for inequality: build ids are opaque strings, and an
//     unchecked entry is neither current nor outdated.
//   installedAt must survive an update; only updatedAt moves.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "gog/GogDownloader.h"
#include "gog/GogInstallRegistry.h"

using Entry = GogInstallRegistry::Entry;

class TstGogRegistry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void readsARegistryFile();
    void roundTripsThroughJson();
    void remembersTheArtworkItWasGiven();
    void refusesToForgetArtworkItAlreadyHas();
    void survivesGarbage_data();
    void survivesGarbage();
    void dropsRowsWithNothingToActOn();

    void hidesIncompleteInstallsFromDiscovery();
    void spotsAnUpdate_data();
    void spotsAnUpdate();

    void keepsTheOriginalInstallDate();
    void removesWhatItIsAskedTo();

    void derivesTheInstallLayout();
    void honoursAConfiguredInstallRoot();

    void refusesToDiscardDangerousPaths_data();
    void refusesToDiscardDangerousPaths();
    void discardsADirectoryCarryingOurJournal();

private:
    static QByteArray fixture(const QString& name)
    {
        QFile file(QStringLiteral(PROTONFORGE_FIXTURES_DIR) + "/gog/" + name);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    QTemporaryDir m_home;
};

void TstGogRegistry::initTestCase()
{
    // A fake HOME, so nothing here can see or touch the developer's real
    // installs — and so installRoot() has a predictable answer.
    QVERIFY(m_home.isValid());
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/.config").toUtf8());
    QStandardPaths::setTestModeEnabled(true);

    QCoreApplication::setOrganizationName("ProtonForgeTest");
    QCoreApplication::setApplicationName("ProtonForgeTest");
}

void TstGogRegistry::readsARegistryFile()
{
    const QList<Entry> entries = GogInstallRegistry::parse(fixture("gog-installs.json"));

    QCOMPARE(entries.size(), 3);

    const Entry witcher = entries.first();
    QVERIFY(witcher.valid);
    QCOMPARE(witcher.productId, QStringLiteral("1207658930"));
    QCOMPARE(witcher.title, QStringLiteral("The Witcher 2: Assassins of Kings"));
    QCOMPARE(witcher.platform, QStringLiteral("windows"));
    QCOMPARE(witcher.launchArgs, QStringList({"-launcher"}));
    QVERIFY(witcher.complete);
    QVERIFY(!witcher.nativeLinux);
    // Verbatim what GogInstallPlan produces — the fixture is a record of the
    // real string, not a paraphrase of it.
    QCOMPARE(witcher.warnings,
             QStringList({"This build lists redistributables (MSVC2019, DirectX). ProtonForge "
                          "does not run them; Proton provides its own."}));
    QCOMPARE(witcher.size, 26214400000LL);   // over 4 GB: not an int
    QVERIFY(witcher.installedAt.isValid());

    QVERIFY(entries.at(1).nativeLinux);
    QVERIFY(!entries.at(2).complete);
}

void TstGogRegistry::roundTripsThroughJson()
{
    const QList<Entry> original = GogInstallRegistry::parse(fixture("gog-installs.json"));
    const QList<Entry> again =
        GogInstallRegistry::parse(GogInstallRegistry::serialize(original));

    QCOMPARE(again.size(), original.size());
    for (int i = 0; i < again.size(); ++i) {
        QCOMPARE(again.at(i).productId, original.at(i).productId);
        QCOMPARE(again.at(i).installPath, original.at(i).installPath);
        QCOMPARE(again.at(i).buildId, original.at(i).buildId);
        QCOMPARE(again.at(i).size, original.at(i).size);
        QCOMPARE(again.at(i).complete, original.at(i).complete);
        QCOMPARE(again.at(i).launchArgs, original.at(i).launchArgs);
        QCOMPARE(again.at(i).nativeLinux, original.at(i).nativeLinux);
        // Warnings have to survive a reload — they are the whole point of
        // storing them rather than showing them once during the install.
        QCOMPARE(again.at(i).warnings, original.at(i).warnings);
        QCOMPARE(again.at(i).installedAt, original.at(i).installedAt);
        // Likewise the banner: it is here so that discovery, which runs on a
        // worker thread and may not fetch, has an answer without asking anyone.
        QCOMPARE(again.at(i).imageUrl, original.at(i).imageUrl);
    }
}

void TstGogRegistry::remembersTheArtworkItWasGiven()
{
    const QString url = QStringLiteral("https://images.gog-statics.com/abc_product_tile_256.jpg");

    Entry entry;
    entry.productId = "1207658930";
    entry.installPath = "/home/user/Games/ProtonForge/GOG/Game";
    entry.imageUrl = url;

    const QList<Entry> back =
        GogInstallRegistry::parse(GogInstallRegistry::serialize({entry}));
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first().imageUrl, url);

    // Every registry written before artwork was recorded has no such field, and
    // that absence is what tells the store service to go and look one up. It
    // must read as empty rather than failing the row or inventing a URL.
    const QByteArray old = R"({"version":1,"installs":[{
        "productId":"1207658930",
        "installPath":"/home/user/Games/ProtonForge/GOG/Game",
        "complete":true}]})";
    const QList<Entry> legacy = GogInstallRegistry::parse(old);
    QCOMPARE(legacy.size(), 1);
    QVERIFY(legacy.first().valid);
    QVERIFY(legacy.first().imageUrl.isEmpty());
}

void TstGogRegistry::refusesToForgetArtworkItAlreadyHas()
{
    // setImageUrl is what the lookups write through, and they report failure by
    // handing back an empty string. Letting that through would blank a banner
    // every time GOG was briefly unreachable.
    const QString url = QStringLiteral("https://images.gog-statics.com/abc_product_tile_256.jpg");

    Entry entry;
    entry.productId = "1207658930";
    entry.installPath = m_home.path() + "/Games/ProtonForge/GOG/Game";
    entry.complete = true;

    GogInstallRegistry& registry = GogInstallRegistry::instance();
    registry.load();
    QVERIFY(registry.put(entry));

    QVERIFY(registry.setImageUrl(entry.productId, url));
    QCOMPARE(registry.entry(entry.productId).imageUrl, url);

    // Both of these must report "nothing changed", because both mean there is
    // nothing new to repaint.
    QVERIFY(!registry.setImageUrl(entry.productId, QString()));
    QCOMPARE(registry.entry(entry.productId).imageUrl, url);
    QVERIFY(!registry.setImageUrl(entry.productId, url));

    // And a product we never installed is not ours to record anything about.
    QVERIFY(!registry.setImageUrl(QStringLiteral("999"), url));
}

void TstGogRegistry::survivesGarbage_data()
{
    QTest::addColumn<QByteArray>("json");

    QTest::newRow("empty")     << QByteArray();
    QTest::newRow("not json")  << QByteArray("not json");
    QTest::newRow("array")     << QByteArray("[]");
    QTest::newRow("object")    << QByteArray("{}");
    QTest::newRow("html")      << QByteArray("<html>404</html>");
    QTest::newRow("truncated") << QByteArray("{\"installs\": [{\"productId\": \"1\"");
}

void TstGogRegistry::survivesGarbage()
{
    QFETCH(QByteArray, json);
    QVERIFY(GogInstallRegistry::parse(json).isEmpty());
}

void TstGogRegistry::dropsRowsWithNothingToActOn()
{
    // No path means nothing to launch and nothing to delete. Half-honouring
    // such a row is how an uninstall ends up with no idea what to remove.
    const QByteArray json = R"({"installs": [
        {"productId": "1", "title": "No path"},
        {"installPath": "/games/x", "title": "No id"},
        {"productId": "2", "installPath": "/games/y"}
    ]})";

    const QList<Entry> entries = GogInstallRegistry::parse(json);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().productId, QStringLiteral("2"));
}

void TstGogRegistry::hidesIncompleteInstallsFromDiscovery()
{
    GogInstallRegistry& registry = GogInstallRegistry::instance();
    registry.load();

    for (const Entry& entry : GogInstallRegistry::parse(fixture("gog-installs.json"))) {
        Entry copy = entry;
        copy.installedAt = entry.installedAt;
        QVERIFY(registry.put(copy));
    }

    QCOMPARE(registry.entries().size(), 3);
    // The half-downloaded one is in the registry — that is what makes it
    // resumable — but it is not a game anyone can start.
    QCOMPARE(registry.completeEntries().size(), 2);
    for (const Entry& entry : registry.completeEntries()) {
        QVERIFY(entry.productId != QLatin1String("1207999999"));
    }
}

void TstGogRegistry::spotsAnUpdate_data()
{
    QTest::addColumn<QString>("installed");
    QTest::addColumn<QString>("latest");
    QTest::addColumn<bool>("complete");
    QTest::addColumn<bool>("expected");

    QTest::newRow("same")           << "100" << "100" << true  << false;
    QTest::newRow("newer")          << "100" << "200" << true  << true;
    // Build ids are opaque, so a "lower" one is still not what is installed.
    QTest::newRow("rolled back")    << "200" << "100" << true  << true;
    QTest::newRow("never checked")  << "100" << ""    << true  << false;
    QTest::newRow("nothing local")  << ""    << "200" << true  << false;
    QTest::newRow("still installing") << "100" << "200" << false << false;
}

void TstGogRegistry::spotsAnUpdate()
{
    QFETCH(QString, installed);
    QFETCH(QString, latest);
    QFETCH(bool, complete);
    QFETCH(bool, expected);

    Entry entry;
    entry.buildId = installed;
    entry.latestBuildId = latest;
    entry.complete = complete;

    QCOMPARE(GogInstallRegistry::hasUpdate(entry), expected);
}

void TstGogRegistry::keepsTheOriginalInstallDate()
{
    GogInstallRegistry& registry = GogInstallRegistry::instance();
    registry.load();

    Entry entry;
    entry.productId = "555";
    entry.installPath = "/games/five";
    entry.buildId = "1";
    entry.complete = true;
    QVERIFY(registry.put(entry));

    const QDateTime firstInstall = registry.entry("555").installedAt;
    QVERIFY(firstInstall.isValid());

    Entry updated = entry;
    updated.buildId = "2";
    updated.installedAt = QDateTime();   // an update does not know the first date
    QVERIFY(registry.put(updated));

    // Losing this would make "installed on" mean "last updated on", quietly.
    QCOMPARE(registry.entry("555").installedAt, firstInstall);
    QCOMPARE(registry.entry("555").buildId, QStringLiteral("2"));
    QVERIFY(registry.entry("555").updatedAt.isValid());
}

void TstGogRegistry::removesWhatItIsAskedTo()
{
    GogInstallRegistry& registry = GogInstallRegistry::instance();
    registry.load();

    Entry a;
    a.productId = "aaa";
    a.installPath = "/games/a";
    Entry b;
    b.productId = "bbb";
    b.installPath = "/games/b";
    QVERIFY(registry.put(a));
    QVERIFY(registry.put(b));

    QVERIFY(registry.remove("aaa"));
    QVERIFY(!registry.contains("aaa"));
    QVERIFY(registry.contains("bbb"));

    QVERIFY(!registry.remove("aaa"));   // idempotent, and says so

    // And it survives a reload — the removal was written, not just forgotten.
    registry.load();
    QVERIFY(!registry.contains("aaa"));
    QVERIFY(registry.contains("bbb"));
}

void TstGogRegistry::derivesTheInstallLayout()
{
    // Store-partitioned, so a second store can join without moving anything.
    QCOMPARE(GogInstallRegistry::storeDirectory("/games"), QStringLiteral("/games/GOG"));
    QCOMPARE(GogInstallRegistry::prefixPathFor("1207658930", "/games"),
             QStringLiteral("/games/prefixes/GOG/1207658930"));

    QVERIFY(GogInstallRegistry::defaultInstallRoot().endsWith("/Games/ProtonForge"));
}

void TstGogRegistry::honoursAConfiguredInstallRoot()
{
    QSettings settings;
    settings.setValue("gog/installRoot", "/mnt/games");
    QCOMPARE(GogInstallRegistry::installRoot(), QStringLiteral("/mnt/games"));
    QCOMPARE(GogInstallRegistry::storeDirectory(), QStringLiteral("/mnt/games/GOG"));

    GogInstallRegistry::setInstallRoot(QString());
    QCOMPARE(GogInstallRegistry::installRoot(), GogInstallRegistry::defaultInstallRoot());
}

void TstGogRegistry::refusesToDiscardDangerousPaths_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("root");
    QTest::addColumn<bool>("allowed");

    const QString root = "/home/user/Games/ProtonForge";

    QTest::newRow("a game")        << root + "/GOG/Witcher 3" << root << true;
    QTest::newRow("a prefix")      << root + "/prefixes/GOG/1207" << root << true;
    QTest::newRow("deep")          << root + "/GOG/A/B" << root << true;

    QTest::newRow("the root")      << root << root << false;
    QTest::newRow("the store dir") << root + "/GOG" << root << false;
    QTest::newRow("above root")    << "/home/user/Games" << root << false;
    QTest::newRow("home")          << QDir::homePath() << root << false;
    QTest::newRow("slash")         << "/" << root << false;
    QTest::newRow("elsewhere")     << "/etc" << root << false;
    QTest::newRow("relative")      << "GOG/Witcher 3" << root << false;
    QTest::newRow("empty path")    << QString() << root << false;
    QTest::newRow("empty root")    << root + "/GOG/Witcher 3" << QString() << false;
    // A path that only looks like it is under the root.
    QTest::newRow("prefix trap")   << "/home/user/Games/ProtonForgeOther/x" << root << false;
    // Traversal must be resolved before the comparison, not after.
    QTest::newRow("traversal")     << root + "/GOG/../../../etc" << root << false;
}

void TstGogRegistry::refusesToDiscardDangerousPaths()
{
    QFETCH(QString, path);
    QFETCH(QString, root);
    QFETCH(bool, allowed);

    QCOMPARE(GogDownloader::isSafeToDiscard(path, root), allowed);
}

void TstGogRegistry::discardsADirectoryCarryingOurJournal()
{
    // An install the user put on a second drive is outside the root, and the
    // journal is the only evidence that we made it.
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const QString game = elsewhere.path() + "/Some Game";
    QVERIFY(QDir().mkpath(game));

    QCOMPARE(GogDownloader::isSafeToDiscard(game, "/home/user/Games/ProtonForge"), false);

    QVERIFY(QDir().mkpath(game + "/" + GogDownloader::journalDirName()));
    QCOMPARE(GogDownloader::isSafeToDiscard(game, "/home/user/Games/ProtonForge"), true);
}

QTEST_MAIN(TstGogRegistry)
#include "tst_gogregistry.moc"
