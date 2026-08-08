#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QStackedWidget>
#include <QLabel>
#include "GameListWidget.h"
#include "DLSSSettingsWidget.h"
#include "SystemInfoDialog.h"
#include "core/Game.h"
#include "runner/GameRunner.h"
#include "utils/GPUDetector.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() = default;

protected:
    // Quitting mid-download is allowed, but not silently: the journal makes it
    // resumable and the user should know that before deciding.
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onGameSelected(const Game& game);
    void onSettingsChanged(const DLSSSettings& settings);
    void onPlayClicked();
    void onCopyToClipboard();
    void onWriteToSteam();
    void onImportFromSteam();
    void refreshGameList();
    void checkProtonCachyOS();
    void installProtonCachyOS();
    void onProtonUpdateCheck(bool updateAvailable, const QString& version);
    void onProtonGEUpdateCheck(bool updateAvailable, const QString& version);
    void onProtonInstallProgress(qint64 received, qint64 total, const QString& protonName);
    void onProtonInstallComplete(bool success, const QString& message);
    void onGitHubTokenRejected();
    void showSystemInfo();
    void showSettings();
    void showStoreLibrary();

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void loadGames();
    void checkProtonOnStartup();
    QWidget* createWelcomeWidget();

    QSplitter* m_splitter;
    GameListWidget* m_gameList;
    DLSSSettingsWidget* m_settingsWidget;
    QStackedWidget* m_rightStack;
    QWidget* m_welcomeWidget;
    QLabel* m_gameCountLabel;
    GameRunner* m_gameRunner;

    Game m_currentGame;
    bool m_dialogInstallActive = false;
    bool m_authWarningShown = false;  // show the expired-token warning at most once per session
    bool m_gamesLoadedThisRefresh = false;  // see refreshGameList()
};

#endif // MAINWINDOW_H
