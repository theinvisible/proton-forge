#ifndef GAMELISTWIDGET_H
#define GAMELISTWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include "core/Game.h"

class GameListWidget : public QWidget {
    Q_OBJECT

public:
    explicit GameListWidget(QWidget* parent = nullptr);

    void setGames(const QList<Game>& games);
    void addGame(const Game& game);
    void clear();

signals:
    void gameSelected(const Game& game);

private slots:
    void onSearchTextChanged(const QString& text);
    void onItemClicked(QListWidgetItem* item);
    void onImageReady(const QString& url);
    void showContextMenu(const QPoint& pos);

private:
    void updateFilter();
    QListWidgetItem* createGameItem(const Game& game);
    void updateItemImage(QListWidgetItem* item, const Game& game);

    QLineEdit* m_searchBox;
    QListWidget* m_listWidget;
    QList<Game> m_games;
    QString m_filterText;
};

#endif // GAMELISTWIDGET_H
