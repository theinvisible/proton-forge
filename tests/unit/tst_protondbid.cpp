// ProtonDB serves its per-game reports under an obfuscated id derived in the
// site's own JavaScript. ProtonDBClient reimplements that derivation in C++,
// including a deliberate 32-bit wraparound that mirrors JS's `|0`. Nothing
// about it is guessable from the C++ alone, and if it drifts the app silently
// falls back to "no reports available" — a failure mode nobody notices.
//
// The expected values below were produced by an independent reimplementation
// of the same JavaScript in Python, not by running this code, so the test is a
// real cross-check rather than a snapshot of whatever the C++ happens to do.

#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "network/ProtonDBClient.h"

class TstProtonDbId : public QObject
{
    Q_OBJECT

private slots:
    void computeGameId_data();
    void computeGameId();
    void isDeterministic();
    void saltChangesTheId();
    void survivesInt32Wraparound();
    void parseSummary_data();
    void parseSummary();
    void parseSummaryRejectsGarbage();
    void parseReports();
    void parseReportsRejectsGarbage();
    void appUrl();
};

void TstProtonDbId::computeGameId_data()
{
    QTest::addColumn<qint64>("appId");
    QTest::addColumn<qint64>("reports");
    QTest::addColumn<qint64>("timestamp");
    QTest::addColumn<qint64>("expected");

    QTest::newRow("elden ring shape")  << 1245620LL << 300000LL << 1770000000LL << 1362624955LL;
    QTest::newRow("minimal salts")     << 1245620LL << 1LL      << 1LL          << 666725696LL;
    QTest::newRow("dota")              << 570LL     << 123456LL << 1700000000LL << 715413572LL;
    // n == 0 takes the guarded division-by-zero branch.
    QTest::newRow("all zero")          << 0LL       << 0LL      << 0LL          << 1682539903LL;
    QTest::newRow("beyond 32 bits")    << 4294967296LL << 7LL   << 13LL         << 1100745404LL;
}

void TstProtonDbId::computeGameId()
{
    QFETCH(qint64, appId);
    QFETCH(qint64, reports);
    QFETCH(qint64, timestamp);
    QFETCH(qint64, expected);

    QCOMPARE(ProtonDBClient::computeGameId(appId, reports, timestamp), expected);
}

void TstProtonDbId::isDeterministic()
{
    const qint64 first  = ProtonDBClient::computeGameId(1245620, 300000, 1770000000);
    const qint64 second = ProtonDBClient::computeGameId(1245620, 300000, 1770000000);
    QCOMPARE(first, second);
}

void TstProtonDbId::saltChangesTheId()
{
    // The report count rotates the id on every ProtonDB build — that rotation
    // is the whole reason counts.json is fetched at runtime instead of the id
    // being cached.
    const qint64 a = ProtonDBClient::computeGameId(1245620, 300000, 1770000000);
    const qint64 b = ProtonDBClient::computeGameId(1245620, 300001, 1770000000);
    QVERIFY(a != b);

    // The timestamp, on the other hand, only ever enters as a modulus, so for
    // real inputs — where both the app id and the report count are far smaller
    // than a unix timestamp — changing it does nothing at all. Surprising, but
    // it is what the site's own code does, and the id has to match the site's.
    QCOMPARE(ProtonDBClient::computeGameId(1245620, 300000, 1770000001), a);

    // It does matter once the modulus is small enough to bite.
    QVERIFY(ProtonDBClient::computeGameId(1245620, 300000, 7)
            != ProtonDBClient::computeGameId(1245620, 300000, 11));
}

void TstProtonDbId::survivesInt32Wraparound()
{
    // The accumulator overflows constantly by design. The result must always be
    // a non-negative value that fits the range abs(int32) can produce — in
    // particular INT32_MIN must not come back negative.
    for (qint64 appId = 1; appId < 4000; appId += 137) {
        const qint64 id = ProtonDBClient::computeGameId(appId, appId * 3 + 1, 1700000000 + appId);
        QVERIFY2(id >= 0, qPrintable(QString("negative id for appId %1: %2").arg(appId).arg(id)));
        QVERIFY2(id <= 2147483648LL, qPrintable(QString("out of range: %1").arg(id)));
    }
}

void TstProtonDbId::parseSummary_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("tier");
    QTest::addColumn<int>("total");

    QTest::newRow("platinum")
        << QByteArray(R"({"bestReportedTier":"platinum","confidence":"strong",
                          "score":0.93,"tier":"platinum","total":1234,"trendingTier":"platinum"})")
        << QString("platinum") << 1234;
    QTest::newRow("pending")
        << QByteArray(R"({"confidence":"inadequate","score":0,"tier":"pending","total":1})")
        << QString("pending") << 1;
}

void TstProtonDbId::parseSummary()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, tier);
    QFETCH(int, total);

    const ProtonDBClient::Summary summary = ProtonDBClient::parseSummary(json);
    QVERIFY2(summary.valid, "a well-formed summary was rejected");
    QCOMPARE(summary.tier, tier);
    QCOMPARE(summary.total, total);
}

void TstProtonDbId::parseSummaryRejectsGarbage()
{
    // Third-party endpoint: it will return something unexpected eventually, and
    // the only acceptable outcome is valid == false.
    for (const QByteArray& bad : {QByteArray(""), QByteArray("not json"),
                                  QByteArray("[]"), QByteArray("{}"),
                                  QByteArray("<html>404</html>")}) {
        const ProtonDBClient::Summary summary = ProtonDBClient::parseSummary(bad);
        QVERIFY2(!summary.valid, qPrintable("accepted garbage: " + QString::fromUtf8(bad)));
    }
}

void TstProtonDbId::parseReports()
{
    // ProtonDB's actual report shape: a "reports" array, the interesting fields
    // nested under "responses", and the driver buried under device.inferred.steam.
    const QByteArray json = R"({"reports":[
        {"timestamp":1700000000,
         "responses":{"launchOptions":"  PROTON_ENABLE_NVAPI=1 %command%  ",
                      "concludingNotes":"Runs great with DLSS forced on.",
                      "protonVersion":"GE-Proton9-20"},
         "device":{"inferred":{"steam":{"gpuDriver":"NVIDIA 570.86"}}}},
        {"timestamp":1700000001,
         "responses":{"concludingNotes":"Needed gamemoderun.",
                      "protonVersion":"Proton Experimental",
                      "customProtonVersion":"GE-Proton9-25"}},
        {"timestamp":1700000002,
         "responses":{"protonVersion":"Proton 9.0-4"}}
    ]})";

    const QList<ProtonDBClient::Report> reports = ProtonDBClient::parseReports(json);

    // The third report carries neither launch options nor notes — nothing to
    // mine, so it is dropped rather than shown as an empty recommendation.
    QCOMPARE(reports.size(), 2);

    QCOMPARE(reports.at(0).launchOptions, QString("PROTON_ENABLE_NVAPI=1 %command%"));  // trimmed
    QCOMPARE(reports.at(0).notes, QString("Runs great with DLSS forced on."));
    QCOMPARE(reports.at(0).protonVersion, QString("GE-Proton9-20"));
    QCOMPARE(reports.at(0).gpuDriver, QString("NVIDIA 570.86"));
    QCOMPARE(reports.at(0).timestamp, 1700000000LL);

    // A missing launchOptions field must come back empty, not crash.
    QVERIFY(reports.at(1).launchOptions.isEmpty());
    QCOMPARE(reports.at(1).notes, QString("Needed gamemoderun."));
    // A hand-entered version wins over the picked one.
    QCOMPARE(reports.at(1).protonVersion, QString("GE-Proton9-25"));
    QVERIFY(reports.at(1).gpuDriver.isEmpty());

    // Note: Report::tier is declared but parseReports never fills it, and
    // nothing reads it. Not asserted here because there is nothing to assert.
}

void TstProtonDbId::parseReportsRejectsGarbage()
{
    for (const QByteArray& bad : {QByteArray(""), QByteArray("not json"),
                                  QByteArray("{}"), QByteArray("<html>404</html>")}) {
        QVERIFY2(ProtonDBClient::parseReports(bad).isEmpty(),
                 qPrintable("accepted garbage: " + QString::fromUtf8(bad)));
    }
}

void TstProtonDbId::appUrl()
{
    const QString url = ProtonDBClient::appUrl("1245620");
    QVERIFY2(url.contains("1245620"), qPrintable(url));
    QVERIFY2(url.startsWith("https://"), qPrintable(url));
}

QTEST_MAIN(TstProtonDbId)
#include "tst_protondbid.moc"
