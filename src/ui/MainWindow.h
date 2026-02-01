#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include "GameListWidget.h"
#include "DLSSSettingsWidget.h"
#include "core/Game.h"
#include "runner/GameRunner.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() = default;

private slots:
    void onGameSelected(const Game& game);
    void onSettingsChanged(const DLSSSettings& settings);
    void onPlayClicked();
    void onCopyToClipboard();
    void onWriteToSteam();
    void refreshGameList();
    void checkProtonCachyOS();
    void installProtonCachyOS();
    void onProtonUpdateCheck(bool updateAvailable, const QString& version);
    void onProtonInstallProgress(qint64 received, qint64 total);
    void onProtonInstallComplete(bool success, const QString& message);

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void loadGames();
    void checkProtonOnStartup();

    QSplitter* m_splitter;
    GameListWidget* m_gameList;
    DLSSSettingsWidget* m_settingsWidget;
    GameRunner* m_gameRunner;

    Game m_currentGame;
};

#endif // MAINWINDOW_H
