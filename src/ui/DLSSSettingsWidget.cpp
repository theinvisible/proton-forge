#include "DLSSSettingsWidget.h"
#include "ui/settings/AdvancedPage.h"
#include "ui/settings/DlssPage.h"
#include "ui/settings/GeneralPage.h"
#include "ui/settings/HdrPage.h"
#include "ui/settings/ProtonPage.h"
#include "RecommendationsDialog.h"
#include "AppStyle.h"
#include "network/ImageCache.h"
#include "network/ProtonDBClient.h"
#include "utils/EnvBuilder.h"
#include "utils/ProtonManager.h"
#include "utils/CompatToolResolver.h"
#include "utils/ExecutableFinder.h"
#include "utils/LaunchOptionExtractor.h"
#include "utils/FeatureWarnings.h"
#include "utils/GpuInfoCache.h"
#include "runner/GameRunner.h"
#include "launchers/SteamLauncher.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include <QAbstractItemView>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QtConcurrent>
#include <algorithm>

namespace {
// Muted style for the header ProtonDB badge (checking / unknown / n-a states).
QString protonDbBadgeMutedStyle()
{
    return QStringLiteral(
        "QPushButton { font-size: 11px; font-weight: bold; padding: 4px 8px; "
        "border-radius: 4px; background-color: %1; color: white; border: none; }")
        .arg(AppStyle::ColorBorderLight);
}

// Tier-coloured style for the header ProtonDB badge.
QString protonDbBadgeTierStyle(const QString& color)
{
    return QStringLiteral(
        "QPushButton { font-size: 11px; font-weight: bold; padding: 4px 8px; "
        "border-radius: 4px; background-color: %1; color: #1a1a1a; border: none; }")
        .arg(color);
}

} // namespace

DLSSSettingsWidget::DLSSSettingsWidget(QWidget* parent)
    : QWidget(parent)
    , m_executableWatcher(new QFutureWatcher<QStringList>(this))
{
    setupUI();

    // Connect executable watcher to update UI when search completes
    connect(m_executableWatcher, &QFutureWatcher<QStringList>::finished,
            this, [this]() {
        // Ignore results from a cancelled/outdated search
        if (m_cancelFlag && m_cancelFlag->load()) {
            return;
        }
        QStringList executables = m_executableWatcher->result();
        updateExecutableSelectorWithResults(executables);
    });

    // Artwork arrives after the panel is drawn, and only for whatever is
    // selected *now* — hence one connection here, keyed on m_currentGame,
    // rather than one per setGame().
    connect(&ImageCache::instance(), &ImageCache::imageReady, this, [this](const QString& url) {
        if (url.isEmpty() || url != m_currentGame.imageUrl()) {
            return;
        }
        m_gameImageLabel->setPixmap(ImageCache::instance().getImage(url, QSize(230, 107)));
    });

    // Re-evaluate feature warnings once the NVIDIA driver version is detected
    // (detection runs asynchronously after startup).
    connect(&GpuInfoCache::instance(), &GpuInfoCache::updated,
            this, &DLSSSettingsWidget::updateFeatureWarnings);

    // ProtonDB tier badge — update when a summary arrives for the current game.
    connect(&ProtonDBClient::instance(), &ProtonDBClient::summaryReady, this,
            [this](const QString& appId, const ProtonDBClient::Summary& summary) {
        if (appId != m_currentGame.id()) {
            return;
        }
        m_protonDbSummary = summary;
        m_protonDbSummaryAppId = appId;
        updateProtonDbBadge(summary);
    });
    connect(&ProtonDBClient::instance(), &ProtonDBClient::summaryFailed, this,
            [this](const QString& appId) {
        if (appId != m_currentGame.id()) {
            return;
        }
        m_protonDbBadge->setText("ProtonDB: unavailable");
        m_protonDbBadge->setStyleSheet(protonDbBadgeMutedStyle());
    });

    // ProtonDB report mining → recommendations dialog. Both outcomes restore the
    // badge and (for the current game) open a dialog; on success it lists mined
    // suggestions, otherwise it explains why and still shows the tier + link.
    auto restoreBadge = [this]() {
        m_protonDbBadge->setEnabled(m_currentGame.traits().idIsSteamAppId && !m_currentGame.id().isEmpty());
        updateProtonDbBadge(m_protonDbSummary);
    };
    connect(&ProtonDBClient::instance(), &ProtonDBClient::reportsReady, this,
            [this, restoreBadge](const QString& appId, const QList<ProtonDBClient::Report>& reports) {
        if (appId != m_currentGame.id()) {
            return;
        }
        restoreBadge();
        const auto suggestions = LaunchOptionExtractor::extract(reports);
        const QString message = suggestions.isEmpty()
            ? QString("No launch-option tips could be mined from the available reports.")
            : QString();
        RecommendationsDialog dialog(appId, m_currentGame.name(), m_protonDbSummary,
                                     suggestions, message, this);
        connect(&dialog, &RecommendationsDialog::applySnippet,
                m_advancedPage, &AdvancedPage::appendCustomParams);
        dialog.exec();
    });
    connect(&ProtonDBClient::instance(), &ProtonDBClient::reportsUnavailable, this,
            [this, restoreBadge](const QString& appId, const QString& reason) {
        if (appId != m_currentGame.id()) {
            return;
        }
        restoreBadge();
        RecommendationsDialog dialog(appId, m_currentGame.name(), m_protonDbSummary,
                                     {}, reason, this);
        connect(&dialog, &RecommendationsDialog::applySnippet,
                m_advancedPage, &AdvancedPage::appendCustomParams);
        dialog.exec();
    });
}

void DLSSSettingsWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // Game header card
    QWidget* headerCard = new QWidget(this);
    headerCard->setStyleSheet(
        "QWidget#headerCard { background-color: #262626; border: 1px solid #4a4a4a; border-radius: 8px; }");
    headerCard->setObjectName("headerCard");
    QHBoxLayout* headerLayout = new QHBoxLayout(headerCard);
    headerLayout->setContentsMargins(12, 12, 12, 12);

    m_gameImageLabel = new QLabel(headerCard);
    m_gameImageLabel->setFixedSize(230, 107);
    m_gameImageLabel->setScaledContents(true);
    m_gameImageLabel->setStyleSheet("border-radius: 6px; background-color: #1a1a1a;");
    headerLayout->addWidget(m_gameImageLabel);

    QVBoxLayout* gameInfoLayout = new QVBoxLayout();
    m_gameNameLabel = new QLabel("Select a game", headerCard);
    m_gameNameLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #e0e0e0;");
    m_gameNameLabel->setWordWrap(true);
    gameInfoLayout->addWidget(m_gameNameLabel);

    m_platformBadge = new QLabel("", headerCard);
    m_platformBadge->setStyleSheet(
        "font-size: 11px; font-weight: bold; padding: 4px 8px; "
        "border-radius: 4px; background-color: #555; color: white;");
    m_platformBadge->setMaximumWidth(120);
    m_platformBadge->hide();

    // Clickable ProtonDB tier badge — sits next to the platform badge and opens
    // the recommendations dialog (tier + community launch-option tips).
    m_protonDbBadge = new QPushButton("ProtonDB: —", headerCard);
    m_protonDbBadge->setCursor(Qt::PointingHandCursor);
    m_protonDbBadge->setToolTip(
        "ProtonDB compatibility tier. Click to view reports and mine community "
        "launch-option tips for this game.");
    m_protonDbBadge->setStyleSheet(protonDbBadgeMutedStyle());
    m_protonDbBadge->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_protonDbBadge, &QPushButton::clicked, this, &DLSSSettingsWidget::onRecommendClicked);

    QHBoxLayout* badgeRow = new QHBoxLayout();
    badgeRow->setContentsMargins(0, 0, 0, 0);
    badgeRow->setSpacing(8);
    badgeRow->addWidget(m_platformBadge);
    badgeRow->addWidget(m_protonDbBadge);
    badgeRow->addStretch();
    gameInfoLayout->addLayout(badgeRow);

    // Proton version selector (container widget for easy show/hide)
    m_protonSelectorContainer = new QWidget(headerCard);
    QVBoxLayout* protonColumn = new QVBoxLayout(m_protonSelectorContainer);
    protonColumn->setContentsMargins(0, 0, 0, 0);
    protonColumn->setSpacing(2);
    QHBoxLayout* protonLayout = new QHBoxLayout();
    protonLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* protonLabel = new QLabel("Proton:", m_protonSelectorContainer);
    protonLabel->setStyleSheet("font-size: 12px; color: #999;");
    protonLabel->setMinimumWidth(75);
    protonLayout->addWidget(protonLabel);

    m_protonVersionSelector = new QComboBox(m_protonSelectorContainer);
    m_protonVersionSelector->setStyleSheet("font-size: 11px;");
    m_protonVersionSelector->setToolTip(
        "Select which Proton version to use.\n\n"
        "Automatic picks, in this order: the compatibility tool Steam has set "
        "for this game, the newest Proton-CachyOS, the newest GE-Proton, "
        "Proton Experimental, the newest Proton release from Valve.\n\n"
        "The \"Latest …\" entries always take the newest installed build of "
        "that kind, ignoring Steam's choice. A selection that is no longer "
        "installed falls back to Automatic.");
    m_protonVersionSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Resolve first: onSettingChanged() evaluates the warnings against it.
    connect(m_protonVersionSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
        if (!m_loading) refreshResolvedProton();
    });
    connect(m_protonVersionSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DLSSSettingsWidget::onSettingChanged);
    protonLayout->addWidget(m_protonVersionSelector, 1);
    protonColumn->addLayout(protonLayout);

    // What the selection actually resolves to — "Automatic" can mean four
    // different things, and a selected version may have been uninstalled.
    m_resolvedProtonLabel = new QLabel(m_protonSelectorContainer);
    m_resolvedProtonLabel->setStyleSheet("font-size: 11px; color: #888;");
    m_resolvedProtonLabel->setContentsMargins(81, 0, 0, 0);  // under the combo, past "Proton:"
    m_resolvedProtonLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    protonColumn->addWidget(m_resolvedProtonLabel);
    gameInfoLayout->addWidget(m_protonSelectorContainer);

    // Executable selector
    QHBoxLayout* exeLayout = new QHBoxLayout();
    QLabel* exeLabel = new QLabel("Executable:", headerCard);
    exeLabel->setStyleSheet("font-size: 12px; color: #999;");
    exeLabel->setMinimumWidth(75);
    exeLayout->addWidget(exeLabel);

    m_executableSelector = new QComboBox(headerCard);
    m_executableSelector->setStyleSheet("font-size: 11px;");
    m_executableSelector->setToolTip("Select which executable to launch");
    m_executableSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    exeLayout->addWidget(m_executableSelector, 1);
    gameInfoLayout->addLayout(exeLayout);

    gameInfoLayout->addStretch();

    headerLayout->addLayout(gameInfoLayout, 1);

    mainLayout->addWidget(headerCard);

    // Update available banner (shown when Steam has flagged the game for update)
    m_updateAvailableLabel = new QLabel(this);
    m_updateAvailableLabel->setText("Update available — Steam has flagged this game for update");
    m_updateAvailableLabel->setAlignment(Qt::AlignCenter);
    m_updateAvailableLabel->setStyleSheet(
        QString("background-color: %1; color: white; font-weight: bold; "
                "padding: 8px 12px; border-radius: 6px; font-size: 12px;")
            .arg(AppStyle::ColorBadgeUpdate));
    m_updateAvailableLabel->hide();
    mainLayout->addWidget(m_updateAvailableLabel);

    // Feature gating warnings — shown when an enabled option needs a newer
    // NVIDIA driver / Proton version than detected. Non-blocking (warn only).
    // Lives above the tab widget so warnings stay visible on every tab.
    m_featureWarnings = new QLabel(this);
    m_featureWarnings->setWordWrap(true);
    m_featureWarnings->setStyleSheet(
        QString("background-color: rgba(224,160,48,0.12); color: %1; "
                "border: 1px solid %1; border-radius: 6px; padding: 8px 12px; font-size: 12px;")
            .arg(AppStyle::ColorWarning));
    m_featureWarnings->hide();
    mainLayout->addWidget(m_featureWarnings);

    // Settings, grouped thematically into tabs; each tab scrolls on its own.
    m_advancedPage = new AdvancedPage(this);
    m_pages = {new GeneralPage(this), new DlssPage(this), new HdrPage(this),
               new ProtonPage(this), m_advancedPage};
    QTabWidget* tabWidget = new QTabWidget(this);
    const char* const titles[] = {"General", "DLSS", "HDR", "Proton", "Advanced"};
    for (int i = 0; i < m_pages.size(); ++i) {
        connect(m_pages[i], &SettingsPage::changed, this, &DLSSSettingsWidget::onSettingChanged);
        tabWidget->addTab(createScrollTab(m_pages[i]), titles[i]);
    }
    mainLayout->addWidget(tabWidget, 1);

    // Launch command preview
    QGroupBox* previewGroup = new QGroupBox("Launch Options Preview", this);
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    m_launchCommandPreview = new QTextEdit(this);
    m_launchCommandPreview->setReadOnly(true);
    m_launchCommandPreview->setMaximumHeight(80);
    m_launchCommandPreview->setStyleSheet(
        "font-family: monospace; background-color: #1a1a1a; border: 1px solid #4a4a4a; "
        "border-radius: 4px; color: #c0c0c0; padding: 6px;");
    previewLayout->addWidget(m_launchCommandPreview);

    mainLayout->addWidget(previewGroup);

    // Action buttons
    mainLayout->addWidget(createActionsSection());

    // Style combo popups directly on their views (stylesheet on parent doesn't reach top-level popups)
    styleComboBoxPopups();
}

QWidget* DLSSSettingsWidget::createScrollTab(SettingsPage* page)
{
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    static_cast<QBoxLayout*>(page->layout())->addStretch();
    scroll->setWidget(page);
    return scroll;
}

void DLSSSettingsWidget::styleComboBoxPopups()
{
    const QString viewStyle = AppStyle::comboPopupStyle();
    const auto combos = findChildren<QComboBox*>();
    for (QComboBox* combo : combos) {
        combo->view()->setStyleSheet(viewStyle);
    }
}

QWidget* DLSSSettingsWidget::createActionsSection()
{
    QWidget* widget = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 10, 0, 0);

    m_playButton = new QPushButton("Play", this);
    m_playButton->setStyleSheet(AppStyle::playButtonStyle());
    m_playButton->setToolTip("Launch game directly with DLSS settings via Proton");
    layout->addWidget(m_playButton);

    m_copyButton = new QPushButton("Copy to Clipboard", this);
    m_copyButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    m_copyButton->setToolTip("Copy launch options to clipboard for manual paste into Steam");
    layout->addWidget(m_copyButton);

    m_importButton = new QPushButton("Import from Launcher", this);
    m_importButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    m_importButton->setToolTip(
        "Read this game's existing launch options from its launcher and map "
        "them onto the controls above. Unknown options go into Custom Launch "
        "Parameters.");
    layout->addWidget(m_importButton);

    m_writeToSteamButton = new QPushButton("Write to Launcher", this);
    m_writeToSteamButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    m_writeToSteamButton->setToolTip("Write launch options directly into the launcher's own config");
    layout->addWidget(m_writeToSteamButton);

    layout->addStretch();

    connect(m_playButton, &QPushButton::clicked, this, &DLSSSettingsWidget::playClicked);
    connect(m_copyButton, &QPushButton::clicked, this, &DLSSSettingsWidget::copyClicked);
    connect(m_importButton, &QPushButton::clicked, this, &DLSSSettingsWidget::importFromLauncherClicked);
    connect(m_writeToSteamButton, &QPushButton::clicked, this, &DLSSSettingsWidget::writeToLauncherClicked);

    return widget;
}

void DLSSSettingsWidget::setGame(const Game& game)
{
    m_currentGame = game;
    m_gameNameLabel->setText(game.name());

    // Platform badge
    if (game.isNativeLinux()) {
        m_platformBadge->setText("🐧 Native Linux");
        m_platformBadge->setStyleSheet(AppStyle::platformBadgeStyle(true));
        m_platformBadge->show();
    } else {
        m_platformBadge->setText("🪟 Windows");
        m_platformBadge->setStyleSheet(AppStyle::platformBadgeStyle(false));
        m_platformBadge->show();
    }

    m_updateAvailableLabel->setVisible(game.needsUpdate());

    // Only offer reading and writing launch options to launchers that actually
    // store them. Hiding beats showing a button and then explaining it does
    // nothing for this game.
    const LauncherTraits traits = game.traits();
    m_importButton->setVisible(traits.supportsLaunchOptionsIO);
    m_writeToSteamButton->setVisible(traits.supportsLaunchOptionsIO);

    // Show/hide Proton version selector based on game type
    if (game.isNativeLinux()) {
        // Hide Proton-specific UI for native Linux games
        m_protonSelectorContainer->hide();
    } else {
        // Show and populate Proton UI for Windows games
        m_protonSelectorContainer->show();
        populateProtonVersionSelector();
    }

    // Grey out whatever cannot reach this game before its settings are loaded,
    // so the panel is never briefly shown offering options that do nothing.
    // A native ELF binary loads no Proton, no DXVK, no vkd3d-proton and no
    // DXVK-NVAPI, so every variable those layers read is inert — and for the
    // DLSS overrides there is no workaround: NVIDIA does not support preset
    // overrides outside NVAPI's driver-settings layer, which has no native
    // counterpart. The values themselves are kept (see SettingsPage::gate),
    // because a user who forces Proton on a native title in Steam gets the
    // Windows build, and then the options apply after all.
    for (SettingsPage* page : m_pages)
        page->setGame(game);

    // Populate executable selector
    populateExecutableSelector(game);

    // A placeholder now, the real one when it arrives — see the constructor for
    // where that is picked up. (The connection used to be made here, capturing
    // this game: selecting fifty games left fifty live lambdas, and a cover
    // that arrived late would repaint the panel for a game the user had long
    // since navigated away from.)
    m_gameImageLabel->setPixmap(ImageCache::instance().getImage(game.imageUrl(), QSize(230, 107)));

    // Fetch the ProtonDB tier for the badge. ProtonDB is keyed by Steam appid,
    // so this is the one thing standing between another store's numeric
    // product id and a request to protondb.com for a different game entirely
    // (fetchSummary has no id check of its own).
    m_protonDbSummary = ProtonDBClient::Summary();
    m_protonDbSummaryAppId.clear();
    const bool hasSteamAppId = game.traits().idIsSteamAppId && !game.id().isEmpty();
    m_protonDbBadge->setEnabled(hasSteamAppId);
    m_protonDbBadge->setStyleSheet(protonDbBadgeMutedStyle());
    if (hasSteamAppId) {
        m_protonDbBadge->setText("ProtonDB: checking…");
        ProtonDBClient::instance().fetchSummary(game.id());
    } else {
        m_protonDbBadge->setText("ProtonDB: n/a");
    }
}

void DLSSSettingsWidget::updateGameStatus(const Game& game)
{
    if (m_currentGame.id() != game.id() || m_currentGame.launcher() != game.launcher()) {
        return;
    }
    m_currentGame.setStateFlags(game.stateFlags());
    m_currentGame.setBuildId(game.buildId());
    m_currentGame.setNeedsUpdate(game.needsUpdate());
    m_updateAvailableLabel->setVisible(m_currentGame.needsUpdate());
}

void DLSSSettingsWidget::setGameRunning(bool running)
{
    if (running) {
        m_playButton->setText("Game is running...");
        m_playButton->setStyleSheet(AppStyle::playButtonRunningStyle());
        m_playButton->setEnabled(false);
        m_playButton->setToolTip("Game is currently running");
    } else {
        m_playButton->setText("Play");
        m_playButton->setStyleSheet(AppStyle::playButtonStyle());
        m_playButton->setEnabled(true);
        m_playButton->setToolTip("Launch game directly with DLSS settings via Proton");
    }
}

void DLSSSettingsWidget::setOtherGameRunning(const QString& gameName)
{
    m_playButton->setText(QString("%1 is running...").arg(gameName));
    m_playButton->setStyleSheet(AppStyle::playButtonRunningStyle());
    m_playButton->setEnabled(false);
    m_playButton->setToolTip("Only one game can run at a time");
}

void DLSSSettingsWidget::setLaunchPending(bool pending)
{
    if (pending) {
        m_playButton->setText("Waiting for Steam...");
        m_playButton->setStyleSheet(AppStyle::playButtonRunningStyle());
        m_playButton->setEnabled(false);
        m_playButton->setToolTip("Waiting for the Steam client to become ready");
    } else {
        setGameRunning(false);
    }
}

void DLSSSettingsWidget::setSettings(const DLSSSettings& settings)
{
    // Every widget below is written one at a time, so a handler that ran part
    // way through would read the previous game's values from the widgets not
    // yet written and MainWindow would save that mix under the new game. The
    // blocked signals are the first line of defence; m_loading catches any
    // handler that runs anyway.
    m_loading = true;
    m_loaded = settings;
    {
        std::vector<QSignalBlocker> blockers;
        for (SettingsPage* page : m_pages) {
            for (QWidget* input : page->inputWidgets())
                blockers.emplace_back(input);
        }
        blockers.emplace_back(m_protonVersionSelector);

        for (SettingsPage* page : m_pages)
            page->load(settings);

        // Save executable path for later restoration after async search completes
        m_savedExecutablePath = settings.executablePath;

        // Try to restore immediately (will only work if search already completed)
        if (!settings.executablePath.isEmpty()) {
            const QSignalBlocker blocker(m_executableSelector);
            const int index = m_executableSelector->findData(settings.executablePath);
            if (index >= 0) m_executableSelector->setCurrentIndex(index);
        }

        // Restore saved Proton version selection; a choice that is gone shows
        // as "Automatic", which is what a launch would fall back to.
        const QString protonVersionKey = settings.protonVersion.isEmpty() ? "auto" : settings.protonVersion;
        m_protonVersionSelector->setCurrentIndex(
            std::max(0, m_protonVersionSelector->findData(protonVersionKey)));
    }
    m_loading = false;

    // The warnings below evaluate against it, and it depends on the game, the
    // selection and Steam's per-game mapping — all of which just changed.
    refreshResolvedProton();

    // Update launch command preview
    updateLaunchCommand(EnvBuilder::buildLaunchOptions(settings));

    // Refresh driver/Proton requirement warnings for the loaded settings
    updateFeatureWarnings();
}

DLSSSettings DLSSSettingsWidget::settings() const
{
    // Starts from what was loaded, so fields without a control of their own
    // (foreign VKD3D_CONFIG / DXVK_CONFIG entries from an import) survive an
    // edit to any control; every page then writes the fields it shows.
    DLSSSettings settings = m_loaded;
    for (const SettingsPage* page : m_pages)
        page->store(settings);

    // Executable Selection. Empty while the search runs or when it found
    // nothing — keep the saved choice rather than forget it.
    const QString exe = m_executableSelector->currentData().toString();
    settings.executablePath = exe.isEmpty() ? m_savedExecutablePath : exe;

    // Proton Version Selection — "auto" is the default and stored as empty.
    const QString protonVersion = m_protonVersionSelector->currentData().toString();
    settings.protonVersion = protonVersion == QLatin1String("auto") ? QString() : protonVersion;

    return settings;
}

void DLSSSettingsWidget::updateLaunchCommand(const QString& command)
{
    m_launchCommandPreview->setText(command);
}

void DLSSSettingsWidget::onSettingChanged()
{
    if (m_loading) {
        return;
    }
    DLSSSettings s = settings();
    updateLaunchCommand(EnvBuilder::buildLaunchOptions(s));
    updateFeatureWarnings();
    emit settingsChanged(s);
}

void DLSSSettingsWidget::onRecommendClicked()
{
    if (m_currentGame.id().isEmpty() || !m_currentGame.traits().idIsSteamAppId) {
        return;
    }
    m_protonDbBadge->setEnabled(false);
    m_protonDbBadge->setText("ProtonDB: searching…");
    m_protonDbBadge->setStyleSheet(protonDbBadgeMutedStyle());
    ProtonDBClient::instance().fetchReports(m_currentGame.id());
}

void DLSSSettingsWidget::updateProtonDbBadge(const ProtonDBClient::Summary& summary)
{
    if (!summary.valid || summary.tier.isEmpty()) {
        m_protonDbBadge->setText("ProtonDB: unknown");
        m_protonDbBadge->setStyleSheet(protonDbBadgeMutedStyle());
        return;
    }

    // Tier-coloured chip; mirrors the colours used in RecommendationsDialog.
    const QString t = summary.tier.toLower();
    QString color = AppStyle::ColorTextMuted;
    if (t == "platinum") color = "#b4c7dc";
    else if (t == "gold") color = "#cfb53b";
    else if (t == "silver") color = "#a8a8a8";
    else if (t == "bronze") color = "#cd7f32";
    else if (t == "borked") color = AppStyle::ColorDanger;

    QString text = QString("ProtonDB: %1").arg(summary.tier.toUpper());
    if (summary.total > 0) {
        text += QString("  (%1 reports)").arg(summary.total);
    }
    m_protonDbBadge->setText(text);
    m_protonDbBadge->setStyleSheet(protonDbBadgeTierStyle(color));
}

void DLSSSettingsWidget::refreshResolvedProton()
{
    // Asks the launch path itself, so the warnings can never be about a
    // different Proton than the one a launch would start. Not called per
    // keystroke: for a Steam game it parses config.vdf, and only the game,
    // the selection or the installed Protons can change the answer.
    DLSSSettings selection;
    selection.protonVersion = m_protonVersionSelector->currentData().toString();
    m_resolvedProton = GameRunner().resolveProtonTool(m_currentGame, selection);

    if (m_resolvedProton.isValid()) {
        m_resolvedProtonLabel->setText(QStringLiteral("→ %1").arg(m_resolvedProton.dirName));
        m_resolvedProtonLabel->setToolTip(m_resolvedProton.path);
    } else {
        m_resolvedProtonLabel->setText(QStringLiteral("→ no Proton installed"));
        m_resolvedProtonLabel->setToolTip(QString());
    }
}

void DLSSSettingsWidget::updateFeatureWarnings()
{
    QStringList lines;

    // Say once, up front, why half the panel is greyed — a disabled control
    // with no explanation reads as a bug. Everything below is then reported
    // only for options that actually reach the game: telling someone their
    // driver is too old for a variable that will never be read is worse than
    // silence.
    if (m_currentGame.isNativeLinux()) {
        lines << QStringLiteral(
            "⚠ Native Linux build: the DLSS overrides, the DXVK/VKD3D options and the "
            "Proton tweaks are greyed out because the game loads no Proton, no DXVK and "
            "no DXVK-NVAPI — there is nothing to read them. NVIDIA does not support DLSS "
            "preset overrides for native Linux titles; use the game's own graphics "
            "options, or force a Proton compatibility tool in Steam to configure the "
            "Windows build instead. Your settings are kept either way.");
    }

    // The Proton a launch would really use (see refreshResolvedProton()).
    const FeatureWarnings::Input input = FeatureWarnings::inputFor(
        GpuInfoCache::instance().facts(), m_resolvedProton, m_currentGame);
    for (const QString& line : FeatureWarnings::collect(settings(), input))
        lines << QStringLiteral("⚠ ") + line;

    if (lines.isEmpty()) {
        m_featureWarnings->hide();
    } else {
        m_featureWarnings->setText(lines.join("\n"));
        m_featureWarnings->show();
    }
}

void DLSSSettingsWidget::populateExecutableSelector(const Game& game)
{
    // Show loading state
    m_executableSelector->blockSignals(true);
    m_executableSelector->clear();
    m_executableSelector->addItem("Searching for executables...", "");
    m_executableSelector->setEnabled(false);
    m_executableSelector->blockSignals(false);

    // Signal any previous search to stop
    if (m_cancelFlag) {
        m_cancelFlag->store(true);
    }

    // Increment generation so stale results are ignored
    unsigned int generation = ++m_searchGeneration;
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    m_cancelFlag = cancelFlag;

    // Start async search in background thread
    QString installPath = game.installPath();
    bool isLinux = game.isNativeLinux();

    QFuture<QStringList> future = QtConcurrent::run([installPath, isLinux, cancelFlag]() {
        return ExecutableFinder::candidates(installPath, isLinux, cancelFlag);
    });

    m_executableWatcher->setFuture(future);
}

void DLSSSettingsWidget::updateExecutableSelectorWithResults(const QStringList& executables)
{
    m_executableSelector->blockSignals(true);
    m_executableSelector->clear();

    if (executables.isEmpty()) {
        m_executableSelector->addItem("No executables found", "");
        m_executableSelector->setEnabled(false);
    } else {
        for (const QString& exe : executables) {
            QString displayName = QFileInfo(exe).fileName();
            m_executableSelector->addItem(displayName, exe);
        }
        m_executableSelector->setEnabled(true);

        // First, try to restore the saved executable path from settings
        bool restored = false;
        if (!m_savedExecutablePath.isEmpty()) {
            for (int i = 0; i < m_executableSelector->count(); ++i) {
                if (m_executableSelector->itemData(i).toString() == m_savedExecutablePath) {
                    m_executableSelector->setCurrentIndex(i);
                    restored = true;
                    break;
                }
            }
        }

        // If no saved path or it wasn't found, select the best match
        if (!restored) {
            // The same choice a launch makes when none is saved: the
            // launcher's own (GOG's primary play task), else the best match.
            const QString bestMatch = !m_currentGame.executablePath().isEmpty()
                ? m_currentGame.executablePath()
                : ExecutableFinder::bestMatch(m_currentGame.name(),
                                              m_currentGame.installPath(), executables);
            for (int i = 0; i < m_executableSelector->count(); ++i) {
                if (m_executableSelector->itemData(i).toString() == bestMatch) {
                    m_executableSelector->setCurrentIndex(i);
                    break;
                }
            }
        }
    }

    m_executableSelector->blockSignals(false);

    // Connect the signal to save when user changes selection
    // Disconnect first to avoid multiple connections
    disconnect(m_executableSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), nullptr, nullptr);
    connect(m_executableSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0 && m_executableSelector->isEnabled()) {
            // Trigger settings save
            emit settingsChanged(settings());
        }
    });
}

// Delegate that indents selectable items in the Proton combo popup
static constexpr int ProtonGroupRole = Qt::UserRole + 100;

class ProtonComboDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        // Indent non-header items in the popup
        bool isHeader = !index.flags().testFlag(Qt::ItemIsSelectable);
        if (!isHeader) {
            opt.rect.adjust(14, 0, 0, 0);
        }
        QStyledItemDelegate::paint(painter, opt, index);
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        bool isHeader = !index.flags().testFlag(Qt::ItemIsSelectable);
        if (isHeader) {
            s.setHeight(s.height() + 4);  // slightly taller headers
        }
        return s;
    }
};

void DLSSSettingsWidget::populateProtonVersionSelector()
{
    m_protonVersionSelector->blockSignals(true);
    m_protonVersionSelector->clear();

    auto* model = qobject_cast<QStandardItemModel*>(m_protonVersionSelector->model());
    if (!model) {
        m_protonVersionSelector->setModel(new QStandardItemModel(m_protonVersionSelector));
        model = qobject_cast<QStandardItemModel*>(m_protonVersionSelector->model());
    }

    // Install the indent delegate on the popup view (only once)
    static bool delegateInstalled = false;
    if (!delegateInstalled) {
        m_protonVersionSelector->view()->setItemDelegate(
            new ProtonComboDelegate(m_protonVersionSelector));
        delegateInstalled = true;
    }

    // Helper: add a non-selectable group header
    auto addGroupHeader = [&](const QString& title) {
        m_protonVersionSelector->addItem(title);
        int idx = m_protonVersionSelector->count() - 1;
        if (auto* item = model->item(idx)) {
            item->setEnabled(false);
            item->setSelectable(false);
            item->setData(QColor(AppStyle::ColorAccent), Qt::ForegroundRole);
            QFont f = item->font();
            f.setBold(true);
            f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.95 : 9);
            item->setFont(f);
        }
    };

    // Helper: add a selectable version item (indent handled by delegate)
    auto addVersionItem = [&](const QString& displayName, const QVariant& data) {
        m_protonVersionSelector->addItem(displayName, data);
    };

    // ── Discover installed versions ─────────────────────────────────────────
    // Newest first by version, not by name: sorting names put GE-Proton9-20
    // above GE-Proton11-7.
    using namespace CompatToolResolver;
    const QList<ToolInfo> managed =
        sortedNewestFirst(scanCompatDirs({ProtonManager::protonCachyOSPath()}));
    const QList<ToolInfo> steamTools =
        scanLibraries(SteamLauncher::libraryPaths());
    // What findProtonPath() resolves the "Latest …" entries against.
    const QList<ToolInfo> installed = ProtonManager::instance().installedTools();

    // ── Automatic / Latest ──────────────────────────────────────────────────
    // A "Latest …" entry only when there is something of that kind to be the
    // latest of — otherwise picking it would silently launch something else.
    addGroupHeader("— Automatic —");
    addVersionItem("Automatic (Recommended)", "auto");
    if (newest(installed, Kind::CachyOS).isValid())
        addVersionItem("Latest Proton-CachyOS", "latest-cachyos");
    if (newest(installed, Kind::GE).isValid())
        addVersionItem("Latest Proton-GE", "latest-ge");
    if (latestSteam(steamTools).isValid())
        addVersionItem("Latest Steam Proton", "steam-proton");

    // ── Proton-CachyOS ─────────────────────────────────────────────────────
    // Stored as the bare folder name; findProtonPath() resolves it against
    // ProtonForge's install directory.
    bool header = false;
    for (const ToolInfo& t : managed) {
        if (t.kind != Kind::CachyOS) continue;
        if (!header) { addGroupHeader("— Proton-CachyOS —"); header = true; }
        addVersionItem(t.dirName.mid(QStringLiteral("proton-cachyos-").size()), t.dirName);
    }

    // ── GE-Proton ───────────────────────────────────────────────────────────
    header = false;
    for (const ToolInfo& t : managed) {
        if (t.kind != Kind::GE) continue;
        if (!header) { addGroupHeader("— GE-Proton —"); header = true; }
        addVersionItem(t.dirName.mid(3), t.dirName);
    }

    // ── Steam Proton ────────────────────────────────────────────────────────
    // Stored as absolute paths. Experimental first, then numbered releases
    // newest first, then Hotfix.
    QList<ToolInfo> steamOrdered;
    for (Kind kind : {Kind::ValveExperimental, Kind::Valve, Kind::ValveHotfix}) {
        QList<ToolInfo> ofKind;
        for (const ToolInfo& t : steamTools)
            if (t.kind == kind) ofKind << t;
        steamOrdered += sortedNewestFirst(ofKind);
    }
    if (!steamOrdered.isEmpty()) {
        addGroupHeader("— Steam Proton —");
        for (const ToolInfo& t : steamOrdered) {
            addVersionItem(t.dirName, t.path);
        }
    }

    m_protonVersionSelector->blockSignals(false);
}

