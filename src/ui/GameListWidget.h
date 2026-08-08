#ifndef GAMELISTWIDGET_H
#define GAMELISTWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QFutureWatcher>
#include <QHash>
#include "core/Game.h"

class GameListWidget : public QWidget {
    Q_OBJECT

public:
    explicit GameListWidget(QWidget* parent = nullptr);

    void setGames(const QList<Game>& games);
    void addGame(const Game& game);
    void clear();

    qreal shimmerPhase() const { return m_shimmerPhase; }

    // Whether the per-game source badge is worth drawing. False while only one
    // launcher can answer, which is what keeps a Steam-only install looking
    // exactly as it did.
    bool showsSourceBadge() const { return m_showSourceBadge; }

signals:
    void gameSelected(const Game& game);
    void refreshRequested();
    void gameUpdateStatusChanged(const Game& game);

private slots:
    void onSearchTextChanged(const QString& text);
    void onSourceFilterChanged();
    void onItemClicked(QListWidgetItem* item);
    void onCurrentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onImageReady(const QString& url);
    void onImageFailed(const QString& url);
    void showContextMenu(const QPoint& pos);

private:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void updateFilter();
    bool matchesFilter(const Game& game) const;
    void refreshSourceFilter();
    QListWidgetItem* createGameItem(const Game& game);
    void finishImage(const QString& url, bool success);
    static bool itemStillLoading(const QListWidgetItem* item);
    void ensureShimmerRunning();
    void checkForUpdates();
    // Keyed by Game::settingsKey(), not by id(): two launchers can hand out the
    // same numeric id, and keying on it alone stamps one game's install state
    // onto the other's.
    void applyUpdateResults(const QHash<QString, Game>& changed);

    QLineEdit* m_searchBox;
    QComboBox* m_sourceFilter;
    QListWidget* m_listWidget;
    QList<Game> m_games;
    QString m_filterText;
    QString m_sourceFilterName;   // launcher name, empty = all
    bool m_showSourceBadge = false;

    QTimer* m_shimmerTimer;
    qreal m_shimmerPhase = 0.0;

    QTimer* m_updateCheckTimer;
    bool m_updateCheckRunning = false;
};

#endif // GAMELISTWIDGET_H
