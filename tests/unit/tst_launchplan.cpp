// GameRunner resolves a launch entirely from what is on disk: which Proton to
// use, where the game's executable is, which prefix to hand Proton. All of that
// derives from $HOME, so a fake $HOME plus a stub Proton tree drives it with no
// Steam, no network and no display.
//
// Today this pins Proton discovery. It used to require a Steam installation
// even when the Proton in question had been installed by ProtonForge itself,
// which left "auto" resolving to nothing on a machine with no Steam.

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "runner/GameRunner.h"
#include "launchers/SteamLauncher.h"
#include "core/Game.h"
#include "core/DLSSSettings.h"
#include "utils/SteamPaths.h"

class TstLaunchPlan : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void findsProtonInstalledWithoutSteam();
    void findsProtonInSteamCompatToolsDir();
    void findsNothingWhenNoProtonIsInstalled();

    void aNonSteamGameGetsNoSteamEnvironment();
    void aNonSteamGameNeverGetsTheOverlayEvenWithSteamInstalled();
    void launcherArgumentsComeBeforeTheUsersOwn();
    void aGameWithNowhereToPutAPrefixIsRefused();
    void aSteamGameStillGetsTheFullSteamEnvironment();

private:
    QTemporaryDir m_home;
    QByteArray m_realHome;
    QByteArray m_realFlatpakId;

    QString home() const { return m_home.path(); }
    QString nativeRoot() const { return home() + "/.local/share/Steam"; }

    void makeFile(const QString& path, const QByteArray& contents = "stub") const
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    // The overlay libraries Steam ships. GameRunner only preloads them if they
    // are really there, so a test about the overlay has to create them.
    void makeOverlay(const QString& root) const
    {
        makeFile(root + "/ubuntu12_64/gameoverlayrenderer.so");
        makeFile(root + "/ubuntu12_32/gameoverlayrenderer.so");
    }

    // A game as a store launcher that is not Steam would hand it over: it knows
    // its own install and prefix locations, and claims none of Steam's traits.
    Game gogGame() const
    {
        const QString install = home() + "/Games/ProtonForge/GOG/Witcher 3";
        makeFile(install + "/bin/x64/witcher3.exe");

        Game game("1207664663", "The Witcher 3", "GOG");
        game.setInstallPath(install);
        game.setExecutablePath(install + "/bin/x64/witcher3.exe");
        game.setCompatDataPath(home() + "/Games/ProtonForge/prefixes/GOG/1207664663");
        return game;
    }

    // A Proton is any directory holding a file called "proton".
    void makeProton(const QString& dir) const
    {
        QVERIFY(QDir().mkpath(dir));
        QFile proton(dir + "/proton");
        QVERIFY(proton.open(QIODevice::WriteOnly));
        proton.write("#!/bin/sh\n");
    }

    // The minimum that makes SteamPaths believe a directory is a Steam root.
    void makeSteamRoot(const QString& root) const
    {
        QVERIFY(QDir().mkpath(root + "/steamapps"));
        QFile vdf(root + "/steamapps/libraryfolders.vdf");
        QVERIFY(vdf.open(QIODevice::WriteOnly | QIODevice::Text));
        vdf.write(QString(R"("libraryfolders" { "0" { "path" "%1" } })").arg(root).toUtf8());
    }
};

void TstLaunchPlan::init()
{
    QVERIFY(m_home.isValid());
    m_realHome = qgetenv("HOME");
    qputenv("HOME", m_home.path().toUtf8());

    // defaultInstallCompatPath() branches on this, so pin it rather than
    // inheriting whatever the machine running the tests happens to have.
    m_realFlatpakId = qgetenv("FLATPAK_ID");
    qputenv("FLATPAK_ID", QByteArray());

    // EnvBuilder::buildEnvironment starts from the process environment, so an
    // LD_PRELOAD inherited from whatever launched the test would look exactly
    // like an overlay ProtonForge injected.
    qunsetenv("LD_PRELOAD");

    SteamPaths::invalidateCache();
}

void TstLaunchPlan::cleanup()
{
    qputenv("HOME", m_realHome);
    qputenv("FLATPAK_ID", m_realFlatpakId);
    SteamPaths::invalidateCache();

    QDir dir(m_home.path());
    for (const QString& entry : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        QFileInfo info(dir.filePath(entry));
        if (info.isDir() && !info.isSymLink()) {
            QDir(info.absoluteFilePath()).removeRecursively();
        } else {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

void TstLaunchPlan::findsProtonInstalledWithoutSteam()
{
    // No Steam anywhere, so this is the directory ProtonForge's own
    // Proton-Manager would have extracted into.
    const QString managed = home() + "/.steam/root/compatibilitytools.d";
    const QString proton  = managed + "/proton-cachyos-10.0-20250101";
    makeProton(proton);

    QVERIFY2(SteamPaths::steamRoot().isEmpty(), "fixture accidentally looks like a Steam install");

    GameRunner runner;
    QCOMPARE(runner.findProtonPath(Game(), DLSSSettings()), proton);
}

void TstLaunchPlan::findsProtonInSteamCompatToolsDir()
{
    // With Steam present the managed directory and Steam's compatibilitytools.d
    // are the same path. It must be searched, and searched once.
    makeSteamRoot(nativeRoot());
    const QString proton = nativeRoot() + "/compatibilitytools.d/GE-Proton10-1";
    makeProton(proton);

    QCOMPARE(SteamPaths::steamRoot(), nativeRoot());

    GameRunner runner;
    QCOMPARE(runner.findProtonPath(Game(), DLSSSettings()), proton);
}

void TstLaunchPlan::findsNothingWhenNoProtonIsInstalled()
{
    GameRunner runner;
    QVERIFY(runner.findProtonPath(Game(), DLSSSettings()).isEmpty());
}

void TstLaunchPlan::aNonSteamGameGetsNoSteamEnvironment()
{
    makeProton(home() + "/.steam/root/compatibilitytools.d/proton-cachyos-10.0");

    GameRunner runner;
    const GameRunner::LaunchPlan plan = runner.resolveLaunch(gogGame(), DLSSSettings());

    QVERIFY2(plan.valid, qPrintable(plan.error));

    // A DRM-free game from another store has no appid, and claiming one makes
    // Proton set up Steamworks for a game that will never call it.
    QVERIFY(!plan.env.contains("SteamAppId"));
    QVERIFY(!plan.env.contains("SteamGameId"));
    QVERIFY(!plan.env.contains("STEAM_RUNTIME"));

    // No Steam here at all, so this key is omitted rather than set to "".
    QVERIFY(!plan.env.contains("STEAM_COMPAT_CLIENT_INSTALL_PATH"));

    // What Proton does still need.
    QCOMPARE(plan.env.value("STEAM_COMPAT_DATA_PATH"),
             home() + "/Games/ProtonForge/prefixes/GOG/1207664663");
    QCOMPARE(plan.compatDataPath, home() + "/Games/ProtonForge/prefixes/GOG/1207664663");
}

void TstLaunchPlan::aNonSteamGameNeverGetsTheOverlayEvenWithSteamInstalled()
{
    // Deliberately with Steam present and its overlay libraries on disk. Proving
    // this on a machine with no Steam would prove nothing — there would be
    // nothing to preload either way.
    makeSteamRoot(nativeRoot());
    makeOverlay(nativeRoot());
    makeProton(nativeRoot() + "/compatibilitytools.d/proton-cachyos-10.0");
    QCOMPARE(SteamPaths::steamRoot(), nativeRoot());

    DLSSSettings settings;
    settings.enableSteamOverlay = true;   // the shipped default

    GameRunner runner;
    const GameRunner::LaunchPlan plan = runner.resolveLaunch(gogGame(), settings);

    QVERIFY2(plan.valid, qPrintable(plan.error));
    QVERIFY2(!plan.env.contains("LD_PRELOAD"),
             "the Steam overlay must not be injected into a non-Steam game");
    QVERIFY(!plan.env.contains("SteamAppId"));

    // But the client path is set now, because there genuinely is one to name.
    QCOMPARE(plan.env.value("STEAM_COMPAT_CLIENT_INSTALL_PATH"), nativeRoot());
}

void TstLaunchPlan::launcherArgumentsComeBeforeTheUsersOwn()
{
    makeProton(home() + "/.steam/root/compatibilitytools.d/proton-cachyos-10.0");

    Game game = gogGame();
    game.setLaunchArgs({"--launcher-skip"});
    game.setWorkingDirectory(game.installPath());

    DLSSSettings settings;
    settings.customLaunchParams = "%command% -myflag";

    GameRunner runner;
    const GameRunner::LaunchPlan plan = runner.resolveLaunch(game, settings);

    QVERIFY2(plan.valid, qPrintable(plan.error));
    QCOMPARE(plan.args, QStringList({"run",
                                     game.executablePath(),
                                     "--launcher-skip",
                                     "-myflag"}));

    // The launcher named a working directory that is not the executable's own.
    QCOMPARE(plan.workingDirectory, game.installPath());
}

void TstLaunchPlan::aGameWithNowhereToPutAPrefixIsRefused()
{
    makeProton(home() + "/.steam/root/compatibilitytools.d/proton-cachyos-10.0");

    Game game = gogGame();
    game.setCompatDataPath(QString());   // and no libraryPath to derive one from

    GameRunner runner;
    const GameRunner::LaunchPlan plan = runner.resolveLaunch(game, DLSSSettings());

    QVERIFY2(!plan.valid, "a launch with nowhere to put its prefix has to be refused");
    QVERIFY2(plan.error.contains("prefix"), qPrintable("unhelpful error: " + plan.error));
}

void TstLaunchPlan::aSteamGameStillGetsTheFullSteamEnvironment()
{
    // The regression guard for the whole phase: nothing above may have taken
    // anything away from a Steam game.
    makeSteamRoot(nativeRoot());
    makeOverlay(nativeRoot());
    makeProton(nativeRoot() + "/compatibilitytools.d/proton-cachyos-10.0");

    const QString library = nativeRoot() + "/steamapps";
    const QString install = library + "/common/ELDEN RING";
    makeFile(install + "/ELDEN RING.exe");

    Game game("1245620", "ELDEN RING", "Steam");
    game.setTraits(SteamLauncher().traits());
    game.setInstallPath(install);
    game.setExecutablePath(install + "/ELDEN RING.exe");
    game.setLibraryPath(library);

    DLSSSettings settings;
    settings.enableSteamOverlay = true;

    GameRunner runner;
    const GameRunner::LaunchPlan plan = runner.resolveLaunch(game, settings);

    QVERIFY2(plan.valid, qPrintable(plan.error));
    QCOMPARE(plan.env.value("SteamAppId"), QStringLiteral("1245620"));
    QCOMPARE(plan.env.value("SteamGameId"), QStringLiteral("1245620"));
    QCOMPARE(plan.env.value("STEAM_COMPAT_CLIENT_INSTALL_PATH"), nativeRoot());
    QCOMPARE(plan.env.value("STEAM_RUNTIME"), nativeRoot() + "/ubuntu12_32/steam-runtime");

    // Derived from the library, byte-identical to what GameRunner used to build.
    QCOMPARE(plan.compatDataPath, library + "/compatdata/1245620");

    QVERIFY2(plan.env.value("LD_PRELOAD").contains("gameoverlayrenderer.so"),
             "the overlay still has to reach a Steam game");
}

QTEST_MAIN(TstLaunchPlan)
#include "tst_launchplan.moc"
