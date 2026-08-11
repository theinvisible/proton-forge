// The Steam side of IStoreService: owned games from the Web API, and working
// out which account to ask about without making the user find their own 64-bit
// id.
//
// Both parsers matter for reasons that are not obvious from the code. An empty
// games array is the *privacy setting*, not an empty account, and the caller has
// to be able to tell. And loginusers.vdf can name several accounts, in which
// case picking one at random would silently show a stranger's library.

#include <QTest>

#include "launchers/SteamStoreService.h"

class TstSteamStore : public QObject
{
    Q_OBJECT

private slots:
    void parsesOwnedGames();
    void ordersGamesByTitle();
    void reportsAnEmptyLibraryAsEmpty();
    void skipsGamesWithoutAnAppId();
    void survivesGarbage();

    void picksTheMostRecentAccount();
    void picksTheOnlyAccount();
    void refusesToGuessBetweenAccounts();
    void handlesAMissingOrEmptyLoginUsers();

    void headerImageMatchesTheInstalledGamePattern();
};

void TstSteamStore::parsesOwnedGames()
{
    const QByteArray json = R"({
        "response": {
            "game_count": 2,
            "games": [
                {"appid": 1245620, "name": "ELDEN RING", "playtime_forever": 4200},
                {"appid": 1145360, "name": "Hades", "playtime_forever": 0}
            ]
        }
    })";

    int count = 0;
    const QList<StoreEntry> entries = SteamStoreService::parseOwnedGames(json, &count);

    QCOMPARE(count, 2);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.first().id, QStringLiteral("1245620"));
    QCOMPARE(entries.first().title, QStringLiteral("ELDEN RING"));

    // ProtonForge never installs a Steam game itself — it hands the appid to
    // the Steam client, which is what installUrl is for.
    QCOMPARE(entries.first().installUrl, QStringLiteral("steam://install/1245620"));
    QVERIFY(entries.first().installable);
    QVERIFY(entries.first().storeUrl.contains("store.steampowered.com/app/1245620"));

    // The Web API says nothing about platforms. Guessing would put a wrong
    // badge on every row, so both stay false.
    QVERIFY(!entries.first().supportsWindows);
    QVERIFY(!entries.first().supportsLinux);
}

void TstSteamStore::ordersGamesByTitle()
{
    // GetOwnedGames answers in appid order, which on screen reads as no order at
    // all. Sorted in the parser rather than by whoever draws the list, because
    // --store-list consumes the same entries and is not a list widget.
    const QByteArray json = R"({
        "response": {
            "games": [
                {"appid": 100, "name": "Portal 2"},
                {"appid": 101, "name": "aperture desk job"},
                {"appid": 102, "name": "Hades"},
                {"appid": 103, "name": "Zork"},
                {"appid": 104, "name": "ELDEN RING"}
            ]
        }
    })";

    const QList<StoreEntry> entries = SteamStoreService::parseOwnedGames(json, nullptr);

    QStringList titles;
    for (const StoreEntry& entry : entries) {
        titles << entry.title;
    }
    // Case-insensitive: a lower-case title belongs among the others, not before
    // or after all of them.
    QCOMPARE(titles, QStringList({"aperture desk job", "ELDEN RING", "Hades",
                                  "Portal 2", "Zork"}));

    // std::sort is not stable, so two products sharing a title could come out
    // either way round depending on the order the API happened to list them. The
    // id breaks the tie, which is what makes the list the same on every fetch.
    const QByteArray oneWay = R"({"response": {"games": [
        {"appid": 200, "name": "Soundtrack"}, {"appid": 201, "name": "Soundtrack"}]}})";
    const QByteArray theOther = R"({"response": {"games": [
        {"appid": 201, "name": "Soundtrack"}, {"appid": 200, "name": "Soundtrack"}]}})";

    const QList<StoreEntry> a = SteamStoreService::parseOwnedGames(oneWay, nullptr);
    const QList<StoreEntry> b = SteamStoreService::parseOwnedGames(theOther, nullptr);
    QCOMPARE(a.size(), 2);
    QCOMPARE(a.first().id, b.first().id);
    QCOMPARE(a.last().id, b.last().id);
}

void TstSteamStore::reportsAnEmptyLibraryAsEmpty()
{
    // What a private profile actually returns. Indistinguishable from owning
    // nothing, which is why the caller turns this into an explanation about
    // privacy settings rather than a blank list.
    int count = -1;
    const QList<StoreEntry> entries =
        SteamStoreService::parseOwnedGames(R"({"response": {"game_count": 0}})", &count);

    QCOMPARE(count, 0);
    QVERIFY(entries.isEmpty());
}

void TstSteamStore::skipsGamesWithoutAnAppId()
{
    const QByteArray json = R"({
        "response": {"games": [{"name": "Nameless"}, {"appid": 42, "name": "Fine"}]}
    })";

    int count = 0;
    const QList<StoreEntry> entries = SteamStoreService::parseOwnedGames(json, &count);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().id, QStringLiteral("42"));
}

void TstSteamStore::survivesGarbage()
{
    for (const QByteArray& bad : {QByteArray(""), QByteArray("not json"), QByteArray("[]"),
                                  QByteArray("{}"), QByteArray("<html>403</html>")}) {
        int count = -1;
        QVERIFY2(SteamStoreService::parseOwnedGames(bad, &count).isEmpty(), "games: " + bad);
        QCOMPARE(count, 0);
    }
}

void TstSteamStore::picksTheMostRecentAccount()
{
    const QByteArray vdf = R"("users"
{
	"76561198000000001"
	{
		"AccountName"		"olduser"
		"MostRecent"		"0"
	}
	"76561198000000002"
	{
		"AccountName"		"currentuser"
		"MostRecent"		"1"
	}
}
)";

    QCOMPARE(SteamStoreService::steamIdFromLoginUsers(vdf),
             QStringLiteral("76561198000000002"));
}

void TstSteamStore::picksTheOnlyAccount()
{
    const QByteArray vdf = R"("users"
{
	"76561198000000001"
	{
		"AccountName"		"onlyuser"
	}
}
)";

    // Nobody flagged, but there is nothing to be ambiguous about.
    QCOMPARE(SteamStoreService::steamIdFromLoginUsers(vdf),
             QStringLiteral("76561198000000001"));
}

void TstSteamStore::refusesToGuessBetweenAccounts()
{
    const QByteArray vdf = R"("users"
{
	"76561198000000001"
	{
		"AccountName"		"one"
	}
	"76561198000000002"
	{
		"AccountName"		"two"
	}
}
)";

    // Two accounts, neither flagged. Picking one would silently list a
    // stranger's library; returning nothing sends the user to Settings.
    QVERIFY(SteamStoreService::steamIdFromLoginUsers(vdf).isEmpty());
}

void TstSteamStore::handlesAMissingOrEmptyLoginUsers()
{
    for (const QByteArray& bad : {QByteArray(""), QByteArray("garbage"),
                                  QByteArray(R"("users" { })")}) {
        QVERIFY2(SteamStoreService::steamIdFromLoginUsers(bad).isEmpty(), "accepted: " + bad);
    }
}

void TstSteamStore::headerImageMatchesTheInstalledGamePattern()
{
    // Same CDN URL SteamLauncher builds for installed games, so an owned-but-
    // not-installed row and an installed one share a cache entry rather than
    // downloading the same picture twice.
    QCOMPARE(SteamStoreService::headerImageUrl("1245620"),
             QStringLiteral("https://steamcdn-a.akamaihd.net/steam/apps/1245620/header.jpg"));
}

QTEST_MAIN(TstSteamStore)
#include "tst_steamstore.moc"
