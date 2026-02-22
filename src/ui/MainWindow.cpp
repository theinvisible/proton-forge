#include "MainWindow.h"
#include "launchers/LauncherManager.h"
#include "core/SettingsManager.h"
#include "utils/EnvBuilder.h"
#include "utils/ProtonManager.h"
#include "ui/ProtonVersionDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/AboutDialog.h"
#include "Version.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTimer>
#include <QDesktopServices>
#include <QDir>
#include <QSettings>
#include <QVBoxLayout>
#include <QLabel>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_gameRunner(new GameRunner(this))
{
    setupUI();
    setupMenuBar();
    setupToolBar();
    loadGames();

    setWindowTitle("ProtonForge - DLSS & Proton Manager");
    resize(1200, 800);

    // Connect game runner signals
    connect(m_gameRunner, &GameRunner::gameStarted, this, [this](const Game& game) {
        statusBar()->showMessage(QString("Started: %1").arg(game.name()), 5000);
        // Update UI to show game is running
        if (m_currentGame == game) {
            m_settingsWidget->setGameRunning(true);
        }
    });

    connect(m_gameRunner, &GameRunner::gameFinished, this, [this](const Game& game, int exitCode) {
        statusBar()->showMessage(QString("%1 exited with code %2").arg(game.name()).arg(exitCode), 5000);
        // Update UI to show game is no longer running
        if (m_currentGame == game) {
            m_settingsWidget->setGameRunning(false);
        }
    });

    connect(m_gameRunner, &GameRunner::launchError, this, [this](const Game& game, const QString& error) {
        // Update UI on error
        if (m_currentGame == game) {
            m_settingsWidget->setGameRunning(false);
        }
        QMessageBox::warning(this, "Launch Error",
            QString("Failed to launch %1:\n%2").arg(game.name(), error));
    });

    // Connect Proton manager signals
    connect(&ProtonManager::instance(), &ProtonManager::updateCheckComplete,
            this, &MainWindow::onProtonUpdateCheck);
    connect(&ProtonManager::instance(), &ProtonManager::geUpdateCheckComplete,
            this, &MainWindow::onProtonGEUpdateCheck);
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

    m_rightStack = new QStackedWidget(this);
    m_welcomeWidget = createWelcomeWidget();
    m_rightStack->addWidget(m_welcomeWidget);   // Index 0
    m_rightStack->addWidget(m_settingsWidget);  // Index 1
    m_rightStack->setCurrentIndex(0);

    m_splitter->addWidget(m_gameList);
    m_splitter->addWidget(m_rightStack);

    // Set initial sizes (game list: 400px, settings: remaining)
    m_splitter->setSizes({400, 800});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    setCentralWidget(m_splitter);

    // Connections
    connect(m_gameList, &GameListWidget::gameSelected, this, &MainWindow::onGameSelected);
    connect(m_gameList, &GameListWidget::refreshRequested, this, &MainWindow::refreshGameList);
    connect(m_settingsWidget, &DLSSSettingsWidget::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(m_settingsWidget, &DLSSSettingsWidget::playClicked, this, &MainWindow::onPlayClicked);
    connect(m_settingsWidget, &DLSSSettingsWidget::copyClicked, this, &MainWindow::onCopyToClipboard);
    connect(m_settingsWidget, &DLSSSettingsWidget::writeToSteamClicked, this, &MainWindow::onWriteToSteam);

    // Status bar
    statusBar()->showMessage("Ready");
}

QWidget* MainWindow::createWelcomeWidget()
{
    auto* widget = new QWidget(this);
    widget->setStyleSheet("background-color: #1a1a1a;");

    auto* layout = new QVBoxLayout(widget);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(24);

    // App title
    auto* titleLabel = new QLabel("ProtonForge");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "font-size: 36px; font-weight: bold; color: #e0e0e0; background: transparent;");

    // Tagline
    auto* taglineLabel = new QLabel("NVIDIA DLSS & Proton Manager for Linux");
    taglineLabel->setAlignment(Qt::AlignCenter);
    taglineLabel->setStyleSheet("font-size: 14px; color: #999; background: transparent;");

    // Stats card
    auto* statsCard = new QWidget();
    statsCard->setFixedWidth(320);
    statsCard->setStyleSheet(
        "background-color: #242424; border-radius: 12px; border-left: 3px solid #76B900;");
    auto* statsLayout = new QHBoxLayout(statsCard);
    statsLayout->setContentsMargins(20, 16, 20, 16);

    m_gameCountLabel = new QLabel("0");
    m_gameCountLabel->setStyleSheet(
        "font-size: 32px; font-weight: bold; color: #76B900; background: transparent;");

    auto* statsText = new QLabel("Games\nDiscovered");
    statsText->setStyleSheet("font-size: 13px; color: #aaa; background: transparent;");

    statsLayout->addWidget(m_gameCountLabel);
    statsLayout->addSpacing(12);
    statsLayout->addWidget(statsText);
    statsLayout->addStretch();

    // Hint
    auto* hintLabel = new QLabel("Select a game from the list to configure DLSS settings");
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setStyleSheet("font-size: 13px; color: #888; background: transparent;");

    // Feature list
    auto* featuresCard = new QWidget();
    featuresCard->setFixedWidth(320);
    featuresCard->setStyleSheet("background-color: #242424; border-radius: 12px;");
    auto* featuresLayout = new QVBoxLayout(featuresCard);
    featuresLayout->setContentsMargins(20, 16, 20, 16);
    featuresLayout->setSpacing(10);

    const QStringList features = {
        "DLSS Super Resolution",
        "DLSS Ray Reconstruction",
        "DLSS Frame Generation",
        "HDR Configuration",
        "Proton Version Management"
    };

    for (const auto& feature : features) {
        auto* featureLabel = new QLabel(
            QString("<span style='color: #76B900; font-size: 14px;'>&#x25CF;</span>"
                    "&nbsp;&nbsp;<span style='color: #ccc; font-size: 13px;'>%1</span>")
                .arg(feature));
        featureLabel->setStyleSheet("background: transparent;");
        featuresLayout->addWidget(featureLabel);
    }

    layout->addStretch();
    layout->addWidget(titleLabel);
    layout->addWidget(taglineLabel);
    layout->addSpacing(8);
    layout->addWidget(statsCard, 0, Qt::AlignCenter);
    layout->addSpacing(4);
    layout->addWidget(hintLabel);
    layout->addSpacing(4);
    layout->addWidget(featuresCard, 0, Qt::AlignCenter);
    layout->addStretch();

    return widget;
}

void MainWindow::setupMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu("&File");

    QAction* refreshAction = fileMenu->addAction(QIcon(":/icons/refresh.svg"), "&Refresh Games");
    refreshAction->setShortcut(QKeySequence::Refresh);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshGameList);

    fileMenu->addSeparator();

    QAction* settingsAction = fileMenu->addAction(QIcon(":/icons/settings.svg"), "&Settings...");
    settingsAction->setShortcut(QKeySequence::Preferences);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(QIcon(":/icons/exit.svg"), "&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    QMenu* toolsMenu = menuBar()->addMenu("&Tools");

    QAction* checkProtonAction = toolsMenu->addAction(QIcon(":/icons/update.svg"), "Check for Proton-CachyOS Updates");
    connect(checkProtonAction, &QAction::triggered, this, &MainWindow::checkProtonCachyOS);

    QAction* installProtonAction = toolsMenu->addAction(QIcon(":/icons/package.svg"), "Proton-Manager");
    connect(installProtonAction, &QAction::triggered, this, &MainWindow::installProtonCachyOS);

    toolsMenu->addSeparator();

    QAction* openProtonFolderAction = toolsMenu->addAction(QIcon(":/icons/folder-open.svg"), "Open Proton Folder...");
    connect(openProtonFolderAction, &QAction::triggered, this, []() {
        QString path = ProtonManager::protonCachyOSPath();
        QDir().mkpath(path);  // Ensure directory exists
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    QMenu* helpMenu = menuBar()->addMenu("&Help");

    // GPU Information menu item (only shown if NVIDIA GPU detected)
    if (GPUDetector::hasNvidiaGPU()) {
        QAction* gpuInfoAction = helpMenu->addAction(QIcon(":/icons/computer.svg"), "&System Information");
        connect(gpuInfoAction, &QAction::triggered, this, &MainWindow::showSystemInfo);
        helpMenu->addSeparator();
    }

    QAction* aboutAction = helpMenu->addAction(QIcon(":/icons/info.svg"), "&About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        AboutDialog dlg(this);
        dlg.exec();
    });
}

void MainWindow::setupToolBar()
{
}

void MainWindow::loadGames()
{
    QList<Game> games = LauncherManager::instance().discoverAllGames();
    m_gameList->setGames(games);
    m_gameCountLabel->setText(QString::number(games.count()));
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
    m_rightStack->setCurrentIndex(1);

    // Load settings for this game
    DLSSSettings settings = SettingsManager::instance().getSettings(game.settingsKey());
    m_settingsWidget->setGame(game);
    m_settingsWidget->setSettings(settings);

    // Update Play button state based on whether game is running
    m_settingsWidget->setGameRunning(m_gameRunner->isGameRunning(game));

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

    // Check if game is already running
    if (m_gameRunner->isGameRunning(m_currentGame)) {
        QMessageBox::information(this, "Game Already Running",
            QString("%1 is already running.").arg(m_currentGame.name()));
        return;
    }

    DLSSSettings settings = m_settingsWidget->settings();

    // Set the user-selected executable path on the game
    if (!settings.executablePath.isEmpty()) {
        m_currentGame.setExecutablePath(settings.executablePath);
    }

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
        // Check CachyOS for updates silently
        pm.checkForUpdates();
    }

    // Independently check Proton-GE if it is installed
    if (pm.isProtonGEInstalled()) {
        pm.checkForGEUpdates();
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

    statusBar()->showMessage("Fetching available Proton versions...");

    // Show a loading dialog while fetching from GitHub
    auto* loadingDialog = new QProgressDialog(this);
    loadingDialog->setWindowTitle("Proton-Manager");
    loadingDialog->setLabelText("Fetching available versions from GitHub...");
    loadingDialog->setRange(0, 0); // indeterminate
    loadingDialog->setMinimumDuration(0);
    loadingDialog->setCancelButton(nullptr);
    loadingDialog->setMinimumWidth(350);
    loadingDialog->setWindowModality(Qt::WindowModal);
    loadingDialog->show();

    // Fetch available versions and show selection dialog
    connect(&pm, &ProtonManager::availableVersionsFetched, this,
            [this, loadingDialog](const QList<ProtonManager::ProtonRelease>& releases) {
        // Disconnect to avoid multiple calls
        disconnect(&ProtonManager::instance(), &ProtonManager::availableVersionsFetched, this, nullptr);

        loadingDialog->close();
        loadingDialog->deleteLater();
        statusBar()->clearMessage();

        if (releases.isEmpty()) {
            QString detail = ProtonManager::instance().lastFetchError();
            bool isRateLimit = detail.contains("rate limit", Qt::CaseInsensitive);

            if (isRateLimit) {
                QMessageBox msgBox(this);
                msgBox.setWindowTitle("GitHub API Rate Limit Reached");
                msgBox.setIcon(QMessageBox::Warning);
                msgBox.setText("Could not fetch available Proton versions — the GitHub API rate limit has been reached.");
                msgBox.setInformativeText(
                    "Unauthenticated requests are limited to 60 per hour.\n\n"
                    "You can set a Personal Access Token in Settings to increase this limit to 5,000 requests/hour.");
                QPushButton* settingsBtn = msgBox.addButton("Open Settings...", QMessageBox::ActionRole);
                msgBox.addButton(QMessageBox::Ok);
                msgBox.exec();
                if (msgBox.clickedButton() == settingsBtn)
                    showSettings();
            } else {
                QString msg = "Could not fetch available Proton versions.";
                if (!detail.isEmpty())
                    msg += "\n\nAPI error: " + detail;
                msg += "\n\nPlease check your internet connection and try again.";
                QMessageBox::warning(this, "Error", msg);
            }
            return;
        }

        // Show version selection dialog – dialog handles installation internally
        QString currentVersion = ProtonManager::instance().getInstalledVersion();
        ProtonVersionDialog dialog(releases, currentVersion, this);

        m_dialogInstallActive = true;
        dialog.exec();
        m_dialogInstallActive = false;
    }, Qt::SingleShotConnection);

    pm.fetchAvailableVersions();
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
            // Update available - check if user has dismissed this version
            QSettings settings;
            QString dismissedVersion = settings.value("proton/dismissedUpdateVersion").toString();

            // Only show notification if this is a different (newer) version than the dismissed one
            if (dismissedVersion.isEmpty() || version != dismissedVersion) {
                QMessageBox msgBox(this);
                msgBox.setWindowTitle("Update Available");
                msgBox.setIcon(QMessageBox::Question);
                msgBox.setText(QString("A new version of Proton-CachyOS is available!\n\n"
                                      "Current version: %1\n"
                                      "New version: %2\n\n"
                                      "Would you like to update now?").arg(currentVersion, version));
                msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::Yes);

                // Add informative text
                msgBox.setInformativeText("Note: If you choose 'No', you won't be notified about this version again "
                                         "until a newer version is released.");

                int reply = msgBox.exec();

                if (reply == QMessageBox::Yes) {
                    pm.updateProtonCachyOS();
                    // Clear dismissed version since user is updating
                    settings.remove("proton/dismissedUpdateVersion");
                } else {
                    // User clicked No - save this version as dismissed
                    settings.setValue("proton/dismissedUpdateVersion", version);
                    statusBar()->showMessage(QString("Update to version %1 dismissed. "
                                                    "You will be notified when a newer version is available.").arg(version), 8000);
                }
            } else {
                // This version was already dismissed, don't show notification
                // Silently update status bar
                statusBar()->showMessage(QString("Proton-CachyOS update available (%1), previously dismissed. "
                                                "Check Tools menu to update.").arg(version), 3000);
            }
        }
    } else {
        if (!currentVersion.isEmpty()) {
            statusBar()->showMessage("Proton-CachyOS is up to date (" + currentVersion + ")", 3000);

            // Clear dismissed version if we're up to date
            QSettings settings;
            settings.remove("proton/dismissedUpdateVersion");
        }
    }
}

void MainWindow::onProtonGEUpdateCheck(bool updateAvailable, const QString& version)
{
    if (!updateAvailable)
        return;

    QString currentVersion = ProtonManager::instance().getInstalledGEVersion();

    QSettings settings;
    QString dismissedVersion = settings.value("proton/dismissedGEUpdateVersion").toString();

    if (!dismissedVersion.isEmpty() && version == dismissedVersion) {
        statusBar()->showMessage(
            QString("Proton-GE update available (%1), previously dismissed. Check Tools menu to update.").arg(version), 3000);
        return;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Proton-GE Update Available");
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(QString("A new version of Proton-GE is available!\n\n"
                           "Installed: %1\n"
                           "New version: %2\n\n"
                           "Would you like to open the Proton Manager to update?")
                       .arg(currentVersion, version));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    msgBox.setInformativeText("Note: If you choose 'No', you won't be notified about this version again "
                              "until a newer version is released.");

    if (msgBox.exec() == QMessageBox::Yes) {
        settings.remove("proton/dismissedGEUpdateVersion");
        installProtonCachyOS();
    } else {
        settings.setValue("proton/dismissedGEUpdateVersion", version);
        statusBar()->showMessage(
            QString("Proton-GE update to %1 dismissed.").arg(version), 5000);
    }
}

void MainWindow::onProtonInstallProgress(qint64 received, qint64 total, const QString& protonName)
{
    if (total > 0) {
        int percent = (received * 100) / total;
        double mb = received / (1024.0 * 1024.0);
        double totalMb = total / (1024.0 * 1024.0);
        statusBar()->showMessage(QString("Downloading %1: %2% (%3 / %4 MB)")
            .arg(protonName).arg(percent).arg(mb, 0, 'f', 1).arg(totalMb, 0, 'f', 1));
    }
}

void MainWindow::onProtonInstallComplete(bool success, const QString& message)
{
    // Only show a message box when installation was triggered outside the dialog
    // (e.g. auto-update from checkProtonOnStartup / onProtonUpdateCheck)
    if (!m_dialogInstallActive) {
        if (success) {
            QMessageBox::information(this, "Installation Complete",
                message + "\n\nProton is now available for use with your games.");
        } else {
            QMessageBox::warning(this, "Installation Failed", message);
        }
    }

    statusBar()->showMessage(success ? message : "Proton installation failed", 5000);

    if (success) {
        QSettings settings;
        settings.remove("proton/dismissedUpdateVersion");
        settings.remove("proton/dismissedGEUpdateVersion");
    }
}

void MainWindow::showSystemInfo()
{
    QList<GPUInfo> gpus = GPUDetector::detectAllGPUs();

    if (gpus.isEmpty()) {
        QMessageBox::information(this, "No GPUs Detected",
            "Could not detect any compatible GPUs.\n\n"
            "Supported vendors: NVIDIA");
        return;
    }

    SystemInfoDialog dialog(gpus, this);
    dialog.exec();
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
}
