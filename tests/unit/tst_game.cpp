// Game is mostly a bag of setters, but three things about it are load-bearing
// and each has already gone wrong once:
//
//   compatDataPath()/shaderCachePath() are derived, not stored. Concatenating
//   an empty library path used to yield "/compatdata/<id>" — an absolute path
//   at the filesystem root that mkpath quietly fails on. Empty has to stay
//   empty so the launch can refuse instead of inventing a prefix.
//
//   settingsKey() is a key in the user's settings.json. Change its shape and
//   every per-game profile in the wild is orphaned, silently.
//
//   needsUpdate() is stored. It used to be (stateFlags & 2), which is Steam's
//   ACF bitmask and means nothing to any other launcher.

#include <QTest>

#include "core/Game.h"

class TstGame : public QObject
{
    Q_OBJECT

private slots:
    void compatDataPathPrefersTheExplicitValue();
    void compatDataPathFallsBackToTheSteamLayout();
    void compatDataPathIsEmptyWithNothingToDeriveFrom();
    void shaderCachePathFollowsTheSameThreeRules();
    void settingsKeyIsNamespacedByLauncher();
    void gamesDifferByLauncherAsWellAsId();
    void needsUpdateIsStoredNotDerived();
    void launcherSuppliedFieldsRoundTrip();
    void traitsDefaultToNothing();
};

void TstGame::compatDataPathPrefersTheExplicitValue()
{
    Game game("1245620", "ELDEN RING", "GOG");
    game.setLibraryPath("/games/steamapps");
    game.setCompatDataPath("/games/ProtonForge/prefixes/GOG/1245620");

    QCOMPARE(game.compatDataPath(), QStringLiteral("/games/ProtonForge/prefixes/GOG/1245620"));
}

void TstGame::compatDataPathFallsBackToTheSteamLayout()
{
    // Byte-identical to what GameRunner used to build inline. This pins the
    // Steam layout: existing prefixes must keep resolving to the same directory
    // or every Steam user loses their saves and settings.
    Game game("1245620", "ELDEN RING", "Steam");
    game.setLibraryPath("/games/steamapps");

    QCOMPARE(game.compatDataPath(), QStringLiteral("/games/steamapps/compatdata/1245620"));
}

void TstGame::compatDataPathIsEmptyWithNothingToDeriveFrom()
{
    Game game("1245620", "ELDEN RING", "GOG");

    QVERIFY2(game.compatDataPath().isEmpty(),
             "an unset library path must not produce a root-relative /compatdata/<id>");
}

void TstGame::shaderCachePathFollowsTheSameThreeRules()
{
    Game explicitPath("42", "Game", "GOG");
    explicitPath.setLibraryPath("/games/steamapps");
    explicitPath.setShaderCachePath("/elsewhere/shaders");
    QCOMPARE(explicitPath.shaderCachePath(), QStringLiteral("/elsewhere/shaders"));

    Game derived("42", "Game", "Steam");
    derived.setLibraryPath("/games/steamapps");
    QCOMPARE(derived.shaderCachePath(),
             QStringLiteral("/games/steamapps/shadercache/42/fozpipelinesv6"));

    QVERIFY(Game("42", "Game", "GOG").shaderCachePath().isEmpty());
}

void TstGame::settingsKeyIsNamespacedByLauncher()
{
    // The persisted format. If this assertion ever needs editing, every user's
    // settings.json needs a migration in the same change.
    QCOMPARE(Game("1245620", "ELDEN RING", "Steam").settingsKey(),
             QStringLiteral("Steam:1245620"));
    QCOMPARE(Game("1207664663", "The Witcher 3", "GOG").settingsKey(),
             QStringLiteral("GOG:1207664663"));
}

void TstGame::gamesDifferByLauncherAsWellAsId()
{
    // Two stores can hand out the same number, and they are not the same game.
    const Game steam("1207664663", "Something", "Steam");
    const Game gog("1207664663", "Something", "GOG");

    QVERIFY(!(steam == gog));
    QVERIFY(steam == Game("1207664663", "Different name, same identity", "Steam"));
}

void TstGame::needsUpdateIsStoredNotDerived()
{
    Game game("1245620", "ELDEN RING", "Steam");
    QVERIFY(!game.needsUpdate());

    // 6 is Steam's "fully installed + update required". Setting the raw flags
    // must NOT move needsUpdate on its own — only the launcher that understands
    // the encoding gets to decide.
    game.setStateFlags(6);
    QVERIFY2(!game.needsUpdate(), "needsUpdate must not be derived from stateFlags");

    game.setNeedsUpdate(true);
    QVERIFY(game.needsUpdate());
    QCOMPARE(game.stateFlags(), 6);
}

void TstGame::launcherSuppliedFieldsRoundTrip()
{
    Game game;
    game.setWorkingDirectory("/games/GOG/Witcher3");
    game.setLaunchArgs({"--launcher-skip", "-opengl"});
    game.setVersion("1.32-GOTY");

    QCOMPARE(game.workingDirectory(), QStringLiteral("/games/GOG/Witcher3"));
    QCOMPARE(game.launchArgs(), QStringList({"--launcher-skip", "-opengl"}));
    QCOMPARE(game.version(), QStringLiteral("1.32-GOTY"));

    // Steam's numeric buildId and a store's opaque version string are kept
    // apart rather than coerced into one another.
    QCOMPARE(game.buildId(), 0LL);
}

void TstGame::traitsDefaultToNothing()
{
    // A game nobody stamped gets the conservative answer everywhere: no Steam
    // environment, no client to wait for, no launch-option writeback, no
    // ProtonDB lookup keyed by its id.
    const LauncherTraits traits = Game().traits();

    QVERIFY(!traits.usesSteamEnv);
    QVERIFY(!traits.requiresClientRunning);
    QVERIFY(!traits.supportsLaunchOptionsIO);
    QVERIFY(!traits.providesUpdateState);
    QVERIFY(!traits.idIsSteamAppId);
}

QTEST_MAIN(TstGame)
#include "tst_game.moc"
