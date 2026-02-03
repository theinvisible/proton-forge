#include "DLSSSettingsWidget.h"
#include "network/ImageCache.h"
#include "utils/EnvBuilder.h"
#include "utils/ProtonManager.h"
#include "runner/GameRunner.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <algorithm>

DLSSSettingsWidget::DLSSSettingsWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void DLSSSettingsWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // Game header
    QHBoxLayout* headerLayout = new QHBoxLayout();
    m_gameImageLabel = new QLabel(this);
    m_gameImageLabel->setFixedSize(230, 107);
    m_gameImageLabel->setScaledContents(true);
    m_gameImageLabel->setStyleSheet("border: 1px solid #333; background-color: #222;");
    headerLayout->addWidget(m_gameImageLabel);

    QVBoxLayout* gameInfoLayout = new QVBoxLayout();
    m_gameNameLabel = new QLabel("Select a game", this);
    m_gameNameLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    m_gameNameLabel->setWordWrap(true);
    gameInfoLayout->addWidget(m_gameNameLabel);

    m_platformBadge = new QLabel("", this);
    m_platformBadge->setStyleSheet(
        "font-size: 11px; font-weight: bold; padding: 4px 8px; "
        "border-radius: 3px; background-color: #555; color: white;");
    m_platformBadge->setMaximumWidth(120);
    m_platformBadge->hide();  // Hidden until a game is selected
    gameInfoLayout->addWidget(m_platformBadge);

    m_protonVersionLabel = new QLabel("", this);
    m_protonVersionLabel->setStyleSheet("font-size: 12px; color: #888;");
    m_protonVersionLabel->setWordWrap(true);
    gameInfoLayout->addWidget(m_protonVersionLabel);

    // Proton version selector
    QHBoxLayout* protonLayout = new QHBoxLayout();
    QLabel* protonLabel = new QLabel("Proton:", this);
    protonLabel->setStyleSheet("font-size: 12px; color: #888;");
    protonLayout->addWidget(protonLabel);

    m_protonVersionSelector = new QComboBox(this);
    m_protonVersionSelector->setStyleSheet("font-size: 11px;");
    m_protonVersionSelector->setToolTip("Select which Proton version to use");
    m_protonVersionSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_protonVersionSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DLSSSettingsWidget::onSettingChanged);
    protonLayout->addWidget(m_protonVersionSelector, 1);
    gameInfoLayout->addLayout(protonLayout);

    // Executable selector
    QHBoxLayout* exeLayout = new QHBoxLayout();
    QLabel* exeLabel = new QLabel("Executable:", this);
    exeLabel->setStyleSheet("font-size: 12px; color: #888;");
    exeLayout->addWidget(exeLabel);

    m_executableSelector = new QComboBox(this);
    m_executableSelector->setStyleSheet("font-size: 11px;");
    m_executableSelector->setToolTip("Select which executable to launch");
    m_executableSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    exeLayout->addWidget(m_executableSelector, 1);
    gameInfoLayout->addLayout(exeLayout);

    gameInfoLayout->addStretch();

    headerLayout->addLayout(gameInfoLayout, 1);

    mainLayout->addLayout(headerLayout);

    // Scroll area for settings
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* scrollContent = new QWidget();
    QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setSpacing(10);

    scrollLayout->addWidget(createGeneralGroup());
    scrollLayout->addWidget(createSuperResolutionGroup());
    scrollLayout->addWidget(createRayReconstructionGroup());
    scrollLayout->addWidget(createFrameGenerationGroup());
    scrollLayout->addWidget(createUpgradeGroup());
    scrollLayout->addWidget(createSmoothMotionGroup());
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    // Launch command preview
    QGroupBox* previewGroup = new QGroupBox("Launch Options Preview", this);
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    m_launchCommandPreview = new QTextEdit(this);
    m_launchCommandPreview->setReadOnly(true);
    m_launchCommandPreview->setMaximumHeight(80);
    m_launchCommandPreview->setStyleSheet("font-family: monospace;");
    previewLayout->addWidget(m_launchCommandPreview);
    mainLayout->addWidget(previewGroup);

    // Action buttons
    mainLayout->addWidget(createActionsSection());
}

QGroupBox* DLSSSettingsWidget::createGeneralGroup()
{
    QGroupBox* group = new QGroupBox("General", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_enableNVAPI = new QCheckBox("Enable NVAPI (PROTON_ENABLE_NVAPI)", this);
    m_enableNVAPI->setToolTip(
        "Enable NVIDIA API support in Proton.\n\n"
        "Required for DLSS and other NVIDIA features to work. "
        "This enables dxvk-nvapi which translates NVIDIA-specific DirectX calls to Vulkan.\n\n"
        "Recommended: Enabled for all NVIDIA GPU users.");
    layout->addWidget(m_enableNVAPI);

    m_enableNGXUpdater = new QCheckBox("Enable NGX Updater (PROTON_ENABLE_NGX_UPDATER)", this);
    m_enableNGXUpdater->setToolTip(
        "Allow NVIDIA NGX to automatically update DLSS DLLs.\n\n"
        "When enabled, NGX can download newer DLSS versions from NVIDIA servers. "
        "This may improve quality and performance in some games.\n\n"
        "Note: Requires internet connection and may increase loading times.");
    layout->addWidget(m_enableNGXUpdater);

    m_showIndicator = new QCheckBox("Show DLSS Indicator (PROTON_DLSS_INDICATOR)", this);
    m_showIndicator->setToolTip(
        "Display an on-screen DLSS status indicator in-game.\n\n"
        "Shows which DLSS features are active (SR/RR/FG) and their settings. "
        "Useful for verifying that your settings are being applied correctly.\n\n"
        "The indicator appears as an overlay in the corner of the screen.");
    layout->addWidget(m_showIndicator);

    connect(m_enableNVAPI, &QCheckBox::toggled, this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_enableNGXUpdater, &QCheckBox::toggled, this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_showIndicator, &QCheckBox::toggled, this, &DLSSSettingsWidget::onSettingChanged);

    return group;
}

QGroupBox* DLSSSettingsWidget::createSuperResolutionGroup()
{
    QGroupBox* group = new QGroupBox("Super Resolution (DLSS SR)", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_srOverride = new QCheckBox("Override SR Settings", this);
    m_srOverride->setToolTip(
        "Override DLSS Super Resolution settings chosen by the game.\n\n"
        "DLSS SR upscales rendered frames from a lower resolution to improve performance. "
        "Enable this to force specific quality/performance settings instead of using the game's defaults.\n\n"
        "Use this if the game doesn't expose DLSS options or you want more control.");
    layout->addWidget(m_srOverride);

    QFormLayout* formLayout = new QFormLayout();

    m_srMode = new QComboBox(this);
    for (const QString& mode : DLSSSettings::availableSRModes()) {
        m_srMode->addItem(mode.isEmpty() ? "(App Default)" : mode, mode);
    }
    m_srMode->setToolTip(
        "DLSS Super Resolution quality preset:\n\n"
        "• PERFORMANCE: Lowest internal resolution, highest FPS boost (~50% render scale)\n"
        "• BALANCED: Balanced quality/performance (~58% render scale)\n"
        "• QUALITY: Higher quality, moderate FPS boost (~67% render scale)\n"
        "• ULTRA_PERFORMANCE: Maximum performance, lowest quality (~33% render scale)\n"
        "• DLAA: AI anti-aliasing at native resolution (no upscaling)\n"
        "• CUSTOM: Use manual scaling ratio instead of preset");
    formLayout->addRow("Mode:", m_srMode);

    m_srPreset = new QComboBox(this);
    for (const QString& preset : DLSSSettings::availablePresets()) {
        m_srPreset->addItem(preset.isEmpty() ? "(App Default)" : preset, preset);
    }
    m_srPreset->setToolTip(
        "DLSS rendering preset selection:\n\n"
        "Different presets (A through O) tune the AI model for specific quality characteristics. "
        "Most games work best with RENDER_PRESET_LATEST which uses the newest preset.\n\n"
        "Only change this if you experience specific quality issues or are testing.");
    formLayout->addRow("Render Preset:", m_srPreset);

    m_srScalingRatio = new QSpinBox(this);
    m_srScalingRatio->setRange(0, 100);
    m_srScalingRatio->setSpecialValueText("(App Default)");
    m_srScalingRatio->setSuffix("%");
    m_srScalingRatio->setToolTip(
        "Manual scaling ratio override (33-100%):\n\n"
        "Sets the percentage of native resolution to render internally before upscaling. "
        "Lower = better performance, potentially lower quality.\n"
        "Higher = better quality, less performance gain.\n\n"
        "Examples at 4K:\n"
        "• 50% = Render at 1080p, upscale to 4K\n"
        "• 67% = Render at ~1440p, upscale to 4K\n\n"
        "0 = Use app default or mode preset");
    formLayout->addRow("Scaling Ratio:", m_srScalingRatio);

    layout->addLayout(formLayout);

    // Enable/disable based on override checkbox
    auto updateSREnabled = [this]() {
        bool enabled = m_srOverride->isChecked();
        m_srMode->setEnabled(enabled);
        m_srPreset->setEnabled(enabled);
        m_srScalingRatio->setEnabled(enabled);
    };

    connect(m_srOverride, &QCheckBox::toggled, this, [updateSREnabled, this]() {
        updateSREnabled();
        onSettingChanged();
    });
    connect(m_srMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_srPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_srScalingRatio, QOverload<int>::of(&QSpinBox::valueChanged), this, &DLSSSettingsWidget::onSettingChanged);

    updateSREnabled();

    return group;
}

QGroupBox* DLSSSettingsWidget::createRayReconstructionGroup()
{
    QGroupBox* group = new QGroupBox("Ray Reconstruction (DLSS RR)", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_rrOverride = new QCheckBox("Override RR Settings", this);
    m_rrOverride->setToolTip(
        "Override DLSS Ray Reconstruction settings.\n\n"
        "DLSS RR uses AI to reconstruct high-quality ray-traced images from fewer rays, "
        "dramatically improving ray tracing performance without sacrificing quality.\n\n"
        "Only applicable in games with ray tracing support. "
        "Enable this to control RR quality independently from Super Resolution.");
    layout->addWidget(m_rrOverride);

    QFormLayout* formLayout = new QFormLayout();

    m_rrMode = new QComboBox(this);
    for (const QString& mode : DLSSSettings::availableRRModes()) {
        m_rrMode->addItem(mode.isEmpty() ? "(App Default)" : mode, mode);
    }
    m_rrMode->setToolTip(
        "Ray Reconstruction quality mode:\n\n"
        "• PERFORMANCE: Fewer rays traced, maximum FPS boost\n"
        "• BALANCED: Balanced ray count and quality\n"
        "• QUALITY: More rays traced, better visual quality\n"
        "• ULTRA_PERFORMANCE: Minimum rays, maximum performance\n"
        "• DLAA: Full ray count with AI denoising\n\n"
        "Higher quality modes trace more rays but have lower performance impact.");
    formLayout->addRow("Mode:", m_rrMode);

    m_rrPreset = new QComboBox(this);
    for (const QString& preset : DLSSSettings::availablePresets()) {
        m_rrPreset->addItem(preset.isEmpty() ? "(App Default)" : preset, preset);
    }
    m_rrPreset->setToolTip(
        "Ray Reconstruction AI model preset:\n\n"
        "Different presets tune the denoising algorithm. "
        "RENDER_PRESET_LATEST uses the newest model optimizations.\n\n"
        "Generally leave at default unless troubleshooting quality issues.");
    formLayout->addRow("Render Preset:", m_rrPreset);

    m_rrScalingRatio = new QSpinBox(this);
    m_rrScalingRatio->setRange(0, 100);
    m_rrScalingRatio->setSpecialValueText("(App Default)");
    m_rrScalingRatio->setSuffix("%");
    m_rrScalingRatio->setToolTip(
        "Ray budget scaling ratio (33-100%):\n\n"
        "Percentage of full ray count to trace. Lower values trace fewer rays, "
        "improving performance at the cost of reconstruction quality.\n\n"
        "The AI reconstructs the missing detail from the reduced ray samples.\n\n"
        "0 = Use app default or mode preset");
    formLayout->addRow("Scaling Ratio:", m_rrScalingRatio);

    layout->addLayout(formLayout);

    auto updateRREnabled = [this]() {
        bool enabled = m_rrOverride->isChecked();
        m_rrMode->setEnabled(enabled);
        m_rrPreset->setEnabled(enabled);
        m_rrScalingRatio->setEnabled(enabled);
    };

    connect(m_rrOverride, &QCheckBox::toggled, this, [updateRREnabled, this]() {
        updateRREnabled();
        onSettingChanged();
    });
    connect(m_rrMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_rrPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_rrScalingRatio, QOverload<int>::of(&QSpinBox::valueChanged), this, &DLSSSettingsWidget::onSettingChanged);

    updateRREnabled();

    return group;
}

QGroupBox* DLSSSettingsWidget::createFrameGenerationGroup()
{
    QGroupBox* group = new QGroupBox("Frame Generation (DLSS FG)", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_fgOverride = new QCheckBox("Override FG Settings", this);
    m_fgOverride->setToolTip(
        "Override DLSS Frame Generation settings.\n\n"
        "DLSS FG uses AI to generate entirely new frames between rendered frames, "
        "potentially doubling, tripling, or even quadrupling your frame rate.\n\n"
        "Requirements:\n"
        "• RTX 40-series GPU or newer\n"
        "• Game with DLSS 3+ support\n\n"
        "Note: Adds slight input latency. Use NVIDIA Reflex to minimize.");
    layout->addWidget(m_fgOverride);

    QFormLayout* formLayout = new QFormLayout();

    m_fgMultiFrameCount = new QComboBox(this);
    m_fgMultiFrameCount->addItem("(App Default)", 0);
    m_fgMultiFrameCount->addItem("2x Frame Generation", 1);
    m_fgMultiFrameCount->addItem("3x Frame Generation", 2);
    m_fgMultiFrameCount->addItem("4x Frame Generation", 3);
    m_fgMultiFrameCount->setToolTip(
        "Number of AI-generated frames inserted between each rendered frame:\n\n"
        "• 0 (App Default): Let the game decide\n"
        "• 1 (2x): Generate 1 frame → 2x total FPS\n"
        "• 2 (3x): Generate 2 frames → 3x total FPS\n"
        "• 3 (4x): Generate 3 frames → 4x total FPS\n\n"
        "Higher values give more FPS but increase input latency. "
        "DLSS 3.5+ required for 3x/4x modes.\n\n"
        "Example: 60 rendered FPS + 2x FG = 120 displayed FPS");
    formLayout->addRow("Multi-Frame Count:", m_fgMultiFrameCount);

    layout->addLayout(formLayout);

    auto updateFGEnabled = [this]() {
        m_fgMultiFrameCount->setEnabled(m_fgOverride->isChecked());
    };

    connect(m_fgOverride, &QCheckBox::toggled, this, [updateFGEnabled, this]() {
        updateFGEnabled();
        onSettingChanged();
    });
    connect(m_fgMultiFrameCount, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DLSSSettingsWidget::onSettingChanged);

    updateFGEnabled();

    return group;
}

QGroupBox* DLSSSettingsWidget::createUpgradeGroup()
{
    QGroupBox* group = new QGroupBox("DLSS Upgrade", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_dlssUpgrade = new QCheckBox("Enable DLSS Upgrade (PROTON_DLSS_UPGRADE)", this);
    m_dlssUpgrade->setToolTip(
        "Replace the game's bundled DLSS DLLs with newer versions from Proton.\n\n"
        "Many older games ship with outdated DLSS versions that have lower quality "
        "or missing features. Enabling this uses Proton's updated DLSS libraries instead.\n\n"
        "Benefits:\n"
        "• Better image quality and performance\n"
        "• Access to newer DLSS features (RR, FG)\n"
        "• Bug fixes and improvements\n\n"
        "Recommended: Enabled for games released before 2024.\n\n"
        "Note: A few games may have compatibility issues with newer DLSS versions.");
    layout->addWidget(m_dlssUpgrade);

    connect(m_dlssUpgrade, &QCheckBox::toggled, this, &DLSSSettingsWidget::onSettingChanged);

    return group;
}

QGroupBox* DLSSSettingsWidget::createSmoothMotionGroup()
{
    QGroupBox* group = new QGroupBox("Smooth Motion / Frame Rate Control", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_enableSmoothMotion = new QCheckBox("Enable Smooth Motion", this);
    m_enableSmoothMotion->setToolTip(
        "Enable driver-level frame generation.\n\n"
        "Sets NVPRESENT_ENABLE_SMOOTH_MOTION=1 environment variable.\n");
    layout->addWidget(m_enableSmoothMotion);

    m_enableFrameRateLimit = new QCheckBox("Enable Frame Rate Limit", this);
    m_enableFrameRateLimit->setToolTip(
        "Limit the maximum frame rate for smoother, more consistent gameplay.\n\n"
        "Frame rate limiting can:\n"
        "• Reduce screen tearing\n"
        "• Lower GPU temperature and power consumption\n"
        "• Provide more consistent frame times\n"
        "• Reduce input latency spikes\n\n"
        "Uses DXVK_FRAME_RATE environment variable.\n\n"
        "Recommended: Enable if you experience tearing or want to cap FPS below your monitor's refresh rate.");
    layout->addWidget(m_enableFrameRateLimit);

    QHBoxLayout* fpsLayout = new QHBoxLayout();
    QLabel* fpsLabel = new QLabel("Target FPS:", this);
    fpsLayout->addWidget(fpsLabel);

    m_targetFrameRate = new QSpinBox(this);
    m_targetFrameRate->setRange(30, 500);
    m_targetFrameRate->setValue(60);
    m_targetFrameRate->setSuffix(" FPS");
    m_targetFrameRate->setToolTip(
        "Set the maximum frame rate limit.\n\n"
        "Common values:\n"
        "• 30 FPS - Console-like experience, very low power\n"
        "• 60 FPS - Standard smooth gaming\n"
        "• 120 FPS - High refresh rate gaming\n"
        "• 144 FPS - Match 144Hz monitor\n"
        "• 165/240 FPS - Match high-end monitors\n\n"
        "Set to match your monitor's refresh rate for best results.");
    fpsLayout->addWidget(m_targetFrameRate);
    fpsLayout->addStretch();

    layout->addLayout(fpsLayout);

    // Enable/disable FPS control based on checkbox
    auto updateFpsEnabled = [this]() {
        m_targetFrameRate->setEnabled(m_enableFrameRateLimit->isChecked());
    };

    connect(m_enableSmoothMotion, &QCheckBox::toggled, this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_enableFrameRateLimit, &QCheckBox::toggled, this, [updateFpsEnabled, this]() {
        updateFpsEnabled();
        onSettingChanged();
    });
    connect(m_targetFrameRate, QOverload<int>::of(&QSpinBox::valueChanged), this, &DLSSSettingsWidget::onSettingChanged);

    updateFpsEnabled();

    return group;
}

QWidget* DLSSSettingsWidget::createActionsSection()
{
    QWidget* widget = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 10, 0, 0);

    m_playButton = new QPushButton("Play", this);
    m_playButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 10px 20px; font-weight: bold; }");
    m_playButton->setToolTip("Launch game directly with DLSS settings via Proton");
    layout->addWidget(m_playButton);

    m_copyButton = new QPushButton("Copy to Clipboard", this);
    m_copyButton->setToolTip("Copy launch options to clipboard for manual paste into Steam");
    layout->addWidget(m_copyButton);

    m_writeToSteamButton = new QPushButton("Write to Steam", this);
    m_writeToSteamButton->setToolTip("Write launch options directly to Steam's config (requires Steam restart)");
    layout->addWidget(m_writeToSteamButton);

    layout->addStretch();

    connect(m_playButton, &QPushButton::clicked, this, &DLSSSettingsWidget::playClicked);
    connect(m_copyButton, &QPushButton::clicked, this, &DLSSSettingsWidget::copyClicked);
    connect(m_writeToSteamButton, &QPushButton::clicked, this, &DLSSSettingsWidget::writeToSteamClicked);

    return widget;
}

void DLSSSettingsWidget::setGame(const Game& game)
{
    m_currentGame = game;
    m_gameNameLabel->setText(game.name());

    // Platform badge
    if (game.isNativeLinux()) {
        m_platformBadge->setText("🐧 Native Linux");
        m_platformBadge->setStyleSheet(
            "font-size: 11px; font-weight: bold; padding: 4px 8px; "
            "border-radius: 3px; background-color: #28a745; color: white;");
        m_platformBadge->show();
    } else {
        m_platformBadge->setText("🪟 Windows");
        m_platformBadge->setStyleSheet(
            "font-size: 11px; font-weight: bold; padding: 4px 8px; "
            "border-radius: 3px; background-color: #0078d4; color: white;");
        m_platformBadge->show();
    }

    // Detect Proton version
    GameRunner runner;
    QString protonPath = runner.findProtonPath(game);
    QString protonVersion = "Proton: Not detected";

    if (!protonPath.isEmpty()) {
        // Extract version from path (e.g., /path/to/proton-cachyos-10.0-20260127-slr)
        QFileInfo protonInfo(protonPath);
        QString dirName = protonInfo.fileName();
        protonVersion = QString("Proton: %1").arg(dirName);
    }

    m_protonVersionLabel->setText(protonVersion);

    // Populate Proton version selector
    populateProtonVersionSelector();

    // Populate executable selector
    populateExecutableSelector(game);

    // Load image
    QPixmap pixmap = ImageCache::instance().getImage(game.imageUrl(), QSize(230, 107));
    m_gameImageLabel->setPixmap(pixmap);

    // Connect to image ready signal
    connect(&ImageCache::instance(), &ImageCache::imageReady, this, [this, game](const QString& url) {
        if (url == game.imageUrl()) {
            QPixmap pixmap = ImageCache::instance().getImage(game.imageUrl(), QSize(230, 107));
            m_gameImageLabel->setPixmap(pixmap);
        }
    });
}

void DLSSSettingsWidget::setGameRunning(bool running)
{
    if (running) {
        m_playButton->setText("Game is running...");
        m_playButton->setStyleSheet("QPushButton { background-color: #dc3545; color: white; padding: 10px 20px; font-weight: bold; }");
        m_playButton->setEnabled(false);
        m_playButton->setToolTip("Game is currently running");
    } else {
        m_playButton->setText("Play");
        m_playButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 10px 20px; font-weight: bold; }");
        m_playButton->setEnabled(true);
        m_playButton->setToolTip("Launch game directly with DLSS settings via Proton");
    }
}

void DLSSSettingsWidget::blockSignalsForAll(bool block)
{
    m_enableNVAPI->blockSignals(block);
    m_enableNGXUpdater->blockSignals(block);
    m_showIndicator->blockSignals(block);
    m_srOverride->blockSignals(block);
    m_srMode->blockSignals(block);
    m_srPreset->blockSignals(block);
    m_srScalingRatio->blockSignals(block);
    m_rrOverride->blockSignals(block);
    m_rrMode->blockSignals(block);
    m_rrPreset->blockSignals(block);
    m_rrScalingRatio->blockSignals(block);
    m_fgOverride->blockSignals(block);
    m_fgMultiFrameCount->blockSignals(block);
    m_dlssUpgrade->blockSignals(block);
    m_protonVersionSelector->blockSignals(block);
    m_enableSmoothMotion->blockSignals(block);
    m_enableFrameRateLimit->blockSignals(block);
    m_targetFrameRate->blockSignals(block);
}

void DLSSSettingsWidget::setSettings(const DLSSSettings& settings)
{
    blockSignalsForAll(true);

    // General
    m_enableNVAPI->setChecked(settings.enableNVAPI);
    m_enableNGXUpdater->setChecked(settings.enableNGXUpdater);
    m_showIndicator->setChecked(settings.showIndicator);

    // Super Resolution
    m_srOverride->setChecked(settings.srOverride);
    int srModeIndex = m_srMode->findData(settings.srMode);
    m_srMode->setCurrentIndex(srModeIndex >= 0 ? srModeIndex : 0);
    int srPresetIndex = m_srPreset->findData(settings.srPreset);
    m_srPreset->setCurrentIndex(srPresetIndex >= 0 ? srPresetIndex : 0);
    m_srScalingRatio->setValue(settings.srScalingRatio);

    // Update enabled state
    m_srMode->setEnabled(settings.srOverride);
    m_srPreset->setEnabled(settings.srOverride);
    m_srScalingRatio->setEnabled(settings.srOverride);

    // Ray Reconstruction
    m_rrOverride->setChecked(settings.rrOverride);
    int rrModeIndex = m_rrMode->findData(settings.rrMode);
    m_rrMode->setCurrentIndex(rrModeIndex >= 0 ? rrModeIndex : 0);
    int rrPresetIndex = m_rrPreset->findData(settings.rrPreset);
    m_rrPreset->setCurrentIndex(rrPresetIndex >= 0 ? rrPresetIndex : 0);
    m_rrScalingRatio->setValue(settings.rrScalingRatio);

    m_rrMode->setEnabled(settings.rrOverride);
    m_rrPreset->setEnabled(settings.rrOverride);
    m_rrScalingRatio->setEnabled(settings.rrOverride);

    // Frame Generation
    m_fgOverride->setChecked(settings.fgOverride);
    int fgIndex = m_fgMultiFrameCount->findData(settings.fgMultiFrameCount);
    m_fgMultiFrameCount->setCurrentIndex(fgIndex >= 0 ? fgIndex : 0);
    m_fgMultiFrameCount->setEnabled(settings.fgOverride);

    // DLSS Upgrade
    m_dlssUpgrade->setChecked(settings.dlssUpgrade);

    // Smooth Motion
    m_enableSmoothMotion->setChecked(settings.enableSmoothMotion);
    m_enableFrameRateLimit->setChecked(settings.enableFrameRateLimit);
    m_targetFrameRate->setValue(settings.targetFrameRate);
    m_targetFrameRate->setEnabled(settings.enableFrameRateLimit);

    // Restore saved executable selection
    if (!settings.executablePath.isEmpty()) {
        m_executableSelector->blockSignals(true);
        for (int i = 0; i < m_executableSelector->count(); ++i) {
            if (m_executableSelector->itemData(i).toString() == settings.executablePath) {
                m_executableSelector->setCurrentIndex(i);
                break;
            }
        }
        m_executableSelector->blockSignals(false);
    }

    // Restore saved Proton version selection
    QString protonVersionKey = settings.protonVersion.isEmpty() ? "auto" : settings.protonVersion;
    m_protonVersionSelector->blockSignals(true);
    int protonIndex = m_protonVersionSelector->findData(protonVersionKey);
    if (protonIndex >= 0) {
        m_protonVersionSelector->setCurrentIndex(protonIndex);
    } else {
        m_protonVersionSelector->setCurrentIndex(0);  // Default to "Latest Proton-CachyOS"
    }
    m_protonVersionSelector->blockSignals(false);

    blockSignalsForAll(false);

    // Update launch command preview
    updateLaunchCommand(EnvBuilder::buildLaunchOptions(settings));
}

DLSSSettings DLSSSettingsWidget::settings() const
{
    DLSSSettings settings;

    // General
    settings.enableNVAPI = m_enableNVAPI->isChecked();
    settings.enableNGXUpdater = m_enableNGXUpdater->isChecked();
    settings.showIndicator = m_showIndicator->isChecked();

    // Super Resolution
    settings.srOverride = m_srOverride->isChecked();
    settings.srMode = m_srMode->currentData().toString();
    settings.srPreset = m_srPreset->currentData().toString();
    settings.srScalingRatio = m_srScalingRatio->value();

    // Ray Reconstruction
    settings.rrOverride = m_rrOverride->isChecked();
    settings.rrMode = m_rrMode->currentData().toString();
    settings.rrPreset = m_rrPreset->currentData().toString();
    settings.rrScalingRatio = m_rrScalingRatio->value();

    // Frame Generation
    settings.fgOverride = m_fgOverride->isChecked();
    settings.fgMultiFrameCount = m_fgMultiFrameCount->currentData().toInt();

    // DLSS Upgrade
    settings.dlssUpgrade = m_dlssUpgrade->isChecked();

    // Smooth Motion
    settings.enableSmoothMotion = m_enableSmoothMotion->isChecked();
    settings.enableFrameRateLimit = m_enableFrameRateLimit->isChecked();
    settings.targetFrameRate = m_targetFrameRate->value();

    // Executable Selection
    if (m_executableSelector->currentIndex() >= 0) {
        settings.executablePath = m_executableSelector->currentData().toString();
    }

    // Proton Version Selection
    if (m_protonVersionSelector->currentIndex() >= 0) {
        QString protonVersion = m_protonVersionSelector->currentData().toString();
        // Don't save "auto" as it's the default
        if (protonVersion != "auto") {
            settings.protonVersion = protonVersion;
        }
    }

    return settings;
}

void DLSSSettingsWidget::updateLaunchCommand(const QString& command)
{
    m_launchCommandPreview->setText(command);
}

void DLSSSettingsWidget::onSettingChanged()
{
    DLSSSettings s = settings();
    updateLaunchCommand(EnvBuilder::buildLaunchOptions(s));
    emit settingsChanged(s);
}

void DLSSSettingsWidget::populateExecutableSelector(const Game& game)
{
    // Block signals to prevent saving during population
    m_executableSelector->blockSignals(true);
    m_executableSelector->clear();

    QStringList executables;
    if (game.isNativeLinux()) {
        executables = findLinuxExecutables(game.installPath());
    } else {
        executables = findWindowsExecutables(game.installPath());
    }

    if (executables.isEmpty()) {
        m_executableSelector->addItem("No executables found", "");
        m_executableSelector->setEnabled(false);
    } else {
        for (const QString& exe : executables) {
            QString displayName = QFileInfo(exe).fileName();
            m_executableSelector->addItem(displayName, exe);
        }
        m_executableSelector->setEnabled(true);

        // Select the best match by default
        QString bestMatch = findBestExecutable(game, executables);
        for (int i = 0; i < m_executableSelector->count(); ++i) {
            if (m_executableSelector->itemData(i).toString() == bestMatch) {
                m_executableSelector->setCurrentIndex(i);
                break;
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

void DLSSSettingsWidget::populateProtonVersionSelector()
{
    m_protonVersionSelector->blockSignals(true);
    m_protonVersionSelector->clear();

    // Add special options
    m_protonVersionSelector->addItem("Latest Proton-CachyOS (Recommended)", "auto");
    m_protonVersionSelector->addItem("Latest Proton-GE", "latest-ge");

    m_protonVersionSelector->insertSeparator(2);

    // Get installed Proton versions
    QString protonPath = ProtonManager::protonCachyOSPath();
    QDir dir(protonPath);

    if (dir.exists()) {
        QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);

        for (const QString& entry : entries) {
            // Check if it's a valid Proton installation
            QString protonExe = protonPath + "/" + entry + "/proton";
            if (QFile::exists(protonExe)) {
                QString displayName = entry;

                // Make display name more readable
                if (entry.startsWith("proton-cachyos-")) {
                    displayName = entry.mid(15);  // Remove "proton-cachyos-" prefix
                    displayName = "CachyOS " + displayName;
                } else if (entry.startsWith("GE-Proton")) {
                    displayName = entry.mid(3);  // Remove "GE-" prefix
                }

                m_protonVersionSelector->addItem(displayName, entry);
            }
        }
    }

    m_protonVersionSelector->blockSignals(false);
}

QStringList DLSSSettingsWidget::findWindowsExecutables(const QString& installPath) const
{
    QStringList executables;
    QDirIterator it(installPath, {"*.exe"}, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString path = it.next();
        QString filename = QFileInfo(path).fileName().toLower();

        // Skip common non-game executables
        if (filename.contains("unins") || filename.contains("setup") ||
            filename.contains("install") || filename.contains("crash") ||
            filename.contains("report") || filename.contains("redist") ||
            filename.contains("vcredist") || filename.contains("directx") ||
            filename.contains("dotnet") || filename.contains("dxsetup") ||
            filename == "ucc.exe") {  // UT2004 compiler, not the game
            continue;
        }

        executables << path;
    }

    // Sort by path depth (shallower first), then alphabetically
    std::sort(executables.begin(), executables.end(), [](const QString& a, const QString& b) {
        int depthA = a.count('/');
        int depthB = b.count('/');
        if (depthA != depthB) return depthA < depthB;
        return a < b;
    });

    return executables;
}

QStringList DLSSSettingsWidget::findLinuxExecutables(const QString& installPath) const
{
    QStringList executables;
    QDirIterator it(installPath, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString path = it.next();
        QString filename = QFileInfo(path).fileName().toLower();

        // Skip common non-game files
        if (filename.contains("uninstall") || filename.contains("setup") ||
            filename.endsWith(".sh") || filename.endsWith(".py") ||
            filename.endsWith(".so") || filename.contains("crash")) {
            continue;
        }

        executables << path;
    }

    // Sort by path depth (shallower first)
    std::sort(executables.begin(), executables.end(), [](const QString& a, const QString& b) {
        int depthA = a.count('/');
        int depthB = b.count('/');
        if (depthA != depthB) return depthA < depthB;
        return a < b;
    });

    return executables;
}

QString DLSSSettingsWidget::findBestExecutable(const Game& game, const QStringList& executables) const
{
    if (executables.isEmpty()) return QString();

    QString gameName = game.name().toLower();
    QString installDirName = QFileInfo(game.installPath()).fileName().toLower();

    // Remove common suffixes and clean up the name
    QString cleanName = gameName;
    cleanName.remove(QRegularExpression("\\s*\\([^)]*\\)"));  // Remove (Demo), (2004), etc.
    cleanName.remove(QRegularExpression("[^a-z0-9]"));  // Keep only alphanumeric

    // Try to find an executable that matches the game name
    for (const QString& exe : executables) {
        QString exeName = QFileInfo(exe).baseName().toLower();
        QString cleanExeName = exeName;
        cleanExeName.remove(QRegularExpression("[^a-z0-9]"));

        // Exact match with game name
        if (cleanExeName == cleanName || exeName == gameName) {
            return exe;
        }

        // Match with install directory
        if (exeName == installDirName) {
            return exe;
        }
    }

    // Try partial matches
    for (const QString& exe : executables) {
        QString exeName = QFileInfo(exe).baseName().toLower();

        // Partial match - executable contains significant part of game name
        if (cleanName.length() > 3 && exeName.contains(cleanName.left(cleanName.length() / 2))) {
            return exe;
        }
    }

    // Return the first executable as fallback
    return executables.first();
}
