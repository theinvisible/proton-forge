// The GOG adapter's half of the details panel: turning what api.gog.com's v2
// endpoint says into the store-agnostic StoreEntryDetails the dialog renders.
//
// Two decisions live in that mapping and both are invisible from the panel if they
// break. Achievements arrive as one entry in the same feature list as cloud saves
// and controller support, and have to be lifted out or they show up twice. And the
// description is HTML, which a rich-text label would honour rather than display —
// so the tags come out before the panel ever sees them.

#include <QTest>

#include "gog/GogStoreService.h"

class TstGogStore : public QObject
{
    Q_OBJECT

private slots:
    void liftsAchievementsOutOfTheFeatureList();
    void stripsMarkupFromTheDescription();
    void passesThroughWhatNeedsNoTranslation();
    void refusesToTranslateAnInvalidAnswer();

private:
    static GogApiClient::GameDetails sample()
    {
        GogApiClient::GameDetails details;
        details.id = QStringLiteral("1207658930");
        details.description = QStringLiteral("A monster slayer.");
        details.features = {QStringLiteral("Achievements"), QStringLiteral("Cloud saves"),
                            QStringLiteral("Single-player")};
        details.genres = {QStringLiteral("Role-playing")};
        details.textLanguages = {QStringLiteral("English"), QStringLiteral("Czech")};
        details.voiceLanguages = {QStringLiteral("English")};
        details.developers = {QStringLiteral("CD PROJEKT RED")};
        details.publisher = QStringLiteral("CD PROJEKT RED");
        details.supportsWindows = true;
        details.supportsLinux = true;
        details.valid = true;
        return details;
    }
};

void TstGogStore::liftsAchievementsOutOfTheFeatureList()
{
    const StoreEntryDetails out = GogStoreService::toDetails(sample());

    QVERIFY(out.hasAchievements);
    QVERIFY(!out.features.contains(QStringLiteral("Achievements")));
    QCOMPARE(out.features, QStringList({"Cloud saves", "Single-player"}));

    // GOG never says how many there are. 0 alongside hasAchievements is the
    // interface's way of saying "yes, count unknown" — the panel prints "Yes"
    // rather than inventing a number.
    QCOMPARE(out.achievementCount, 0);

    GogApiClient::GameDetails without = sample();
    without.features.removeAll(QStringLiteral("Achievements"));
    QVERIFY(!GogStoreService::toDetails(without).hasAchievements);
}

void TstGogStore::stripsMarkupFromTheDescription()
{
    GogApiClient::GameDetails details = sample();
    details.description = QStringLiteral(
        "\n\n<p>The second installment<br>in the saga</p> <a href='x'>read more</a>");

    const StoreEntryDetails out = GogStoreService::toDetails(details);
    QCOMPARE(out.shortDescription,
             QStringLiteral("The second installment in the saga read more"));
    QVERIFY(!out.shortDescription.contains(QLatin1Char('<')));
}

void TstGogStore::passesThroughWhatNeedsNoTranslation()
{
    const StoreEntryDetails out = GogStoreService::toDetails(sample());

    QVERIFY(out.valid);
    QCOMPARE(out.genres, QStringList({"Role-playing"}));
    QCOMPARE(out.languages, QStringList({"English", "Czech"}));
    QCOMPARE(out.voiceLanguages, QStringList({"English"}));
    QCOMPARE(out.developers, QStringList({"CD PROJEKT RED"}));
    QCOMPARE(out.publishers, QStringList({"CD PROJEKT RED"}));
    QVERIFY(out.supportsWindows);
    QVERIFY(out.supportsLinux);
    QVERIFY(!out.supportsMac);

    // The v2 payload leaves the release date null for most of the catalogue, so
    // this stays empty rather than the panel growing a blank line.
    QVERIFY(out.releaseDate.isEmpty());
}

void TstGogStore::refusesToTranslateAnInvalidAnswer()
{
    // A failed parse must not come out as a valid-looking empty panel.
    GogApiClient::GameDetails invalid;
    invalid.description = QStringLiteral("would be shown if this leaked through");
    QVERIFY(!GogStoreService::toDetails(invalid).valid);
    QVERIFY(GogStoreService::toDetails(invalid).shortDescription.isEmpty());
}

QTEST_MAIN(TstGogStore)
#include "tst_gogstore.moc"
