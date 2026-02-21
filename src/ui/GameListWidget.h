#ifndef GAMELISTWIDGET_H
#define GAMELISTWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include "core/Game.h"

class GameListWidget : public QWidget {
    Q_OBJECT

public:
    explicit GameListWidget(QWidget* parent = nullptr);

    void setGames(const QList<Game>& games);
    void addGame(const Game& game);
    void clear();

    qreal shimmerPhase() const { return m_shimmerPhase; }

signals:
    void gameSelected(const Game& game);
    void refreshRequested();

private slots:
    void onSearchTextChanged(const QString& text);
    void onItemClicked(QListWidgetItem* item);
    void onCurrentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onImageReady(const QString& url);
    void showContextMenu(const QPoint& pos);

private:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void updateFilter();
    QListWidgetItem* createGameItem(const Game& game);
    void ensureShimmerRunning();

    QLineEdit* m_searchBox;
    QListWidget* m_listWidget;
    QList<Game> m_games;
    QString m_filterText;

    QTimer* m_shimmerTimer;
    qreal m_shimmerPhase = 0.0;
};

#endif // GAMELISTWIDGET_H
