// SteamLauncher::refreshGameState is what keeps the UPDATE badge honest: it
// re-reads the appmanifest and reports whether anything moved. It had no test
// coverage at all, which is uncomfortable for a function whose only failure
// mode is a badge that silently stops appearing.
//
// The subtle rule it has to follow: needsUpdate is stored on the Game, not
// derived from stateFlags, so it must be re-synced on every pass — including
// the pass where nothing changed and there is nothing to report.
//
// The second subject here is scanLibraries(), which is what turned issue #1 from
// "no games" into a sentence naming the path: libraryfolders.vdf names library
// folders, some of them on drives this process cannot see, and the unreadable
// ones used to be dropped without a trace.
//
// An appmanifest is a text file. No Steam, no account, no network.

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "launchers/SteamLauncher.h"
#include "utils/SteamPaths.h"
#include "core/Game.h"

class TstSteamLauncher : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void traitsDescribeASteamGame();
    void scanKeepsTheLibrariesItCannotRead();
    void scanReportsNothingWhenEveryLibraryIsThere();
    void warningNamesThePathAndSaysWhatIsMissing();
    void warningOffersTheFlatpakOverrideOnlyInsideAFlatpak();
    void discoveryWarningsFollowTheLastDiscovery();
    void refreshPicksUpAPendingUpdate();
    void refreshClearsTheFlagOnceTheUpdateIsDone();
    void refreshResyncsEvenWhenItReportsNoChange();
    void refreshIgnoresGamesFromAnotherLauncher();
    void refreshIgnoresAMissingManifest();

private:
    QTemporaryDir m_library;
    QTemporaryDir m_home;
    QByteArray m_realHome;
    QByteArray m_realFlatpakId;

    QString library() const { return m_library.path(); }
    QString home() const { return m_home.path(); }

    // A Steam root as SteamPaths recognises one, whose libraryfolders.vdf names
    // itself plus every path handed in — existing or not, which is the point.
    QString makeSteamRoot(const QStringList& extraLibraries) const
    {
        const QString root = home() + "/.local/share/Steam";
        QDir().mkpath(root + "/steamapps");

        QString vdf = "\"libraryfolders\"\n{\n";
        vdf += QString("\t\"0\"\n\t{\n\t\t\"path\"\t\t\"%1\"\n\t}\n").arg(root);
        int index = 1;
        for (const QString& extra : extraLibraries) {
            vdf += QString("\t\"%1\"\n\t{\n\t\t\"path\"\t\t\"%2\"\n\t}\n")
                       .arg(index++).arg(extra);
        }
        vdf += "}\n";

        QFile file(root + "/steamapps/libraryfolders.vdf");
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(vdf.toUtf8());
        }
        SteamPaths::invalidateCache();
        return root;
    }

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

    // scanLibraries() reads through SteamPaths, which derives everything from
    // $HOME — so the fixture is a home of our own rather than the developer's.
    QVERIFY(m_home.isValid());
    m_realHome = qgetenv("HOME");
    qputenv("HOME", m_home.path().toUtf8());

    // libraryWarnings() phrases itself differently inside a Flatpak, so pin it
    // instead of inheriting whatever is running the tests.
    m_realFlatpakId = qgetenv("FLATPAK_ID");
    qputenv("FLATPAK_ID", QByteArray());

    SteamPaths::invalidateCache();
}

void TstSteamLauncher::cleanup()
{
    qputenv("HOME", m_realHome);
    qputenv("FLATPAK_ID", m_realFlatpakId);
    SteamPaths::invalidateCache();

    QDir dir(m_home.path());
    for (const QString& entry :
         dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QFileInfo info(dir.filePath(entry));
        if (info.isDir() && !info.isSymLink()) {
            QDir(info.absoluteFilePath()).removeRecursively();
        } else {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

void TstSteamLauncher::scanKeepsTheLibrariesItCannotRead()
{
    // One library that is there, one that is not — the shape of a Flatpak with
    // no grant for the second drive, and of an unmounted disk.
    const QString second = home() + "/second-drive/SteamLibrary";
    QVERIFY(QDir().mkpath(second + "/steamapps"));
    const QString root = makeSteamRoot({second, "/mnt/games/SteamLibrary"});

    const SteamLauncher::LibraryScan scan = SteamLauncher::scanLibraries();

    QVERIFY(scan.paths.contains(root + "/steamapps"));
    QVERIFY(scan.paths.contains(second + "/steamapps"));
    QCOMPARE(scan.unreadable, QStringList{"/mnt/games/SteamLibrary"});

    // libraryPaths() is the same scan without the bad news, so the callers that
    // only ever wanted directories to walk keep working unchanged.
    QCOMPARE(SteamLauncher::libraryPaths(), scan.paths);
}

void TstSteamLauncher::scanReportsNothingWhenEveryLibraryIsThere()
{
    makeSteamRoot({});
    QVERIFY2(SteamLauncher::scanLibraries().unreadable.isEmpty(),
             "a healthy install must not produce a warning");
}

void TstSteamLauncher::warningNamesThePathAndSaysWhatIsMissing()
{
    QVERIFY(SteamLauncher::libraryWarnings({}, QString()).isEmpty());

    const QStringList warnings =
        SteamLauncher::libraryWarnings({"/mnt/games/SteamLibrary"}, QString());
    QCOMPARE(warnings.count(), 1);

    // The path is the whole point: without it the message is no more useful
    // than the empty list it replaced.
    QVERIFY(warnings.first().contains("/mnt/games/SteamLibrary"));
    QVERIFY2(!warnings.first().contains("flatpak override"),
             "a native install cannot fix anything with a flatpak override");
}

void TstSteamLauncher::warningOffersTheFlatpakOverrideOnlyInsideAFlatpak()
{
    const QStringList warnings = SteamLauncher::libraryWarnings(
        {"/mnt/games/SteamLibrary"}, "org.protonforge.ProtonForge");
    QCOMPARE(warnings.count(), 1);

    // Verbatim and scoped to the one path — a command the user can paste.
    QVERIFY(warnings.first().contains(
        "flatpak override --user --filesystem=\"/mnt/games/SteamLibrary\" "
        "org.protonforge.ProtonForge"));
}

void TstSteamLauncher::discoveryWarningsFollowTheLastDiscovery()
{
    SteamLauncher launcher;
    QVERIFY2(launcher.discoveryWarnings().isEmpty(),
             "nothing has been discovered yet, so there is nothing to report");

    makeSteamRoot({"/mnt/games/SteamLibrary"});
    launcher.discoverGames();
    QCOMPARE(launcher.discoveryWarnings().count(), 1);
    QVERIFY(launcher.discoveryWarnings().first().contains("/mnt/games/SteamLibrary"));

    // And it clears: a Refresh after the drive comes back must take the bar
    // down again rather than leave the last complaint on screen forever.
    makeSteamRoot({});
    launcher.discoverGames();
    QVERIFY(launcher.discoveryWarnings().isEmpty());
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
