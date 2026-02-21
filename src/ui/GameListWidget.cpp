#include "GameListWidget.h"
#include "network/ImageCache.h"
#include <QLabel>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QAction>
#include <QHBoxLayout>
#include <QEvent>

// ---------------------------------------------------------------------------
// Custom roles stored on each game list item
// ---------------------------------------------------------------------------
static constexpr int RoleGame        = Qt::UserRole;
static constexpr int RoleImageLoaded = Qt::UserRole + 1;
static constexpr int RoleIsNative    = Qt::UserRole + 2;
static constexpr int RoleGameName    = Qt::UserRole + 3;
static constexpr int RoleImageUrl    = Qt::UserRole + 4;

// ---------------------------------------------------------------------------
// GameItemDelegate – paints each game as a modern card with artwork
// ---------------------------------------------------------------------------
class GameItemDelegate : public QStyledItemDelegate {
public:
    explicit GameItemDelegate(GameListWidget* parent)
        : QStyledItemDelegate(parent), m_owner(parent) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(0, 100);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        const QRect r        = option.rect.adjusted(4, 1, -4, -1);
        const bool  selected = option.state & QStyle::State_Selected;
        const bool  hovered  = option.state & QStyle::State_MouseOver;

        const bool    imageLoaded = index.data(RoleImageLoaded).toBool();
        const bool    isNative    = index.data(RoleIsNative).toBool();
        const QString gameName    = index.data(RoleGameName).toString();
        const QString imageUrl    = index.data(RoleImageUrl).toString();

        // --- Card background ---
        const QColor bg = selected ? QColor("#1a3a0a")
                        : hovered  ? QColor("#2e2e2e")
                                   : QColor("#242424");
        const QColor border = selected ? QColor("#76B900")
                            : hovered  ? QColor("#4a4a4a")
                                       : QColor("#3a3a3a");
        p->setPen(QPen(border, selected ? 1.5 : 1.0));
        p->setBrush(bg);
        p->drawRoundedRect(r, 8, 8);

        // --- Artwork area ---
        const int artW = 120;
        const int artH = 68;
        const int artX = r.left() + 12;
        const int artY = r.top() + (r.height() - artH) / 2;
        const QRect artRect(artX, artY, artW, artH);

        // Shadow behind artwork
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(QRectF(artRect).adjusted(2, 2, 2, 2), 6, 6);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 60));
        p->drawPath(shadowPath);

        // Clip artwork to rounded rect
        QPainterPath clipPath;
        clipPath.addRoundedRect(QRectF(artRect), 6, 6);

        if (imageLoaded && !imageUrl.isEmpty()) {
            QPixmap pixmap = ImageCache::instance().getImage(imageUrl, QSize(artW, artH));
            if (!pixmap.isNull()) {
                p->setClipPath(clipPath);
                p->drawPixmap(artRect, pixmap);
                p->setClipping(false);
            } else {
                drawPlaceholder(p, clipPath, artRect);
            }
        } else if (!imageUrl.isEmpty()) {
            // Image still loading — draw shimmer
            drawShimmer(p, clipPath, artRect);
        } else {
            drawPlaceholder(p, clipPath, artRect);
        }

        // --- Text area ---
        const int textLeft = artX + artW + 14;
        const int textRight = r.right() - 12;
        const int textW = qMax(0, textRight - textLeft);

        // Game name
        QFont nameFont = option.font;
        nameFont.setBold(true);
        nameFont.setPixelSize(13);
        QFontMetrics nfm(nameFont);

        p->setFont(nameFont);
        p->setPen(selected ? QColor("#ffffff") : QColor("#e0e0e0"));

        const int nameY = r.top() + (r.height() / 2) - nfm.height() - 2;
        p->drawText(QRect(textLeft, nameY, textW, nfm.height()),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    nfm.elidedText(gameName, Qt::ElideRight, textW));

        // --- Platform badge ---
        QFont badgeFont = option.font;
        badgeFont.setPixelSize(9);
        badgeFont.setBold(true);
        QFontMetrics bfm(badgeFont);

        const QString badgeText = isNative ? "LINUX" : "WINDOWS";
        const QColor badgeColor = isNative ? QColor("#e8710a") : QColor("#1565c0");
        const int badgeH = 16;
        const int badgePad = 6;
        const int badgeW = bfm.horizontalAdvance(badgeText) + badgePad * 2;
        const int badgeY = nameY + nfm.height() + 6;
        const QRect badgeRect(textLeft, badgeY, badgeW, badgeH);

        p->setPen(Qt::NoPen);
        p->setBrush(badgeColor);
        p->drawRoundedRect(badgeRect, 3, 3);

        p->setFont(badgeFont);
        p->setPen(Qt::white);
        p->drawText(badgeRect, Qt::AlignCenter, badgeText);

        p->restore();
    }

private:
    GameListWidget* m_owner;

    void drawPlaceholder(QPainter* p, const QPainterPath& clip, const QRect& rect) const {
        p->setClipPath(clip);
        p->setBrush(QColor("#1a1a1a"));
        p->setPen(Qt::NoPen);
        p->drawRect(rect);

        // Draw a gamepad icon hint
        p->setPen(QPen(QColor("#444444"), 1.5));
        int cx = rect.center().x();
        int cy = rect.center().y();
        p->drawEllipse(QPoint(cx, cy), 10, 10);
        p->drawLine(cx - 5, cy, cx + 5, cy);
        p->drawLine(cx, cy - 5, cx, cy + 5);

        p->setClipping(false);
    }

    void drawShimmer(QPainter* p, const QPainterPath& clip, const QRect& rect) const {
        p->setClipPath(clip);
        p->setBrush(QColor("#1a1a1a"));
        p->setPen(Qt::NoPen);
        p->drawRect(rect);

        // Animated shimmer gradient
        qreal phase = m_owner->shimmerPhase();
        qreal sweepCenter = rect.left() + (rect.width() + 80) * phase - 40;

        QLinearGradient grad(sweepCenter - 40, 0, sweepCenter + 40, 0);
        grad.setColorAt(0.0, QColor(255, 255, 255, 0));
        grad.setColorAt(0.5, QColor(255, 255, 255, 25));
        grad.setColorAt(1.0, QColor(255, 255, 255, 0));

        p->setBrush(grad);
        p->drawRect(rect);
        p->setClipping(false);
    }
};

// ---------------------------------------------------------------------------
// GameListWidget
// ---------------------------------------------------------------------------
GameListWidget::GameListWidget(QWidget* parent)
    : QWidget(parent)
    , m_shimmerPhase(0.0)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    // Search row: search box + refresh button
    QHBoxLayout* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(6, 6, 6, 2);
    searchRow->setSpacing(4);

    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Search games...");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setTextMargins(28, 0, 0, 0);
    m_searchBox->setStyleSheet(
        "QLineEdit {"
        "  background-color: #1e1e1e;"
        "  border: 1px solid #3a3a3a;"
        "  border-radius: 6px;"
        "  padding: 8px 10px;"
        "  color: #e0e0e0;"
        "  font-size: 13px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid #76B900;"
        "}"
        "QLineEdit::placeholder {"
        "  color: #666666;"
        "}"
    );

    // Paint magnifying glass icon directly on the search box
    m_searchBox->installEventFilter(this);

    auto* refreshButton = new QPushButton(this);
    refreshButton->setFixedSize(36, 36);
    refreshButton->setToolTip("Refresh game list");
    refreshButton->setText("\xe2\x9f\xb3");  // Unicode ⟳
    refreshButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #2a2a2a;"
        "  border: 1px solid #3a3a3a;"
        "  border-radius: 6px;"
        "  color: #ccc;"
        "  font-size: 18px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #383838;"
        "  border-color: #76B900;"
        "  color: #76B900;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #1e1e1e;"
        "}"
    );
    connect(refreshButton, &QPushButton::clicked, this, &GameListWidget::refreshRequested);

    searchRow->addWidget(m_searchBox, 1);
    searchRow->addWidget(refreshButton);

    layout->addLayout(searchRow);

    // Game list
    m_listWidget = new QListWidget(this);
    m_listWidget->setIconSize(QSize(0, 0));
    m_listWidget->setSpacing(0);
    m_listWidget->setAlternatingRowColors(false);
    m_listWidget->setMouseTracking(true);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listWidget->verticalScrollBar()->setSingleStep(20);

    // Set custom delegate
    m_listWidget->setItemDelegate(new GameItemDelegate(this));

    // Transparent background so delegate draws everything
    m_listWidget->setStyleSheet(
        "QListWidget {"
        "  background-color: transparent;"
        "  border: none;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QListWidget::item:selected {"
        "  background: transparent;"
        "}"
        "QListWidget::item:hover {"
        "  background: transparent;"
        "}"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 8px;"
        "  margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #555;"
        "  border-radius: 4px;"
        "  min-height: 30px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #6a6a6a;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "}"
    );

    layout->addWidget(m_listWidget);

    // Shimmer timer
    m_shimmerTimer = new QTimer(this);
    m_shimmerTimer->setInterval(30);
    connect(m_shimmerTimer, &QTimer::timeout, this, [this]() {
        m_shimmerPhase += 0.02;
        if (m_shimmerPhase > 1.0)
            m_shimmerPhase = 0.0;
        m_listWidget->viewport()->update();
    });

    // Connections
    connect(m_searchBox, &QLineEdit::textChanged, this, &GameListWidget::onSearchTextChanged);
    connect(m_listWidget, &QListWidget::itemClicked, this, &GameListWidget::onItemClicked);
    connect(m_listWidget, &QListWidget::currentItemChanged, this, &GameListWidget::onCurrentItemChanged);
    connect(&ImageCache::instance(), &ImageCache::imageReady, this, &GameListWidget::onImageReady);
    connect(m_listWidget, &QListWidget::customContextMenuRequested, this, &GameListWidget::showContextMenu);
}

bool GameListWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_searchBox && event->type() == QEvent::Paint) {
        // Let the line edit paint itself first
        m_searchBox->removeEventFilter(this);
        m_searchBox->event(event);
        m_searchBox->installEventFilter(this);

        // Now draw the magnifying glass on top
        QPainter p(m_searchBox);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal totalH = 13.0; // circle 9 + handle ~4
        const int iconX = 18;
        const int iconY = qRound((m_searchBox->height() - totalH) / 2.0);

        // Glass circle
        p.setPen(QPen(QColor("#999999"), 1.6, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(iconX, iconY, 9, 9);

        // Handle
        p.drawLine(QPointF(iconX + 8.5, iconY + 8.5),
                   QPointF(iconX + 12.5, iconY + 12.5));

        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void GameListWidget::setGames(const QList<Game>& games)
{
    m_games = games;
    m_shimmerPhase = 0.0;
    updateFilter();
    ensureShimmerRunning();
}

void GameListWidget::addGame(const Game& game)
{
    m_games.append(game);
    if (m_filterText.isEmpty() || game.name().contains(m_filterText, Qt::CaseInsensitive)) {
        m_listWidget->addItem(createGameItem(game));
        ensureShimmerRunning();
    }
}

void GameListWidget::clear()
{
    m_games.clear();
    m_listWidget->clear();
    m_shimmerTimer->stop();
}

void GameListWidget::onSearchTextChanged(const QString& text)
{
    m_filterText = text;
    updateFilter();
}

void GameListWidget::updateFilter()
{
    m_listWidget->clear();

    for (const Game& game : m_games) {
        if (m_filterText.isEmpty() || game.name().contains(m_filterText, Qt::CaseInsensitive)) {
            m_listWidget->addItem(createGameItem(game));
        }
    }
}

QListWidgetItem* GameListWidget::createGameItem(const Game& game)
{
    QListWidgetItem* item = new QListWidgetItem();
    item->setSizeHint(QSize(0, 100));
    item->setData(RoleGame, QVariant::fromValue(game));
    item->setData(RoleGameName, game.name());
    item->setData(RoleIsNative, game.isNativeLinux());
    item->setData(RoleImageUrl, game.imageUrl());

    // Check if image is already cached
    bool cached = ImageCache::instance().hasImage(game.imageUrl());
    item->setData(RoleImageLoaded, cached);

    if (!cached && !game.imageUrl().isEmpty()) {
        // Trigger fetch (getImage will start download if not cached)
        ImageCache::instance().getImage(game.imageUrl(), QSize(120, 68));
    }

    // Set tooltip with game info
    QString tooltip = QString("%1\nApp ID: %2\nPath: %3")
        .arg(game.name())
        .arg(game.id())
        .arg(game.installPath());
    item->setToolTip(tooltip);

    return item;
}

void GameListWidget::onItemClicked(QListWidgetItem* item)
{
    if (!item) return;

    Game game = item->data(RoleGame).value<Game>();
    emit gameSelected(game);
}

void GameListWidget::onCurrentItemChanged(QListWidgetItem* current, QListWidgetItem* previous)
{
    Q_UNUSED(previous);
    if (!current) return;

    Game game = current->data(RoleGame).value<Game>();
    emit gameSelected(game);
}

void GameListWidget::onImageReady(const QString& url)
{
    bool allLoaded = true;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        if (item->data(RoleImageUrl).toString() == url) {
            item->setData(RoleImageLoaded, true);
        }
        if (!item->data(RoleImageLoaded).toBool() && !item->data(RoleImageUrl).toString().isEmpty()) {
            allLoaded = false;
        }
    }

    // Stop shimmer when all visible images are loaded
    if (allLoaded) {
        m_shimmerTimer->stop();
    }

    m_listWidget->viewport()->update();
}

void GameListWidget::ensureShimmerRunning()
{
    // Check if any visible items still need images
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        if (!item->data(RoleImageLoaded).toBool() && !item->data(RoleImageUrl).toString().isEmpty()) {
            if (!m_shimmerTimer->isActive()) {
                m_shimmerTimer->start();
            }
            return;
        }
    }
}

void GameListWidget::showContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_listWidget->itemAt(pos);
    if (!item) return;

    Game game = item->data(RoleGame).value<Game>();

    QMenu menu(this);

    // Open install location action
    QAction* openLocationAction = menu.addAction("Open Install Location");
    openLocationAction->setEnabled(QDir(game.installPath()).exists());

    // Open compatdata location action (for Steam games)
    QAction* openCompatDataAction = nullptr;
    if (game.launcher() == "Steam") {
        QString compatDataPath = game.libraryPath() + "/compatdata/" + game.id();
        openCompatDataAction = menu.addAction("Open Proton Prefix");
        openCompatDataAction->setEnabled(QDir(compatDataPath).exists());
    }

    QAction* selectedAction = menu.exec(m_listWidget->mapToGlobal(pos));

    if (selectedAction == openLocationAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(game.installPath()));
    } else if (selectedAction == openCompatDataAction && openCompatDataAction) {
        QString compatDataPath = game.libraryPath() + "/compatdata/" + game.id();
        QDesktopServices::openUrl(QUrl::fromLocalFile(compatDataPath));
    }
}
