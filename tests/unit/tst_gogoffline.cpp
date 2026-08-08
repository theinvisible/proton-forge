// GOG's offline installers — the account page's `downloads`, not the content
// system.
//
// This is how nearly every Linux build actually ships, so getting it wrong means
// falling back to the Windows version under Proton for games that have a native
// one. The traps:
//
//   `downloads` is a list of two-element arrays — [language, {os: [...]}] — not
//     an object keyed by language. Reading it as a map yields an empty list and
//     looks like "this game has no installers".
//   Sizes are display text ("3.4 GB"). A parser that guesses on an unfamiliar
//     shape turns "unknown" into a number a preflight check then trusts.
//   An entry with an empty manualUrl is not downloadable and must not be
//     offered as though it were.
//   Language selection has to prefer English over whatever came first, or a
//     game installs in a language the user cannot read.

#include <QTest>
#include <QFile>

#include "gog/GogOfflineClient.h"

using Installer = GogOfflineClient::Installer;

class TstGogOffline : public QObject
{
    Q_OBJECT

private slots:
    void readsTheNestedDownloadsStructure();
    void skipsEntriesWithNoDownloadLink();

    void picksTheLinuxInstaller();
    void prefersTheWantedLanguage();
    void fallsBackToEnglish();
    void reportsNothingForAPlatformOnOffer();

    void parsesSizes_data();
    void parsesSizes();

    void buildsTheDownloadUrl_data();
    void buildsTheDownloadUrl();

    void survivesGarbage_data();
    void survivesGarbage();

private:
    static QByteArray fixture(const QString& name)
    {
        QFile file(QStringLiteral(PROTONFORGE_FIXTURES_DIR) + "/gog/" + name);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static QList<Installer> real()
    {
        return GogOfflineClient::parseGameDetails(fixture("game-details.json"));
    }
};

void TstGogOffline::readsTheNestedDownloadsStructure()
{
    const QList<Installer> installers = real();

    // English: windows + linux + mac. Deutsch: windows + linux. Polski's only
    // entry has no link and is dropped.
    QCOMPARE(installers.size(), 5);

    bool sawLinux = false;
    for (const Installer& installer : installers) {
        QVERIFY(!installer.manualUrl.isEmpty());
        sawLinux = sawLinux || installer.os == QLatin1String("linux");
    }
    QVERIFY2(sawLinux, "no Linux installer found — the nested structure was misread");
}

void TstGogOffline::skipsEntriesWithNoDownloadLink()
{
    for (const Installer& installer : real()) {
        QVERIFY2(installer.language != QLatin1String("Polski"),
                 "an installer with no manualUrl was offered as downloadable");
    }
}

void TstGogOffline::picksTheLinuxInstaller()
{
    const Installer chosen = GogOfflineClient::selectInstaller(real(), "linux");

    QCOMPARE(chosen.os, QStringLiteral("linux"));
    QCOMPARE(chosen.language, QStringLiteral("English"));
    QCOMPARE(chosen.id, QStringLiteral("en1installer1"));
    QCOMPARE(chosen.size, qint64(3.4 * 1024 * 1024 * 1024));
}

void TstGogOffline::prefersTheWantedLanguage()
{
    const Installer chosen = GogOfflineClient::selectInstaller(real(), "linux", "Deutsch");

    QCOMPARE(chosen.language, QStringLiteral("Deutsch"));
    QCOMPARE(chosen.id, QStringLiteral("de1installer1"));
}

void TstGogOffline::fallsBackToEnglish()
{
    // Nobody publishes a French build here. English is a better answer than
    // "whichever the JSON happened to list first".
    const Installer chosen = GogOfflineClient::selectInstaller(real(), "linux", "Français");

    QCOMPARE(chosen.language, QStringLiteral("English"));
}

void TstGogOffline::reportsNothingForAPlatformOnOffer()
{
    // Deutsch has no mac build; nothing at all should come back rather than a
    // Windows one silently substituted.
    const Installer chosen = GogOfflineClient::selectInstaller({}, "linux");
    QVERIFY(chosen.manualUrl.isEmpty());

    QList<Installer> windowsOnly;
    for (const Installer& installer : real()) {
        if (installer.os == QLatin1String("windows")) {
            windowsOnly.append(installer);
        }
    }
    QVERIFY(GogOfflineClient::selectInstaller(windowsOnly, "linux").manualUrl.isEmpty());
}

void TstGogOffline::parsesSizes_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<qint64>("expected");

    QTest::newRow("gb")        << "3.4 GB" << qint64(3.4 * 1024 * 1024 * 1024);
    QTest::newRow("mb")        << "512 MB" << qint64(512) * 1024 * 1024;
    QTest::newRow("kb")        << "980 KB" << qint64(980) * 1024;
    QTest::newRow("bytes")     << "42 B" << qint64(42);
    QTest::newRow("comma")     << "3,4 GB" << qint64(3.4 * 1024 * 1024 * 1024);
    QTest::newRow("no space")  << "12GB" << qint64(12) * 1024 * 1024 * 1024;
    QTest::newRow("lowercase") << "7 gb" << qint64(7) * 1024 * 1024 * 1024;

    // Everything below is "we do not know", and must not become a number a
    // free-space check would then trust.
    QTest::newRow("empty")     << "" << qint64(0);
    QTest::newRow("words")     << "quite large" << qint64(0);
    QTest::newRow("no unit")   << "3.4" << qint64(0);
    QTest::newRow("bad unit")  << "3.4 parsecs" << qint64(0);
    QTest::newRow("negative")  << "-2 GB" << qint64(0);
    QTest::newRow("zero")      << "0 GB" << qint64(0);
}

void TstGogOffline::parsesSizes()
{
    QFETCH(QString, text);
    QFETCH(qint64, expected);
    QCOMPARE(GogOfflineClient::parseSize(text), expected);
}

void TstGogOffline::buildsTheDownloadUrl_data()
{
    QTest::addColumn<QString>("manualUrl");
    QTest::addColumn<QString>("expected");

    QTest::newRow("relative")
        << "/downlink/fixture_game/en1installer1"
        << "https://embed.gog.com/downlink/fixture_game/en1installer1";
    QTest::newRow("relative without slash")
        << "downlink/x" << "https://embed.gog.com/downlink/x";
    QTest::newRow("already absolute")
        << "https://cdn.gog.com/x" << "https://cdn.gog.com/x";
    QTest::newRow("empty") << "" << "";
}

void TstGogOffline::buildsTheDownloadUrl()
{
    QFETCH(QString, manualUrl);
    QFETCH(QString, expected);
    QCOMPARE(GogOfflineClient::downloadUrl(manualUrl), expected);
}

void TstGogOffline::survivesGarbage_data()
{
    QTest::addColumn<QByteArray>("json");

    QTest::newRow("empty")     << QByteArray();
    QTest::newRow("not json")  << QByteArray("not json");
    QTest::newRow("array")     << QByteArray("[]");
    QTest::newRow("object")    << QByteArray("{}");
    QTest::newRow("html")      << QByteArray("<html>404</html>");
    QTest::newRow("truncated") << QByteArray("{\"downloads\": [[\"English\", {");
    // The shape it would have if someone read `downloads` as an object.
    QTest::newRow("wrong shape")
        << QByteArray("{\"downloads\": {\"English\": {\"linux\": []}}}");
    QTest::newRow("short pairs")
        << QByteArray("{\"downloads\": [[\"English\"]]}");
}

void TstGogOffline::survivesGarbage()
{
    QFETCH(QByteArray, json);

    const QList<Installer> installers = GogOfflineClient::parseGameDetails(json);
    QVERIFY(installers.isEmpty());
    QVERIFY(GogOfflineClient::selectInstaller(installers, "linux").manualUrl.isEmpty());
}

QTEST_MAIN(TstGogOffline)
#include "tst_gogoffline.moc"
