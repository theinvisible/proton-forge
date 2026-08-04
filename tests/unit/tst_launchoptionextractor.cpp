// The extractor mines free-form ProtonDB comments for launch options and shows
// what it finds as recommendations the user can apply with one click. The risk
// is not that it misses something — it is that it turns a sentence into a
// suggestion. These tests are mostly about what must NOT come out of it.

#include <QTest>

#include "utils/LaunchOptionExtractor.h"
#include "network/ProtonDBClient.h"

class TstLaunchOptionExtractor : public QObject
{
    Q_OBJECT

private slots:
    void emptyInput();
    void explicitFieldWins();
    void extractsCommandLines();
    void extractsKnownEnvVars();
    void ignoresProse();
    void detectsWrappers();
    void deduplicatesAndCounts();
    void respectsMaxResults();
    void keepsSourceReports();

private:
    static ProtonDBClient::Report report(const QString& notes,
                                         const QString& launchOptions = QString())
    {
        ProtonDBClient::Report r;
        r.notes = notes;
        r.launchOptions = launchOptions;
        r.protonVersion = "GE-Proton9-20";
        r.gpuDriver = "NVIDIA 570.86";
        r.tier = "platinum";
        r.timestamp = 1700000000;
        return r;
    }

    static bool hasSnippetContaining(const QList<LaunchOptionExtractor::Suggestion>& all,
                                     const QString& needle)
    {
        for (const auto& s : all) {
            if (s.snippet.contains(needle)) {
                return true;
            }
        }
        return false;
    }
};

void TstLaunchOptionExtractor::emptyInput()
{
    QVERIFY(LaunchOptionExtractor::extract({}).isEmpty());
    QVERIFY(LaunchOptionExtractor::extract({report("")}).isEmpty());
}

void TstLaunchOptionExtractor::explicitFieldWins()
{
    const auto results = LaunchOptionExtractor::extract(
        {report("Some prose.", "PROTON_ENABLE_NVAPI=1 %command%")});

    QVERIFY(!results.isEmpty());
    QVERIFY2(results.first().direct, "the explicit launchOptions field should rank first");
    QVERIFY(results.first().snippet.contains("PROTON_ENABLE_NVAPI=1"));
    QVERIFY(results.first().hasCommand);
}

void TstLaunchOptionExtractor::extractsCommandLines()
{
    const auto results = LaunchOptionExtractor::extract({
        report("I had to use this:\nDXVK_ASYNC=1 %command% -novid\nand then it worked."),
    });

    QVERIFY2(hasSnippetContaining(results, "%command%"),
             "a whole launch line in the notes was not picked up");
    QVERIFY(hasSnippetContaining(results, "DXVK_ASYNC=1"));
}

void TstLaunchOptionExtractor::extractsKnownEnvVars()
{
    const auto results = LaunchOptionExtractor::extract({
        report("Set PROTON_USE_NTSYNC=1 and it stopped stuttering."),
        report("VKD3D_CONFIG=dxr fixed the ray tracing."),
        report("DXVK_FRAME_RATE=60 to cap it."),
    });

    QVERIFY(hasSnippetContaining(results, "PROTON_USE_NTSYNC=1"));
    QVERIFY(hasSnippetContaining(results, "VKD3D_CONFIG=dxr"));
    QVERIFY(hasSnippetContaining(results, "DXVK_FRAME_RATE=60"));
}

void TstLaunchOptionExtractor::ignoresProse()
{
    // The allowlist and the prose heuristic exist so ordinary sentences with an
    // equals sign, or unrelated shouty words, never become a suggestion.
    const auto results = LaunchOptionExtractor::extract({
        report("Performance = great after the latest update."),
        report("My GPU is an RTX 4080 and the CPU is a 7800X3D."),
        report("It just works, no launch options needed at all."),
        report("FPS was 60 = smooth."),
    });

    for (const auto& s : results) {
        QVERIFY2(!s.snippet.contains("great"), qPrintable("prose leaked in: " + s.snippet));
        QVERIFY2(!s.snippet.contains("smooth"), qPrintable("prose leaked in: " + s.snippet));
        QVERIFY2(!s.snippet.contains("RTX"), qPrintable("prose leaked in: " + s.snippet));
    }
}

void TstLaunchOptionExtractor::detectsWrappers()
{
    const auto results = LaunchOptionExtractor::extract({
        report("Run it with gamemoderun for better frame pacing."),
        report("I use mangohud to check the fps."),
        report("gamescope -W 2560 -H 1440 works well."),
    });

    QVERIFY(hasSnippetContaining(results, "gamemoderun"));
    QVERIFY(hasSnippetContaining(results, "mangohud"));
    QVERIFY(hasSnippetContaining(results, "gamescope"));
}

void TstLaunchOptionExtractor::deduplicatesAndCounts()
{
    // The same advice repeated by many people is the strongest signal there is,
    // so it must aggregate rather than appear five times.
    QList<ProtonDBClient::Report> reports;
    for (int i = 0; i < 5; ++i) {
        reports << report("Use PROTON_USE_NTSYNC=1.");
    }
    reports << report("Try DXVK_ASYNC=1 maybe.");

    const auto results = LaunchOptionExtractor::extract(reports);

    int ntsyncEntries = 0;
    int ntsyncOccurrences = 0;
    for (const auto& s : results) {
        if (s.snippet.contains("PROTON_USE_NTSYNC=1")) {
            ++ntsyncEntries;
            ntsyncOccurrences = s.occurrences;
        }
    }
    QCOMPARE(ntsyncEntries, 1);
    QVERIFY2(ntsyncOccurrences >= 5,
             qPrintable(QString("occurrences was %1, expected >= 5").arg(ntsyncOccurrences)));

    // And the popular one outranks the one-off.
    QVERIFY(!results.isEmpty());
    QVERIFY2(results.first().snippet.contains("PROTON_USE_NTSYNC=1"),
             qPrintable("ranking put '" + results.first().snippet + "' first"));
}

void TstLaunchOptionExtractor::respectsMaxResults()
{
    QList<ProtonDBClient::Report> reports;
    const QStringList vars = {
        "PROTON_USE_NTSYNC=1", "PROTON_ENABLE_NVAPI=1", "PROTON_LOG=1",
        "DXVK_ASYNC=1", "DXVK_FRAME_RATE=60", "DXVK_HUD=fps",
        "VKD3D_CONFIG=dxr", "VKD3D_FRAME_RATE=60", "PROTON_USE_D7VK=1",
        "PROTON_ENABLE_HDR=1", "PROTON_NO_ESYNC=1", "PROTON_NO_FSYNC=1",
    };
    for (const QString& v : vars) {
        reports << report("Set " + v + " for this one.");
    }

    QVERIFY(LaunchOptionExtractor::extract(reports, 3).size() <= 3);
    QVERIFY(LaunchOptionExtractor::extract(reports, 8).size() <= 8);
}

void TstLaunchOptionExtractor::keepsSourceReports()
{
    // The dialog shows the comment a suggestion came from; without the source
    // the user gets a bare env var and no reason to trust it.
    const auto results = LaunchOptionExtractor::extract(
        {report("Set PROTON_USE_NTSYNC=1 and it stopped stuttering.")});

    QVERIFY(!results.isEmpty());
    QVERIFY2(!results.first().sources.isEmpty(), "a suggestion with no source report");
    QVERIFY(results.first().sources.first().notes.contains("stuttering"));
}

QTEST_MAIN(TstLaunchOptionExtractor)
#include "tst_launchoptionextractor.moc"
