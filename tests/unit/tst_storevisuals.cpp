// StoreVisuals is the one place that knows what a store looks like — it replaced
// a colour map in GameListWidget's delegate and a lettered-circle painter in
// SettingsDialog, both of which had their own copy of the same two hex values.
//
// Two things are worth pinning here, and neither is a screenshot.
//
// The first is the colour contract the badge delegate depends on: an unknown
// launcher must come back neutral rather than borrowing another store's colour.
//
// The second is the failure mode this feature can actually regress into. A QIcon
// built on a resource path that is not in resources.qrc is *silently null* — no
// warning, no crash, just a row that draws no icon. The qrc is compiled into the
// executable and not into this test, so instead of loading the assets we read the
// .qrc itself and assert that every path the table names is listed in it. A typo
// in "icons/stores/gog.svg" then fails here instead of quietly shipping.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "ui/StoreVisuals.h"

class TstStoreVisuals : public QObject
{
    Q_OBJECT

private slots:
    void knownStoresHaveDistinctAccents();
    void unknownStoreFallsBackToNeutral();
    void onlyKnownStoresClaimAnAsset();
    void everyNamedAssetIsInTheResourceFile();
    void compositedCircleIsDrawnAtTheRequestedSize();

private:
    // PROTONFORGE_FIXTURES_DIR is <repo>/tests/steam-lab/fixtures.
    static QString repoRoot()
    {
        return QDir(QStringLiteral(PROTONFORGE_FIXTURES_DIR) + "/../../..").canonicalPath();
    }
};

void TstStoreVisuals::knownStoresHaveDistinctAccents()
{
    QCOMPARE(StoreVisuals::accentColor("Steam"), QColor(AppStyle::ColorStoreSteam));
    QCOMPARE(StoreVisuals::accentColor("GOG"), QColor(AppStyle::ColorStoreGog));
    QVERIFY(StoreVisuals::accentColor("Steam") != StoreVisuals::accentColor("GOG"));
}

void TstStoreVisuals::unknownStoreFallsBackToNeutral()
{
    const QColor neutral(AppStyle::ColorStoreUnknown);
    QCOMPARE(StoreVisuals::accentColor("Epic"), neutral);
    QCOMPARE(StoreVisuals::accentColor(QString()), neutral);
    // Exact match on purpose — no prefix or case-insensitive matching, or a
    // future launcher would inherit Steam's identity.
    QCOMPARE(StoreVisuals::accentColor("Steam Deck"), neutral);
    QCOMPARE(StoreVisuals::accentColor("steam"), neutral);
}

void TstStoreVisuals::onlyKnownStoresClaimAnAsset()
{
    QVERIFY(!StoreVisuals::assetPath("Steam").isEmpty());
    QVERIFY(!StoreVisuals::assetPath("GOG").isEmpty());
    QVERIFY(StoreVisuals::assetPath("Epic").isEmpty());
    QVERIFY(StoreVisuals::assetPath(QString()).isEmpty());
}

void TstStoreVisuals::everyNamedAssetIsInTheResourceFile()
{
    QFile qrc(repoRoot() + "/resources.qrc");
    QVERIFY2(qrc.open(QIODevice::ReadOnly), qPrintable(qrc.fileName()));
    const QString contents = QString::fromUtf8(qrc.readAll());

    for (const QString& launcher : {QStringLiteral("Steam"), QStringLiteral("GOG")}) {
        const QString path = StoreVisuals::assetPath(launcher);
        QVERIFY2(path.startsWith(":/"), qPrintable(path));

        const QString relative = path.mid(2);
        QVERIFY2(contents.contains("<file>" + relative + "</file>"), qPrintable(relative));
        // And the file the qrc points at exists, which is the other half of the
        // same silent-null failure.
        QVERIFY2(QFileInfo::exists(repoRoot() + "/" + relative), qPrintable(relative));
    }
}

void TstStoreVisuals::compositedCircleIsDrawnAtTheRequestedSize()
{
    // The fallback path for a store with no asset, and what the GitHub category
    // row is drawn with. Runs offscreen; QTEST_MAIN gives it a QApplication.
    const QIcon icon = StoreVisuals::circleIcon(QColor(AppStyle::ColorGitHub), "E");
    QVERIFY(!icon.isNull());
    QCOMPARE(icon.pixmap(QSize(36, 36)).size(), QSize(36, 36));
    // The 2x pixmap is there so a HiDPI screen is not handed an upscaled 36px
    // bitmap.
    QCOMPARE(icon.pixmap(QSize(72, 72)).size(), QSize(72, 72));

    QVERIFY(!StoreVisuals::icon("Epic").isNull());
}

QTEST_MAIN(TstStoreVisuals)
#include "tst_storevisuals.moc"
