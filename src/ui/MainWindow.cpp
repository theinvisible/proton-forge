#include "MainWindow.h"
#include "launchers/LauncherManager.h"
#include "core/SettingsManager.h"
#include "utils/EnvBuilder.h"
#include "utils/ProtonManager.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTimer>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_gameRunner(new GameRunner(this))
{
    setupUI();
    setupMenuBar();
    setupToolBar();
    loadGames();

    setWindowTitle("NVIDIA App Linux - DLSS Manager");
    resize(1200, 800);

    // Connect game runner signals
    connect(m_gameRunner, &GameRunner::gameStarted, this, [this](const Game& game) {
        statusBar()->showMessage(QString("Started: %1").arg(game.name()), 5000);
    });

    connect(m_gameRunner, &GameRunner::gameFinished, this, [this](const Game& game, int exitCode) {
        statusBar()->showMessage(QString("%1 exited with code %2").arg(game.name()).arg(exitCode), 5000);
    });

    connect(m_gameRunner, &GameRunner::launchError, this, [this](const Game& game, const QString& error) {
        QMessageBox::warning(this, "Launch Error",
            QString("Failed to launch %1:\n%2").arg(game.name(), error));
    });

    // Connect Proton manager signals
    connect(&ProtonManager::instance(), &ProtonManager::updateCheckComplete,
            this, &MainWindow::onProtonUpdateCheck);
    connect(&ProtonManager::instance(), &ProtonManager::downloadProgress,
            this, &MainWindow::onProtonInstallProgress);
    connect(&ProtonManager::instance(), &ProtonManager::installationComplete,
            this, &MainWindow::onProtonInstallComplete);

    // Check for Proton-CachyOS on startup
    QTimer::singleShot(1000, this, &MainWindow::checkProtonOnStartup);
}

void MainWindow::setupUI()
{
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_gameList = new GameListWidget(this);
    m_settingsWidget = new DLSSSettingsWidget(this);

    m_splitter->addWidget(m_gameList);
    m_splitter->addWidget(m_settingsWidget);

    // Set initial sizes (game list: 400px, settings: remaining)
    m_splitter->setSizes({400, 800});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    setCentralWidget(m_splitter);

    // Connections
    connect(m_gameList, &GameListWidget::gameSelected, this, &MainWindow::onGameSelected);
    connect(m_settingsWidget, &DLSSSettingsWidget::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(m_settingsWidget, &DLSSSettingsWidget::playClicked, this, &MainWindow::onPlayClicked);
    connect(m_settingsWidget, &DLSSSettingsWidget::copyClicked, this, &MainWindow::onCopyToClipboard);
    connect(m_settingsWidget, &DLSSSettingsWidget::writeToSteamClicked, this, &MainWindow::onWriteToSteam);

    // Status bar
    statusBar()->showMessage("Ready");
}

void MainWindow::setupMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu("&File");

    QAction* refreshAction = fileMenu->addAction("&Refresh Games");
    refreshAction->setShortcut(QKeySequence::Refresh);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshGameList);

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    QMenu* toolsMenu = menuBar()->addMenu("&Tools");

    QAction* checkProtonAction = toolsMenu->addAction("Check for Proton-CachyOS Updates");
    connect(checkProtonAction, &QAction::triggered, this, &MainWindow::checkProtonCachyOS);

    QAction* installProtonAction = toolsMenu->addAction("Install/Update Proton-CachyOS");
    connect(installProtonAction, &QAction::triggered, this, &MainWindow::installProtonCachyOS);

    QMenu* helpMenu = menuBar()->addMenu("&Help");

    QAction* aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About NVIDIA App Linux",
            "NVIDIA App Linux - DLSS Manager\n\n"
            "Manage DLSS settings for Steam games running under Proton.\n\n"
            "Version 1.0.0");
    });
}

void MainWindow::setupToolBar()
{
    QToolBar* toolBar = addToolBar("Main");
    toolBar->setMovable(false);

    QAction* refreshAction = toolBar->addAction("Refresh");
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshGameList);
}

void MainWindow::loadGames()
{
    QList<Game> games = LauncherManager::instance().discoverAllGames();
    m_gameList->setGames(games);
    statusBar()->showMessage(QString("Found %1 games").arg(games.count()), 3000);
}

void MainWindow::refreshGameList()
{
    statusBar()->showMessage("Refreshing game list...");
    loadGames();
}

void MainWindow::onGameSelected(const Game& game)
{
    m_currentGame = game;

    // Load settings for this game
    DLSSSettings settings = SettingsManager::instance().getSettings(game.settingsKey());
    m_settingsWidget->setGame(game);
    m_settingsWidget->setSettings(settings);

    statusBar()->showMessage(QString("Selected: %1").arg(game.name()), 3000);
}

void MainWindow::onSettingsChanged(const DLSSSettings& settings)
{
    if (m_currentGame.id().isEmpty()) {
        return;
    }

    SettingsManager::instance().setSettings(m_currentGame.settingsKey(), settings);
    m_settingsWidget->updateLaunchCommand(EnvBuilder::buildLaunchOptions(settings));
}

void MainWindow::onPlayClicked()
{
    if (m_currentGame.id().isEmpty()) {
        QMessageBox::information(this, "No Game Selected", "Please select a game first.");
        return;
    }

    DLSSSettings settings = m_settingsWidget->settings();
    statusBar()->showMessage(QString("Launching %1...").arg(m_currentGame.name()));
    m_gameRunner->launch(m_currentGame, settings);
}

void MainWindow::onCopyToClipboard()
{
    if (m_currentGame.id().isEmpty()) {
        return;
    }

    DLSSSettings settings = m_settingsWidget->settings();
    QString launchCommand = EnvBuilder::buildLaunchOptions(settings);

    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(launchCommand);

    statusBar()->showMessage("Launch options copied to clipboard", 3000);
}

void MainWindow::onWriteToSteam()
{
    if (m_currentGame.id().isEmpty()) {
        return;
    }

    auto launcher = LauncherManager::instance().launcher(m_currentGame.launcher());
    if (!launcher) {
        QMessageBox::warning(this, "Error", "Launcher not found");
        return;
    }

    DLSSSettings settings = m_settingsWidget->settings();
    bool success = launcher->applySettings(m_currentGame, settings);

    if (success) {
        QMessageBox::information(this, "Settings Applied",
            "Launch options have been written to Steam's localconfig.vdf.\n\n"
            "Please restart Steam for the changes to take effect.");
    } else {
        QMessageBox::warning(this, "Error",
            "Failed to write settings to Steam configuration.\n"
            "You may need to copy the launch options manually.");
    }
}

void MainWindow::checkProtonOnStartup()
{
    ProtonManager& pm = ProtonManager::instance();

    if (!pm.isProtonCachyOSInstalled()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "Proton-CachyOS Not Found",
            "Proton-CachyOS is not installed. This is a high-performance Proton build optimized for gaming.\n\n"
            "Would you like to download and install it now?",
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            installProtonCachyOS();
        }
    } else {
        // Check for updates silently
        pm.checkForUpdates();
    }
}

void MainWindow::checkProtonCachyOS()
{
    statusBar()->showMessage("Checking for Proton-CachyOS updates...");
    ProtonManager::instance().checkForUpdates();
}

void MainWindow::installProtonCachyOS()
{
    ProtonManager& pm = ProtonManager::instance();

    QString currentVersion = pm.getInstalledVersion();
    QString message;

    if (currentVersion.isEmpty()) {
        message = "This will download and install Proton-CachyOS.\n\n"
                  "Proton-CachyOS is a high-performance Proton build with optimizations "
                  "for better gaming performance and compatibility.\n\n"
                  "Continue?";
    } else {
        message = QString("Current version: %1\n\n"
                         "This will check for and install the latest version of Proton-CachyOS.\n\n"
                         "Continue?").arg(currentVersion);
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Install Proton-CachyOS", message, QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        statusBar()->showMessage("Starting Proton-CachyOS installation...");
        pm.installProtonCachyOS();
    }
}

void MainWindow::onProtonUpdateCheck(bool updateAvailable, const QString& version)
{
    ProtonManager& pm = ProtonManager::instance();
    QString currentVersion = pm.getInstalledVersion();

    if (updateAvailable) {
        if (currentVersion.isEmpty()) {
            // Not installed
            statusBar()->showMessage("Proton-CachyOS not installed. Use Tools menu to install.", 5000);
        } else {
            // Update available
            QMessageBox::StandardButton reply = QMessageBox::question(this,
                "Update Available",
                QString("A new version of Proton-CachyOS is available!\n\n"
                       "Current version: %1\n"
                       "New version: %2\n\n"
                       "Would you like to update now?").arg(currentVersion, version),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                pm.updateProtonCachyOS();
            }
        }
    } else {
        if (!currentVersion.isEmpty()) {
            statusBar()->showMessage("Proton-CachyOS is up to date (" + currentVersion + ")", 3000);
        }
    }
}

void MainWindow::onProtonInstallProgress(qint64 received, qint64 total)
{
    if (total > 0) {
        int percent = (received * 100) / total;
        double mb = received / (1024.0 * 1024.0);
        double totalMb = total / (1024.0 * 1024.0);
        statusBar()->showMessage(QString("Downloading Proton-CachyOS: %1% (%2 / %3 MB)")
            .arg(percent).arg(mb, 0, 'f', 1).arg(totalMb, 0, 'f', 1));
    }
}

void MainWindow::onProtonInstallComplete(bool success, const QString& message)
{
    if (success) {
        QMessageBox::information(this, "Installation Complete",
            message + "\n\nProton-CachyOS is now available for use with your games.");
        statusBar()->showMessage("Proton-CachyOS installed successfully", 5000);
    } else {
        QMessageBox::warning(this, "Installation Failed", message);
        statusBar()->showMessage("Proton-CachyOS installation failed", 5000);
    }
}
