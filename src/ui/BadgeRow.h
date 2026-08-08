#ifndef BADGEROW_H
#define BADGEROW_H

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QList>
#include <QPainter>
#include <QRect>
#include <QString>

// Small pill labels drawn in a row: LINUX/WINDOWS/UPDATE on a game,
// LATEST/INSTALLED on a Proton build.
//
// Both delegates used to lay these out by hand, which meant every badge's
// position was written in terms of the one before it. That survives exactly two
// badges — adding a third means rewriting the arithmetic at the call site. So
// the arithmetic lives here once and callers only say what to draw.
namespace BadgeRow {

struct Badge {
    QString label;
    QColor color;
};

struct Metrics {
    int height  = 16;
    int padding = 6;   // per side, horizontal
    int gap     = 6;   // between badges
};

inline int badgeWidth(const Badge& badge, const QFontMetrics& fm, const Metrics& metrics = {})
{
    return fm.horizontalAdvance(badge.label) + metrics.padding * 2;
}

// Left to right starting at `left`, top edge at `top`.
inline QList<QRect> layoutFromLeft(const QList<Badge>& badges, const QFontMetrics& fm,
                                   int left, int top, const Metrics& metrics = {})
{
    QList<QRect> rects;
    rects.reserve(badges.size());
    int x = left;
    for (const Badge& badge : badges) {
        const int width = badgeWidth(badge, fm, metrics);
        rects.append(QRect(x, top, width, metrics.height));
        x += width + metrics.gap;
    }
    return rects;
}

// Right-aligned: the last badge ends at `right`, while the list still reads left
// to right. `leftEdge` receives where the row begins, which is what a caller
// needs in order to know how much room is left for text beside it.
inline QList<QRect> layoutFromRight(const QList<Badge>& badges, const QFontMetrics& fm,
                                    int right, int centerY, int* leftEdge = nullptr,
                                    const Metrics& metrics = {})
{
    QList<QRect> rects(badges.size());
    const int top = centerY - metrics.height / 2;
    int x = right;
    for (int i = badges.size() - 1; i >= 0; --i) {
        const int width = badgeWidth(badges[i], fm, metrics);
        rects[i] = QRect(x - width, top, width, metrics.height);
        x -= width + metrics.gap;
    }
    if (leftEdge) {
        *leftEdge = badges.isEmpty() ? right : x + metrics.gap;
    }
    return rects;
}

inline void draw(QPainter* painter, const QList<Badge>& badges, const QList<QRect>& rects,
                 const QFont& font)
{
    painter->setFont(font);
    for (int i = 0; i < badges.size() && i < rects.size(); ++i) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(badges[i].color);
        painter->drawRoundedRect(rects[i], 3, 3);
        painter->setPen(Qt::white);
        painter->drawText(rects[i], Qt::AlignCenter, badges[i].label);
    }
}

} // namespace BadgeRow

#endif // BADGEROW_H
