// The badge row is pure geometry, and it replaced arithmetic that was written
// out by hand at each call site — first badge at the text edge, second at
// "first + its width + 6". That shape survives exactly two badges, which is why
// it had to go before a third one (the game's source) could be added.
//
// Pixel widths depend on the font, so nothing here asserts absolute positions.
// What is pinned is the relationships: where the row starts, that badges sit
// flush in order with a constant gap, and that a right-aligned row ends where it
// was told to. That is the whole contract, and it is exactly what a screenshot
// would not have told us reliably.

#include <QTest>
#include <QFont>
#include <QFontMetrics>

#include "ui/BadgeRow.h"

class TstBadgeRow : public QObject
{
    Q_OBJECT

private slots:
    void leftRowStartsWhereToldAndPacksInOrder();
    void leftRowHandlesAnEmptyList();
    void rightRowEndsWhereToldAndKeepsListOrder();
    void rightRowReportsWhereTextMayRunTo();

private:
    QFontMetrics metrics() const
    {
        QFont font;
        font.setPixelSize(9);
        font.setBold(true);
        return QFontMetrics(font);
    }

    QList<BadgeRow::Badge> threeBadges() const
    {
        return {{"GOG", QColor("#7c2bbb")},
                {"WINDOWS", QColor("#1565c0")},
                {"UPDATE", QColor("#d98c00")}};
    }
};

void TstBadgeRow::leftRowStartsWhereToldAndPacksInOrder()
{
    const BadgeRow::Metrics m;
    const QFontMetrics fm = metrics();
    const QList<BadgeRow::Badge> badges = threeBadges();

    const QList<QRect> rects = BadgeRow::layoutFromLeft(badges, fm, 140, 60, m);

    QCOMPARE(rects.size(), 3);
    QCOMPARE(rects.first().left(), 140);

    for (int i = 0; i < rects.size(); ++i) {
        QCOMPARE(rects[i].top(), 60);
        QCOMPARE(rects[i].height(), m.height);
        // Text plus one padding either side — the same formula the hand-written
        // version used.
        QCOMPARE(rects[i].width(), fm.horizontalAdvance(badges[i].label) + m.padding * 2);
        if (i > 0) {
            QCOMPARE(rects[i].left(), rects[i - 1].left() + rects[i - 1].width() + m.gap);
        }
    }
}

void TstBadgeRow::leftRowHandlesAnEmptyList()
{
    // A game with no badges at all is not hypothetical: the source badge is
    // hidden with one launcher, and the platform badge is the only other.
    QVERIFY(BadgeRow::layoutFromLeft({}, metrics(), 140, 60).isEmpty());
}

void TstBadgeRow::rightRowEndsWhereToldAndKeepsListOrder()
{
    const BadgeRow::Metrics m;
    const QFontMetrics fm = metrics();
    const QList<BadgeRow::Badge> badges = {{"LATEST", QColor()}, {"INSTALLED", QColor()}};

    const QList<QRect> rects = BadgeRow::layoutFromRight(badges, fm, 500, 40, nullptr, m);

    QCOMPARE(rects.size(), 2);
    // The list still reads left to right; it is the row that is right-aligned.
    QCOMPARE(rects.last().left() + rects.last().width(), 500);
    QCOMPARE(rects[1].left(), rects[0].left() + rects[0].width() + m.gap);
    // Vertically centred on the row. Asserted on the top edge rather than
    // QRect::center(), which rounds down for an even height and would make this
    // read as an off-by-one that is not there.
    QCOMPARE(rects[0].top(), 40 - m.height / 2);
}

void TstBadgeRow::rightRowReportsWhereTextMayRunTo()
{
    const BadgeRow::Metrics m;
    const QFontMetrics fm = metrics();
    const QList<BadgeRow::Badge> badges = {{"LATEST", QColor()}, {"INSTALLED", QColor()}};

    int leftEdge = -1;
    const QList<QRect> rects = BadgeRow::layoutFromRight(badges, fm, 500, 40, &leftEdge, m);
    QCOMPARE(leftEdge, rects.first().left());

    // With nothing to draw, the caller may use the full width.
    int emptyEdge = -1;
    BadgeRow::layoutFromRight({}, fm, 500, 40, &emptyEdge, m);
    QCOMPARE(emptyEdge, 500);
}

QTEST_MAIN(TstBadgeRow)
#include "tst_badgerow.moc"
