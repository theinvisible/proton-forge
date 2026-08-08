// SteamLauncher::refreshGameState is what keeps the UPDATE badge honest: it
// re-reads the appmanifest and reports whether anything moved. It had no test
// coverage at all, which is uncomfortable for a function whose only failure
// mode is a badge that silently stops appearing.
//
// The subtle rule it has to follow: needsUpdate is stored on the Game, not
// derived from stateFlags, so it must be re-synced on every pass — including
// the pass where nothing changed and there is nothing to report.
//
// An appmanifest is a text file. No Steam, no account, no network.

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include "launchers/SteamLauncher.h"
#include "core/Game.h"

class TstSteamLauncher : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void traitsDescribeASteamGame();
    void refreshPicksUpAPendingUpdate();
    void refreshClearsTheFlagOnceTheUpdateIsDone();
    void refreshResyncsEvenWhenItReportsNoChange();
    void refreshIgnoresGamesFromAnotherLauncher();
    void refreshIgnoresAMissingManifest();

private:
    QTemporaryDir m_library;

    QString library() const { return m_library.path(); }

    void writeManifest(const QString& appId, int stateFlags, qint64 buildId) const
    {
        QFile acf(library() + "/appmanifest_" + appId + ".acf");
        QVERIFY(acf.open(QIODevice::WriteOnly | QIODevice::Text));
        acf.write(QString(
            "\"AppState\"\n{\n"
            "\t\"appid\"\t\t\"%1\"\n"
            "\t\"name\"\t\t\"ELDEN RING\"\n"
            "\t\"installdir\"\t\t\"ELDEN RING\"\n"
            "\t\"StateFlags\"\t\t\"%2\"\n"
            "\t\"buildid\"\t\t\"%3\"\n"
            "}\n").arg(appId).arg(stateFlags).arg(buildId).toUtf8());
    }

    // A game as SteamLauncher would have discovered it.
    Game installedGame(int stateFlags = 4, qint64 buildId = 100) const
    {
        Game game("1245620", "ELDEN RING", "Steam");
        game.setLibraryPath(library());
        game.setStateFlags(stateFlags);
        game.setBuildId(buildId);
        game.setNeedsUpdate((stateFlags & 2) != 0);
        return game;
    }
};

void TstSteamLauncher::init()
{
    QVERIFY(m_library.isValid());
    QDir dir(library());
    for (const QString& entry : dir.entryList(QDir::Files)) {
        QFile::remove(dir.filePath(entry));
    }
}

void TstSteamLauncher::traitsDescribeASteamGame()
{
    // Steam is the launcher every trait was extracted from, so it answers yes
    // to all of them. A regression here silently strips Steam games of their
    // environment, their overlay or their ProtonDB lookup.
    const LauncherTraits traits = SteamLauncher().traits();

    QVERIFY(traits.usesSteamEnv);
    QVERIFY(traits.requiresClientRunning);
    QVERIFY(traits.supportsLaunchOptionsIO);
    QVERIFY(traits.providesUpdateState);
    QVERIFY(traits.idIsSteamAppId);
}

void TstSteamLauncher::refreshPicksUpAPendingUpdate()
{
    writeManifest("1245620", 6, 101);   // 6 = installed + update required

    const SteamLauncher launcher;
    Game game = installedGame(4, 100);

    QVERIFY(launcher.refreshGameState(game));
    QVERIFY(game.needsUpdate());
    QCOMPARE(game.stateFlags(), 6);
    QCOMPARE(game.buildId(), 101LL);
}

void TstSteamLauncher::refreshClearsTheFlagOnceTheUpdateIsDone()
{
    writeManifest("1245620", 4, 101);

    const SteamLauncher launcher;
    Game game = installedGame(6, 100);
    QVERIFY(game.needsUpdate());

    QVERIFY(launcher.refreshGameState(game));
    QVERIFY2(!game.needsUpdate(), "the badge has to go away again");
}

void TstSteamLauncher::refreshResyncsEvenWhenItReportsNoChange()
{
    // A game whose stored needsUpdate has drifted out of step with its flags —
    // which is what a caller copying stateFlags across without needsUpdate
    // produces. The manifest agrees with the flags, so nothing "changed", and
    // an implementation that only writes on the changed path leaves the stale
    // value in place forever.
    writeManifest("1245620", 6, 100);

    const SteamLauncher launcher;
    Game game = installedGame(6, 100);
    game.setNeedsUpdate(false);

    QVERIFY2(!launcher.refreshGameState(game), "nothing moved, so nothing to report");
    QVERIFY2(game.needsUpdate(), "but the stale flag still had to be corrected");
}

void TstSteamLauncher::refreshIgnoresGamesFromAnotherLauncher()
{
    writeManifest("1245620", 6, 101);

    const SteamLauncher launcher;
    Game game("1245620", "Something else entirely", "GOG");
    game.setLibraryPath(library());

    QVERIFY(!launcher.refreshGameState(game));
    QVERIFY2(!game.needsUpdate(),
             "a colliding product id must not pick up another store's install state");
}

void TstSteamLauncher::refreshIgnoresAMissingManifest()
{
    const SteamLauncher launcher;
    Game game = installedGame(4, 100);

    QVERIFY(!launcher.refreshGameState(game));
    QCOMPARE(game.stateFlags(), 4);
}

QTEST_MAIN(TstSteamLauncher)
#include "tst_steamlauncher.moc"
