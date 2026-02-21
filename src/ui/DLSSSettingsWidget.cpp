#include "DLSSSettingsWidget.h"
#include "network/ImageCache.h"
#include "utils/EnvBuilder.h"
#include "utils/ProtonManager.h"
#include "utils/HDRChecker.h"
#include "runner/GameRunner.h"
#include "launchers/SteamLauncher.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QMessageBox>
#include <QtConcurrent>
#include <algorithm>

DLSSSettingsWidget::DLSSSettingsWidget(QWidget* parent)
    : QWidget(parent)
    , m_executableWatcher(new QFutureWatcher<QStringList>(this))
{
    setupUI();

    // Connect executable watcher to update UI when search completes
    connect(m_executableWatcher, &QFutureWatcher<QStringList>::finished,
            this, [this]() {
        QStringList executables = m_executableWatcher->result();
        updateExecutableSelectorWithResults(executables);
    });
}

void DLSSSettingsWidget::setupUI()
{
    // Global dark theme stylesheet
    setStyleSheet(R"(
        DLSSSettingsWidget {
            background-color: #1a1a1a;
        }
        QGroupBox {
            background-color: #262626;
            border: 1px solid #4a4a4a;
            border-radius: 8px;
            margin-top: 14px;
            padding-top: 14px;
            font-weight: bold;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 2px 8px;
            color: #e0e0e0;
        }
        QComboBox {
            background-color: #1e1e1e;
            border: 1px solid #555;
            border-radius: 4px;
            padding: 4px 8px;
            color: #e0e0e0;
            combobox-popup: 0;
        }
        QComboBox:focus {
            border-color: #76B900;
        }
        QComboBox QAbstractItemView {
            background-color: #1e1e1e;
            border: 1px solid #555;
            color: #e0e0e0;
            selection-background-color: #76B900;
        }
        QSpinBox {
            background-color: #1e1e1e;
            border: 1px solid #555;
            border-radius: 4px;
            padding: 4px 8px;
            color: #e0e0e0;
        }
        QSpinBox:focus {
            border-color: #76B900;
        }
        QCheckBox {
            color: #d0d0d0;
            spacing: 6px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
            border: 1px solid #555;
            background-color: #1e1e1e;
        }
        QCheckBox::indicator:checked {
            background-color: #76B900;
            border-color: #76B900;
        }
        QLabel {
            color: #d0d0d0;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #555;
            border-radius: 4px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background: #6a6a6a;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollArea {
            border: none;
            background: transparent;
        }
    )");

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
    gameInfoLayout->addWidget(m_platformBadge);

    // Proton version selector (container widget for easy show/hide)
    m_protonSelectorContainer = new QWidget(headerCard);
    QHBoxLayout* protonLayout = new QHBoxLayout(m_protonSelectorContainer);
    protonLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* protonLabel = new QLabel("Proton:", m_protonSelectorContainer);
    protonLabel->setStyleSheet("font-size: 12px; color: #999;");
    protonLabel->setMinimumWidth(75);
    protonLayout->addWidget(protonLabel);

    m_protonVersionSelector = new QComboBox(m_protonSelectorContainer);
    m_protonVersionSelector->setStyleSheet("font-size: 11px;");
    m_protonVersionSelector->setToolTip("Select which Proton version to use");
    m_protonVersionSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_protonVersionSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DLSSSettingsWidget::onSettingChanged);
    protonLayout->addWidget(m_protonVersionSelector, 1);
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

    // Scroll area for settings
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* scrollContent = new QWidget();
    scrollContent->setStyleSheet("background: transparent;");
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
    QGroupBox* previewGroup = new QGroupBox("Steam Launch Options Preview", this);
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

    // HDR Settings with master checkbox
    layout->addSpacing(10);
    QLabel* hdrLabel = new QLabel("HDR Settings:", this);
    hdrLabel->setStyleSheet("font-weight: bold; margin-top: 5px;");
    layout->addWidget(hdrLabel);

    m_enableAllHDR = new QCheckBox("Enable All HDR Options (Quick Toggle)", this);
    m_enableAllHDR->setStyleSheet("font-weight: bold; color: #76B900;");
    m_enableAllHDR->setToolTip(
        "Quick toggle to enable/disable all HDR options at once.\n\n"
        "This is a convenience checkbox that controls all three HDR settings below. "
        "You can also toggle individual options if needed.");
    layout->addWidget(m_enableAllHDR);

    // Indent individual HDR options
    QWidget* hdrOptionsWidget = new QWidget(this);
    QVBoxLayout* hdrOptionsLayout = new QVBoxLayout(hdrOptionsWidget);
    hdrOptionsLayout->setContentsMargins(20, 0, 0, 0);
    hdrOptionsLayout->setSpacing(5);

    m_enableProtonWayland = new QCheckBox("PROTON_ENABLE_WAYLAND=1", this);
    m_enableProtonWayland->setToolTip(
        "Enable Wayland support in Proton.\n\n"
        "Required for HDR to work. Enables Wayland backend instead of XWayland.");
    hdrOptionsLayout->addWidget(m_enableProtonWayland);

    m_enableProtonHDR = new QCheckBox("PROTON_ENABLE_HDR=1", this);
    m_enableProtonHDR->setToolTip(
        "Enable HDR support in Proton.\n\n"
        "Enables High Dynamic Range rendering support in Proton.");
    hdrOptionsLayout->addWidget(m_enableProtonHDR);

    m_enableHDRWSI = new QCheckBox("ENABLE_HDR_WSI=1", this);
    m_enableHDRWSI->setToolTip(
        "Enable HDR Window System Integration.\n\n"
        "Enables HDR support in the Vulkan WSI (Window System Integration) layer.");
    hdrOptionsLayout->addWidget(m_enableHDRWSI);

    layout->addWidget(hdrOptionsWidget);

    connect(m_enableNVAPI, &QCheckBox::toggled, this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_enableNGXUpdater, &QCheckBox::toggled, this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_showIndicator, &QCheckBox::toggled, this, &DLSSSettingsWidget::onSettingChanged);
    connect(m_enableAllHDR, &QCheckBox::toggled, this, &DLSSSettingsWidget::onEnableAllHDRToggled);
    connect(m_enableProtonWayland, &QCheckBox::toggled, this, &DLSSSettingsWidget::onHDRCheckboxChanged);
    connect(m_enableProtonHDR, &QCheckBox::toggled, this, &DLSSSettingsWidget::onHDRCheckboxChanged);
    connect(m_enableHDRWSI, &QCheckBox::toggled, this, &DLSSSettingsWidget::onHDRCheckboxChanged);

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
    m_playButton->setStyleSheet(
        "QPushButton { background-color: #76B900; color: white; padding: 10px 20px; "
        "font-weight: bold; border-radius: 6px; border: none; }"
        "QPushButton:hover { background-color: #8fd400; }");
    m_playButton->setToolTip("Launch game directly with DLSS settings via Proton");
    layout->addWidget(m_playButton);

    m_copyButton = new QPushButton("Copy to Clipboard", this);
    m_copyButton->setStyleSheet(
        "QPushButton { background-color: #2e2e2e; color: #e0e0e0; padding: 10px 20px; "
        "border: 1px solid #4a4a4a; border-radius: 6px; }"
        "QPushButton:hover { background-color: #383838; }");
    m_copyButton->setToolTip("Copy launch options to clipboard for manual paste into Steam");
    layout->addWidget(m_copyButton);

    m_writeToSteamButton = new QPushButton("Write to Steam", this);
    m_writeToSteamButton->setStyleSheet(
        "QPushButton { background-color: #2e2e2e; color: #e0e0e0; padding: 10px 20px; "
        "border: 1px solid #4a4a4a; border-radius: 6px; }"
        "QPushButton:hover { background-color: #383838; }");
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
            "border-radius: 4px; background-color: #e8710a; color: white;");
        m_platformBadge->show();
    } else {
        m_platformBadge->setText("🪟 Windows");
        m_platformBadge->setStyleSheet(
            "font-size: 11px; font-weight: bold; padding: 4px 8px; "
            "border-radius: 4px; background-color: #1565c0; color: white;");
        m_platformBadge->show();
    }

    // Show/hide Proton version selector based on game type
    if (game.isNativeLinux()) {
        // Hide Proton-specific UI for native Linux games
        m_protonSelectorContainer->hide();
    } else {
        // Show and populate Proton UI for Windows games
        m_protonSelectorContainer->show();
        populateProtonVersionSelector();
    }

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
        m_playButton->setStyleSheet(
            "QPushButton { background-color: #c0392b; color: white; padding: 10px 20px; "
            "font-weight: bold; border-radius: 6px; border: none; }");
        m_playButton->setEnabled(false);
        m_playButton->setToolTip("Game is currently running");
    } else {
        m_playButton->setText("Play");
        m_playButton->setStyleSheet(
            "QPushButton { background-color: #76B900; color: white; padding: 10px 20px; "
            "font-weight: bold; border-radius: 6px; border: none; }"
            "QPushButton:hover { background-color: #8fd400; }");
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

    // HDR
    m_enableProtonWayland->setChecked(settings.enableProtonWayland);
    m_enableProtonHDR->setChecked(settings.enableProtonHDR);
    m_enableHDRWSI->setChecked(settings.enableHDRWSI);
    // Update master checkbox state based on individual checkboxes
    m_enableAllHDR->setChecked(settings.enableProtonWayland &&
                               settings.enableProtonHDR &&
                               settings.enableHDRWSI);

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

    // Save executable path for later restoration after async search completes
    m_savedExecutablePath = settings.executablePath;

    // Try to restore immediately (will only work if search already completed)
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

    // HDR
    settings.enableProtonWayland = m_enableProtonWayland->isChecked();
    settings.enableProtonHDR = m_enableProtonHDR->isChecked();
    settings.enableHDRWSI = m_enableHDRWSI->isChecked();

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

void DLSSSettingsWidget::onEnableAllHDRToggled(bool checked)
{
    // If enabling HDR, check system HDR status first
    if (checked && !checkAndWarnHDRStatus()) {
        // User canceled or chose not to enable HDR
        m_enableAllHDR->blockSignals(true);
        m_enableAllHDR->setChecked(false);
        m_enableAllHDR->blockSignals(false);
        return;
    }

    // Block signals to prevent recursive updates
    m_enableProtonWayland->blockSignals(true);
    m_enableProtonHDR->blockSignals(true);
    m_enableHDRWSI->blockSignals(true);

    // Set all three HDR options to the same state
    m_enableProtonWayland->setChecked(checked);
    m_enableProtonHDR->setChecked(checked);
    m_enableHDRWSI->setChecked(checked);

    // Restore signals
    m_enableProtonWayland->blockSignals(false);
    m_enableProtonHDR->blockSignals(false);
    m_enableHDRWSI->blockSignals(false);

    // Trigger settings update
    onSettingChanged();
}

void DLSSSettingsWidget::onHDRCheckboxChanged()
{
    qDebug() << "onHDRCheckboxChanged() called";

    // Check if any HDR option is currently being enabled
    bool anyHDREnabled = m_enableProtonWayland->isChecked() ||
                         m_enableProtonHDR->isChecked() ||
                         m_enableHDRWSI->isChecked();

    // Get the sender to check which checkbox changed
    QCheckBox* senderCheckbox = qobject_cast<QCheckBox*>(sender());
    qDebug() << "Sender:" << (senderCheckbox ? senderCheckbox->text() : "null")
             << "Checked:" << (senderCheckbox ? senderCheckbox->isChecked() : false);

    // If any HDR option is being enabled, check system HDR status
    if (senderCheckbox && senderCheckbox->isChecked() && anyHDREnabled) {
        if (!checkAndWarnHDRStatus()) {
            // User canceled, uncheck the checkbox
            senderCheckbox->blockSignals(true);
            senderCheckbox->setChecked(false);
            senderCheckbox->blockSignals(false);

            // Don't update settings
            return;
        }
    }

    // Update master checkbox state based on individual checkboxes
    bool allChecked = m_enableProtonWayland->isChecked() &&
                      m_enableProtonHDR->isChecked() &&
                      m_enableHDRWSI->isChecked();

    // Block signal to prevent recursive call
    m_enableAllHDR->blockSignals(true);
    m_enableAllHDR->setChecked(allChecked);
    m_enableAllHDR->blockSignals(false);

    // Trigger settings update
    onSettingChanged();
}

void DLSSSettingsWidget::populateExecutableSelector(const Game& game)
{
    // Show loading state
    m_executableSelector->blockSignals(true);
    m_executableSelector->clear();
    m_executableSelector->addItem("Searching for executables...", "");
    m_executableSelector->setEnabled(false);
    m_executableSelector->blockSignals(false);

    // Cancel any previous search
    if (m_executableWatcher->isRunning()) {
        m_executableWatcher->cancel();
        m_executableWatcher->waitForFinished();
    }

    // Start async search in background thread
    QString installPath = game.installPath();
    bool isLinux = game.isNativeLinux();

    QFuture<QStringList> future = QtConcurrent::run([this, installPath, isLinux]() {
        if (isLinux) {
            return findLinuxExecutables(installPath);
        } else {
            return findWindowsExecutables(installPath);
        }
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
            QString bestMatch = findBestExecutable(m_currentGame, executables);
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

void DLSSSettingsWidget::populateProtonVersionSelector()
{
    m_protonVersionSelector->blockSignals(true);
    m_protonVersionSelector->clear();

    // Add special options
    m_protonVersionSelector->addItem("Latest Proton-CachyOS (Recommended)", "auto");
    m_protonVersionSelector->addItem("Latest Proton-GE", "latest-ge");
    m_protonVersionSelector->addItem("Latest Steam Proton", "steam-proton");

    m_protonVersionSelector->insertSeparator(3);

    // Get installed Proton versions from compatibilitytools.d
    QString protonPath = ProtonManager::protonCachyOSPath();
    QDir dir(protonPath);

    QMap<QString, QString> cachyosVersions;  // displayName -> directoryPath
    QMap<QString, QString> geVersions;  // displayName -> directoryPath
    QList<QPair<QString, QString>> steamProtonVersions;  // List of (entry, fullPath) pairs

    if (dir.exists()) {
        QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);

        for (const QString& entry : entries) {
            // Check if it's a valid Proton installation
            QString protonExe = protonPath + "/" + entry + "/proton";
            if (QFile::exists(protonExe)) {
                if (entry.startsWith("proton-cachyos-")) {
                    QString displayName = entry.mid(15);  // Remove "proton-cachyos-" prefix
                    cachyosVersions[displayName] = entry;
                } else if (entry.startsWith("GE-Proton")) {
                    QString displayName = entry.mid(3);  // Remove "GE-" prefix
                    geVersions[displayName] = entry;
                }
            }
        }
    }

    // Get Steam Proton versions from Steam libraries
    QStringList libraryPaths = SteamLauncher::libraryPaths();
    QStringList preferredSteamVersions = {"Proton - Experimental", "Proton 10", "Proton 9", "Proton 8", "Proton 7", "Proton Hotfix", "Proton 6", "Proton 5"};

    for (const QString& preferred : preferredSteamVersions) {
        bool found = false;
        for (const QString& libPath : libraryPaths) {
            if (found) break;

            QString commonPath = libPath + "/common";
            QDir steamDir(commonPath);

            if (steamDir.exists()) {
                QStringList entries = steamDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

                for (const QString& entry : entries) {
                    if (entry.contains(preferred, Qt::CaseInsensitive)) {
                        QString protonExe = commonPath + "/" + entry + "/proton";
                        if (QFile::exists(protonExe)) {
                            QString fullPath = commonPath + "/" + entry;
                            steamProtonVersions.append(qMakePair(entry, fullPath));
                            found = true;
                            break;
                        }
                    }
                }
            }
        }

        // Limit to 3 newest Steam Proton versions
        if (steamProtonVersions.size() >= 3) {
            break;
        }
    }

    // Add CachyOS versions (sorted by version number, newest first)
    if (!cachyosVersions.isEmpty()) {
        QStringList sortedCachyos = cachyosVersions.keys();
        sortedCachyos.sort(Qt::CaseInsensitive);
        std::reverse(sortedCachyos.begin(), sortedCachyos.end());

        for (const QString& displayName : sortedCachyos) {
            m_protonVersionSelector->addItem(displayName, cachyosVersions[displayName]);
        }

        m_protonVersionSelector->insertSeparator(m_protonVersionSelector->count());
    }

    // Add GE-Proton versions (sorted by version number, newest first)
    if (!geVersions.isEmpty()) {
        QStringList sortedGe = geVersions.keys();
        sortedGe.sort(Qt::CaseInsensitive);
        std::reverse(sortedGe.begin(), sortedGe.end());

        for (const QString& displayName : sortedGe) {
            m_protonVersionSelector->addItem(displayName, geVersions[displayName]);
        }

        m_protonVersionSelector->insertSeparator(m_protonVersionSelector->count());
    }

    // Add Steam Proton versions (limited to 3 newest)
    if (!steamProtonVersions.isEmpty()) {
        for (const auto& pair : steamProtonVersions) {
            m_protonVersionSelector->addItem(pair.first, pair.second);
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

bool DLSSSettingsWidget::isElfExecutable(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    // Read ELF header (first 20 bytes is enough)
    QByteArray header = file.read(20);
    file.close();

    if (header.size() < 20) {
        return false;
    }

    // Check ELF magic bytes: 0x7f 'E' 'L' 'F'
    if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F') {
        return false;
    }

    // Byte 16 is e_type (object file type)
    // ET_EXEC = 2 (executable file)
    // ET_DYN = 3 (shared object file)
    unsigned char e_type_low = static_cast<unsigned char>(header[16]);
    unsigned char e_type_high = static_cast<unsigned char>(header[17]);

    // Check for little-endian ET_EXEC (2)
    if (e_type_low == 2 && e_type_high == 0) {
        return true;
    }

    // Check for big-endian ET_EXEC (2)
    if (e_type_low == 0 && e_type_high == 2) {
        return true;
    }

    // ET_DYN (3) can be either PIE executable or shared library
    // Modern executables are often PIE (Position Independent Executable) which shows as ET_DYN
    // We accept ET_DYN as well, but will filter .so files by extension earlier
    if ((e_type_low == 3 && e_type_high == 0) || (e_type_low == 0 && e_type_high == 3)) {
        // Only accept if it doesn't have .so extension (already filtered earlier)
        return true;
    }

    return false;
}

QStringList DLSSSettingsWidget::findLinuxExecutables(const QString& installPath) const
{
    QStringList executables;
    QDirIterator it(installPath, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fileInfo(path);
        QString filename = fileInfo.fileName().toLower();

        // Skip files with extensions that are definitely not executables
        if (filename.endsWith(".txt") || filename.endsWith(".log") || filename.endsWith(".md") ||
            filename.endsWith(".json") || filename.endsWith(".xml") || filename.endsWith(".cfg") ||
            filename.endsWith(".ini") || filename.endsWith(".conf") || filename.endsWith(".yaml") ||
            filename.endsWith(".dat") || filename.endsWith(".pak") || filename.endsWith(".csv") ||
            filename.endsWith(".sh") || filename.endsWith(".py") || filename.endsWith(".pl") ||
            filename.endsWith(".so") || filename.endsWith(".a") || filename.endsWith(".o") ||
            filename.endsWith(".png") || filename.endsWith(".jpg") || filename.endsWith(".jpeg") ||
            filename.endsWith(".bmp") || filename.endsWith(".svg") || filename.endsWith(".ico") ||
            filename.endsWith(".ttf") || filename.endsWith(".otf") || filename.endsWith(".woff") ||
            filename.endsWith(".mp3") || filename.endsWith(".ogg") || filename.endsWith(".wav") ||
            filename.endsWith(".mp4") || filename.endsWith(".avi") || filename.endsWith(".mkv")) {
            continue;
        }

        // Skip common non-game files by name
        if (filename.contains("uninstall") || filename.contains("setup") ||
            filename.contains("install") || filename.contains("update") ||
            filename.contains("crash") || filename.contains("report") ||
            filename.contains("readme") || filename.contains("license") ||
            filename.contains("changelog")) {
            continue;
        }

        // Only include files that are actually executable
        if (!fileInfo.isExecutable()) {
            continue;
        }

        // Check if it's actually an ELF executable by reading the header
        if (!isElfExecutable(path)) {
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

bool DLSSSettingsWidget::checkAndWarnHDRStatus()
{
    // Debug: Show that this method is called
    qDebug() << "checkAndWarnHDRStatus() called";

    // Check HDR status
    HDRChecker::HDRStatus status = HDRChecker::checkHDRStatus();
    qDebug() << "HDR Status - isEnabled:" << status.isEnabled << "message:" << status.message;

    // If HDR is enabled, no warning needed
    if (status.isEnabled) {
        return true;
    }

    // HDR is not enabled - show warning
    QString warningMessage = HDRChecker::getWarningMessage(status);

    if (warningMessage.isEmpty()) {
        // No warning needed (shouldn't happen, but handle gracefully)
        return true;
    }

    // Show warning dialog
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle("HDR Not Enabled");
    msgBox.setText(warningMessage);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    int result = msgBox.exec();

    // Return true if user wants to proceed anyway
    return result == QMessageBox::Yes;
}
