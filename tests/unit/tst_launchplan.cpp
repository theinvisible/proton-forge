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
#include <QSignalSpy>

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
    void latestGePicksNumericallyNewest();
    void autoPicksNewestCachyOS();
    void mappingResolvesInternalNameFromVdf();
    void mappingResolvesValveInSecondaryLibrary();

    void aNonSteamGameGetsNoSteamEnvironment();
    void aNonSteamGameNeverGetsTheOverlayEvenWithSteamInstalled();
    void launcherArgumentsComeBeforeTheUsersOwn();
    void aGameWithNowhereToPutAPrefixIsRefused();
    void aSteamGameStillGetsTheFullSteamEnvironment();
    void aSecondGameIsRefusedWhileOneIsRunning();

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

    // A second library, registered in the root's libraryfolders.vdf.
    void addLibrary(const QString& root, const QString& library) const
    {
        QVERIFY(QDir().mkpath(library + "/steamapps"));
        QFile vdf(root + "/steamapps/libraryfolders.vdf");
        QVERIFY(vdf.open(QIODevice::WriteOnly | QIODevice::Text));
        vdf.write(QString(R"("libraryfolders" { "0" { "path" "%1" } "1" { "path" "%2" } })")
                      .arg(root, library).toUtf8());
    }

    // Steam's per-game compatibility tool choice, as config.vdf stores it.
    void mapCompatTool(const QString& root, const QString& appId, const QString& tool) const
    {
        makeFile(root + "/config/config.vdf",
                 QString(R"("InstallConfigStore" { "Software" { "Valve" { "Steam" {
                     "CompatToolMapping" { "%1" { "name" "%2" "config" "" "priority" "250" } }
                 } } } })").arg(appId, tool).toUtf8());
    }

    Game steamGame(const QString& appId) const
    {
        Game game(appId, "Some Game", "Steam");
        game.setTraits(SteamLauncher().traits());
        return game;
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

void TstLaunchPlan::latestGePicksNumericallyNewest()
{
    // Sorting names put GE-Proton9-20 above GE-Proton11-7 and GE-Proton10-9
    // above GE-Proton10-26.
    const QString compat = home() + "/.steam/root/compatibilitytools.d";
    for (const char* name : {"GE-Proton9-20", "GE-Proton10-9", "GE-Proton10-26"})
        makeProton(compat + "/" + name);

    DLSSSettings settings;
    settings.protonVersion = "latest-ge";

    GameRunner runner;
    QCOMPARE(runner.findProtonPath(Game(), settings), compat + "/GE-Proton10-26");

    makeProton(compat + "/GE-Proton11-7");
    QCOMPARE(runner.findProtonPath(Game(), settings), compat + "/GE-Proton11-7");
}

void TstLaunchPlan::autoPicksNewestCachyOS()
{
    // The default used to take the first match in ascending name order — the
    // oldest Proton-CachyOS on disk.
    const QString compat = home() + "/.steam/root/compatibilitytools.d";
    makeProton(compat + "/proton-cachyos-10.0-20260127-slr-x86_64");
    makeProton(compat + "/proton-cachyos-11.0-20260703-slr-x86_64");
    makeProton(compat + "/GE-Proton11-7");

    GameRunner runner;
    QCOMPARE(runner.findProtonPath(Game(), DLSSSettings()),
             compat + "/proton-cachyos-11.0-20260703-slr-x86_64");
}

void TstLaunchPlan::mappingResolvesInternalNameFromVdf()
{
    // Steam stores the name a tool registers in its compatibilitytool.vdf,
    // which need not be its directory name.
    makeSteamRoot(nativeRoot());
    const QString tool = nativeRoot() + "/compatibilitytools.d/proton-cachyos-11.0-20261005-slr-x86_64_v3";
    makeProton(tool);
    makeProton(nativeRoot() + "/compatibilitytools.d/GE-Proton11-7");
    makeFile(tool + "/compatibilitytool.vdf",
             R"vdf("compatibilitytools" { "compat_tools" { "proton-cachyos-slr" // Internal name
                 { "install_path" "." "display_name" "proton-cachyos (slr)" } } })vdf");
    mapCompatTool(nativeRoot(), "1245620", "proton-cachyos-slr");

    GameRunner runner;
    QCOMPARE(runner.findProtonPath(steamGame("1245620"), DLSSSettings()), tool);
}

void TstLaunchPlan::mappingResolvesValveInSecondaryLibrary()
{
    // "proton_11" is an internal name; the directory is "Proton 11.0", and it
    // may live in any library, not just the one under the Steam root.
    makeSteamRoot(nativeRoot());
    const QString library = home() + "/SecondDrive/SteamLibrary";
    addLibrary(nativeRoot(), library);
    const QString valve = library + "/steamapps/common/Proton 11.0";
    makeProton(valve);
    makeProton(nativeRoot() + "/compatibilitytools.d/proton-cachyos-11.0-20260703-slr-x86_64");
    mapCompatTool(nativeRoot(), "1245620", "proton_11");

    GameRunner runner;
    QCOMPARE(runner.findProtonPath(steamGame("1245620"), DLSSSettings()), valve);

    // An alias resolves the same way.
    mapCompatTool(nativeRoot(), "1245620", "proton-stable");
    QCOMPARE(runner.findProtonPath(steamGame("1245620"), DLSSSettings()), valve);
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

void TstLaunchPlan::aSecondGameIsRefusedWhileOneIsRunning()
{
    // GameRunner has one process slot. Replacing a running QProcess kills its
    // child, so starting B used to end A without a word.
    auto nativeGame = [this](const QString& id, const QString& name) {
        const QString install = home() + "/Games/" + name;
        const QString exe = install + "/" + name.toLower();
        makeFile(exe, "#!/bin/sh\nexec sleep 30\n");
        QFile(exe).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        Game game(id, name, "GOG");
        game.setInstallPath(install);
        game.setExecutablePath(exe);
        game.setIsNativeLinux(true);
        return game;
    };
    const Game first = nativeGame("1", "First");
    const Game second = nativeGame("2", "Second");

    GameRunner runner;
    QSignalSpy errors(&runner, &GameRunner::launchError);

    QVERIFY2(runner.launch(first, DLSSSettings()), "the first game has to start");
    QVERIFY(runner.isGameRunning(first));

    QVERIFY2(!runner.launch(second, DLSSSettings()), "a second game has to be refused");
    QCOMPARE(errors.count(), 1);
    QVERIFY2(errors.at(0).at(1).toString().contains("First"),
             "the refusal should name the game that is running");

    QVERIFY2(runner.isGameRunning(first), "refusing the second game must not stop the first");
    QVERIFY(!runner.isGameRunning(second));
    QCOMPARE(runner.runningGame().id(), QStringLiteral("1"));
}

QTEST_MAIN(TstLaunchPlan)
#include "tst_launchplan.moc"
