#ifndef STOREVISUALS_H
#define STOREVISUALS_H

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>

#include "AppStyle.h"

// How a store is *drawn*. One table, read by the three places that show a store
// to the user: the STORE list in StoreLibraryDialog, the category sidebar in
// SettingsDialog and the source filter in GameListWidget. It replaced a colour
// map and a lettered-circle painter that each of those had grown its own copy of.
//
// Header-only on purpose, like BadgeRow.h: a .cpp would land in UI_SOURCES,
// which is executable-only, and the unit test could not link it.
namespace StoreVisuals {

// Presentation only. A launcher without an entry here gets the neutral colour
// and a lettered circle — nothing behavioural hangs off this, unlike the traits.
//
// Keyed on the stable launcher id (ILauncher::name(), which
// IStoreService::launcherName() must equal), never on displayName().
struct Visual {
    const char* assetPath;   // nullptr: no logo, so composite a circle instead
    const char* color;
};

// Exact match on purpose: a fuzzy one would silently hand a future "Steam Deck"
// the Steam logo.
inline Visual lookup(const QString& launcher)
{
    if (launcher == QLatin1String("Steam")) {
        return { ":/icons/stores/steam.svg", AppStyle::ColorStoreSteam };
    }
    if (launcher == QLatin1String("GOG")) {
        return { ":/icons/stores/gog.svg", AppStyle::ColorStoreGog };
    }
    return { nullptr, AppStyle::ColorStoreUnknown };
}

inline QColor accentColor(const QString& launcher)
{
    return QColor(lookup(launcher).color);
}

// Empty when the store has no logo. Kept separate from icon() because a QIcon
// built on a missing resource path is silently null — no crash, just a blank
// row — so the unit test needs the path as a plain string to check it against
// resources.qrc.
inline QString assetPath(const QString& launcher)
{
    const Visual visual = lookup(launcher);
    return visual.assetPath ? QString::fromLatin1(visual.assetPath) : QString();
}

namespace detail {

inline QPixmap circlePixmap(const QColor& color, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, size, size);
    return pixmap;
}

} // namespace detail

// A coloured disc with a letter on it. The two store logos are self-coloured
// SVGs and never come through here; this is for the rows that have no asset —
// the GitHub category and any store we do not know yet.
//
// Both sizes go into the icon so a 2x display gets the 72px one instead of a
// scaled-up 36px bitmap.
inline QIcon circleIcon(const QColor& color, const QString& letter, int size = 36)
{
    QIcon icon;
    for (int px : { size, size * 2 }) {
        QPixmap pixmap = detail::circlePixmap(color, px);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(px * 4 / 9);   // 16px at 36, as it always was
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 0, px, px), Qt::AlignCenter, letter);
        painter.end();

        icon.addPixmap(pixmap);
    }
    return icon;
}

// Same disc with an existing monochrome glyph on it. The glyph is drawn through
// QIcon::pixmap(), which is what keeps this off Qt6::Svg — that module is not
// linked, only Qt's svg icon plugin is present.
inline QIcon circleIcon(const QColor& color, const QIcon& glyph, int size = 36)
{
    QIcon icon;
    for (int px : { size, size * 2 }) {
        const int inner = px * 5 / 9;    // 20px at 36
        QPixmap pixmap = detail::circlePixmap(color, px);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRect((px - inner) / 2, (px - inner) / 2, inner, inner),
                           glyph.pixmap(QSize(inner, inner)));
        painter.end();

        icon.addPixmap(pixmap);
    }
    return icon;
}

inline QIcon icon(const QString& launcher)
{
    const QString path = assetPath(launcher);
    if (!path.isEmpty()) {
        return QIcon(path);
    }
    return circleIcon(accentColor(launcher), launcher.left(1).toUpper());
}

} // namespace StoreVisuals

#endif // STOREVISUALS_H
