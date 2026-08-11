// The registry used to filter launchers by isAvailable() at registration time,
// inside the singleton's constructor — so availability was decided once per
// process and could never change. A store launcher only becomes usable when the
// user signs in, which is always later than that.
//
// So: register unconditionally, ask about availability at call time, and tell
// the UI when the answer moved. These cases pin all three, plus the central
// stamping of launcher identity onto discovered games.
//
// ILauncher has no Qt dependencies, which makes a fake one three lines of work.

#include <QTest>
#include <QSignalSpy>

#include "launchers/LauncherManager.h"
#include "launchers/ILauncher.h"

namespace {

class FakeLauncher : public ILauncher
{
public:
    FakeLauncher(QString name, bool available)
        : m_name(std::move(name))
        , m_available(available)
    {
    }

    QString name() const override { return m_name; }
    LauncherTraits traits() const override { return m_traits; }
    bool isAvailable() const override { return m_available; }

    QList<Game> discoverGames() override
    {
        ++m_discoverCalls;
        return m_games;
    }

    bool applySettings(const Game&, const DLSSSettings&) override { return false; }
    QString getLaunchCommand(const Game&, const DLSSSettings&) override { return QString(); }

    void setAvailable(bool available) { m_available = available; }
    void setGames(const QList<Game>& games) { m_games = games; }
    void setTraits(const LauncherTraits& traits) { m_traits = traits; }
    int discoverCalls() const { return m_discoverCalls; }

private:
    QString m_name;
    bool m_available;
    LauncherTraits m_traits;
    QList<Game> m_games;
    int m_discoverCalls = 0;
};

} // namespace

class TstLauncherManager : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void resolvesALauncherThatIsNotCurrentlyAvailable();
    void discoveryOnlyAsksAvailableLaunchers();
    void discoveryPicksUpALauncherThatBecomesAvailable();
    void discoveredGamesCarryTheirLauncherAndTraits();
    void availabilityChangedOnlyFiresOnAnActualChange();
    void availabilityChangedFiresWhenALauncherGoesAway();

private:
    LauncherManager& manager() { return LauncherManager::instance(); }

    // resetForTesting() leaves the change-detection baseline empty, which is
    // what a manager looks like before its constructor seeds it. Seed it here
    // so each case measures movement from a known state rather than from
    // "nothing was available".
    void seedBaseline() { manager().refreshAvailability(); }
};

void TstLauncherManager::init()
{
    // Process-wide singleton, so every case starts by emptying it.
    manager().resetForTesting();
}

void TstLauncherManager::resolvesALauncherThatIsNotCurrentlyAvailable()
{
    auto offline = std::make_shared<FakeLauncher>("GOG", false);
    manager().registerLauncher(offline);

    // A game discovered while the launcher was up stays on screen after it goes
    // down. Looking it up has to keep working, or the UI reports "launcher not
    // found" instead of whatever the real problem is.
    QVERIFY(manager().launcher("GOG") != nullptr);
    QVERIFY(manager().availableLaunchers().isEmpty());
    QCOMPARE(manager().launchers().size(), 1);
}

void TstLauncherManager::discoveryOnlyAsksAvailableLaunchers()
{
    auto up = std::make_shared<FakeLauncher>("Steam", true);
    auto down = std::make_shared<FakeLauncher>("GOG", false);
    up->setGames({Game("1", "Up", QString())});
    down->setGames({Game("2", "Down", QString())});

    manager().registerLauncher(up);
    manager().registerLauncher(down);

    const QList<Game> games = manager().discoverAllGames();

    QCOMPARE(games.size(), 1);
    QCOMPARE(games.first().name(), QStringLiteral("Up"));
    QCOMPARE(up->discoverCalls(), 1);
    QCOMPARE(down->discoverCalls(), 0);
}

void TstLauncherManager::discoveryPicksUpALauncherThatBecomesAvailable()
{
    // The whole point of the change: this used to be impossible for the life of
    // the process, because registration had already dropped the launcher.
    auto gog = std::make_shared<FakeLauncher>("GOG", false);
    gog->setGames({Game("1207664663", "The Witcher 3", QString())});
    manager().registerLauncher(gog);

    QVERIFY(manager().discoverAllGames().isEmpty());

    gog->setAvailable(true);

    QCOMPARE(manager().discoverAllGames().size(), 1);
}

void TstLauncherManager::discoveredGamesCarryTheirLauncherAndTraits()
{
    LauncherTraits traits;
    traits.usesSteamEnv = true;
    traits.idIsSteamAppId = true;

    auto launcher = std::make_shared<FakeLauncher>("Steam", true);
    launcher->setTraits(traits);
    // Deliberately returned with neither set, to prove the manager stamps them.
    launcher->setGames({Game("1245620", "ELDEN RING", QString())});
    manager().registerLauncher(launcher);

    const QList<Game> games = manager().discoverAllGames();
    QCOMPARE(games.size(), 1);

    QCOMPARE(games.first().launcher(), QStringLiteral("Steam"));
    QCOMPARE(games.first().settingsKey(), QStringLiteral("Steam:1245620"));
    QVERIFY(games.first().traits().usesSteamEnv);
    QVERIFY(games.first().traits().idIsSteamAppId);
    QVERIFY(!games.first().traits().providesUpdateState);
}

void TstLauncherManager::availabilityChangedOnlyFiresOnAnActualChange()
{
    auto gog = std::make_shared<FakeLauncher>("GOG", false);
    manager().registerLauncher(gog);
    seedBaseline();

    QSignalSpy spy(&manager(), &LauncherManager::availabilityChanged);

    // Nothing moved. A signal here would reload the whole game list on every
    // Refresh for no reason.
    manager().refreshAvailability();
    QCOMPARE(spy.count(), 0);

    gog->setAvailable(true);
    manager().refreshAvailability();
    QCOMPARE(spy.count(), 1);

    // And it settles again rather than firing on every subsequent check.
    manager().refreshAvailability();
    QCOMPARE(spy.count(), 1);
}

void TstLauncherManager::availabilityChangedFiresWhenALauncherGoesAway()
{
    auto gog = std::make_shared<FakeLauncher>("GOG", true);
    manager().registerLauncher(gog);
    seedBaseline();

    QSignalSpy spy(&manager(), &LauncherManager::availabilityChanged);

    gog->setAvailable(false);   // signing out
    manager().refreshAvailability();

    QCOMPARE(spy.count(), 1);
    QVERIFY(manager().availableLaunchers().isEmpty());
    QVERIFY2(manager().launcher("GOG") != nullptr, "it is still registered, just not usable");
}

QTEST_MAIN(TstLauncherManager)
#include "tst_launchermanager.moc"
