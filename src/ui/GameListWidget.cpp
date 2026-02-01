#include "GameListWidget.h"
#include "network/ImageCache.h"
#include <QLabel>

GameListWidget::GameListWidget(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    // Search box
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Search games...");
    m_searchBox->setClearButtonEnabled(true);
    layout->addWidget(m_searchBox);

    // Game list
    m_listWidget = new QListWidget(this);
    m_listWidget->setIconSize(QSize(184, 69));  // Scaled header image size
    m_listWidget->setSpacing(2);
    m_listWidget->setAlternatingRowColors(true);
    layout->addWidget(m_listWidget);

    // Connections
    connect(m_searchBox, &QLineEdit::textChanged, this, &GameListWidget::onSearchTextChanged);
    connect(m_listWidget, &QListWidget::itemClicked, this, &GameListWidget::onItemClicked);
    connect(&ImageCache::instance(), &ImageCache::imageReady, this, &GameListWidget::onImageReady);
}

void GameListWidget::setGames(const QList<Game>& games)
{
    m_games = games;
    updateFilter();
}

void GameListWidget::addGame(const Game& game)
{
    m_games.append(game);
    if (m_filterText.isEmpty() || game.name().contains(m_filterText, Qt::CaseInsensitive)) {
        m_listWidget->addItem(createGameItem(game));
    }
}

void GameListWidget::clear()
{
    m_games.clear();
    m_listWidget->clear();
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
    item->setText(game.name());
    item->setData(Qt::UserRole, QVariant::fromValue(game));

    // Set tooltip with game info
    QString tooltip = QString("%1\nApp ID: %2\nPath: %3")
        .arg(game.name())
        .arg(game.id())
        .arg(game.installPath());
    item->setToolTip(tooltip);

    // Load image
    updateItemImage(item, game);

    return item;
}

void GameListWidget::updateItemImage(QListWidgetItem* item, const Game& game)
{
    QSize iconSize(184, 69);
    QPixmap pixmap = ImageCache::instance().getImage(game.imageUrl(), iconSize);
    item->setIcon(QIcon(pixmap));
}

void GameListWidget::onItemClicked(QListWidgetItem* item)
{
    if (!item) return;

    Game game = item->data(Qt::UserRole).value<Game>();
    emit gameSelected(game);
}

void GameListWidget::onImageReady(const QString& url)
{
    // Update all items that were waiting for this image
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        Game game = item->data(Qt::UserRole).value<Game>();
        if (game.imageUrl() == url) {
            updateItemImage(item, game);
        }
    }
}
