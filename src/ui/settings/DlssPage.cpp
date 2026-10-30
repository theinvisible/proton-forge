#include "ui/settings/DlssPage.h"

#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

namespace {

// Where the slider starts when CUSTOM is picked without a ratio of its own:
// about what Quality renders at.
constexpr int DefaultScalingRatio = 67;

QString scalingText(int ratio)
{
    return QStringLiteral("%1 %").arg(ratio);
}

enum class PresetKind { SuperResolution, RayReconstruction, FrameGeneration };

// Display text for a render preset. The stored value stays the NGX name
// (RENDER_PRESET_K), so settings and launch strings are unaffected by wording.
QString presetLabel(PresetKind kind, const QString& value)
{
    if (value.isEmpty())
        return QStringLiteral("(App Default)");
    // DLSS 310 redefined "latest" as "recommended": the driver picks the
    // preset NVIDIA recommends for the quality mode in use.
    if (value == QLatin1String("RENDER_PRESET_LATEST"))
        return QStringLiteral("Recommended (driver picks per mode)");

    static const QHash<QString, QString> sr = {
        {"RENDER_PRESET_M", "M — DLSS 4.5 (default: Performance)"},
        {"RENDER_PRESET_L", "L — DLSS 4.5 (default: Ultra Performance)"},
        {"RENDER_PRESET_K", "K — Transformer (default: DLAA / Quality / Balanced)"},
        {"RENDER_PRESET_J", "J — Transformer, 1st gen (similar to K)"},
        {"RENDER_PRESET_F", "F — CNN (deprecated)"},
        {"RENDER_PRESET_E", "E — CNN (deprecated)"},
        {"RENDER_PRESET_D", "D — CNN (removed in DLSS 310)"},
        {"RENDER_PRESET_C", "C — CNN (removed in DLSS 310)"},
        {"RENDER_PRESET_B", "B — CNN (removed in DLSS 310)"},
        {"RENDER_PRESET_A", "A — CNN (removed in DLSS 310)"},
    };
    static const QHash<QString, QString> rr = {
        {"RENDER_PRESET_F", "F — Ray Reconstruction 2 (DLSS 310.9.1+)"},
        {"RENDER_PRESET_E", "E — Transformer, latest (needed for the depth-of-field guide)"},
        {"RENDER_PRESET_D", "D — Transformer"},
    };
    static const QHash<QString, QString> fg = {
        {"RENDER_PRESET_C", "C — Performance Mode (beta)"},
        {"RENDER_PRESET_B", "B — UI recomposition (DLSS 310.6.0+)"},
        {"RENDER_PRESET_A", "A — Original"},
    };
    switch (kind) {
        case PresetKind::SuperResolution:   return sr.value(value, value);
        case PresetKind::RayReconstruction: return rr.value(value, value);
        case PresetKind::FrameGeneration:   return fg.value(value, value);
    }
    return value;
}

// Marks an item selectData() added for a value the list does not offer.
constexpr int SavedValueRole = Qt::UserRole + 1;

// Selects the item carrying `value`. A non-empty value the combo does not
// offer — a preset dropped from the list, or one written by hand — gets an
// item of its own instead of silently turning into "(App Default)", which
// the next save would then have made permanent.
void selectData(QComboBox* combo, const QString& value)
{
    for (int i = combo->count() - 1; i >= 0; --i) {
        if (combo->itemData(i, SavedValueRole).toBool())
            combo->removeItem(i);
    }
    int index = combo->findData(value);
    if (index < 0 && !value.isEmpty()) {
        combo->addItem(QStringLiteral("%1 (saved, not in list)").arg(value), value);
        index = combo->count() - 1;
        combo->setItemData(index, true, SavedValueRole);
    }
    combo->setCurrentIndex(index >= 0 ? index : 0);
}
}  // namespace

DlssPage::DlssPage(QWidget* parent)
    : SettingsPage(parent)
{
    pageLayout()->addWidget(createSuperResolutionGroup());
    pageLayout()->addWidget(createRayReconstructionGroup());
    pageLayout()->addWidget(createFrameGenerationGroup());
    pageLayout()->addWidget(createUpgradeGroup());
    pageLayout()->addWidget(createSmoothMotionGroup());

    // Every DLSS override travels in DXVK_NVAPI_DRS_SETTINGS, which only
    // DXVK-NVAPI reads — the whole group, title included, greys out on a
    // native game. Smooth Motion is driver level and stays live; the frame
    // cap is DXVK/VKD3D only.
    gate(m_srGroup, "DXVK_NVAPI_DRS_SETTINGS");
    gate(m_rrGroup, "DXVK_NVAPI_DRS_SETTINGS");
    gate(m_fgGroup, "DXVK_NVAPI_DRS_SETTINGS");
    gate(m_upgradeGroup, "PROTON_DLSS_UPGRADE");
    gate(m_enableFrameRateLimit, "DXVK_CONFIG");
    gate(m_targetFrameRate, "DXVK_CONFIG");
    gate(m_targetFrameRateLabel, "DXVK_CONFIG");
}

QGroupBox* DlssPage::createSuperResolutionGroup()
{
    m_srGroup = new QGroupBox("Super Resolution (DLSS SR)", this);
    QGroupBox* group = m_srGroup;
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
        "• CUSTOM: render at the scaling ratio set below");
    formLayout->addRow("Mode:", m_srMode);

    m_srPreset = new QComboBox(this);
    for (const QString& preset : DLSSSettings::availablePresets()) {
        m_srPreset->addItem(presetLabel(PresetKind::SuperResolution, preset), preset);
    }
    m_srPreset->setToolTip(
        "DLSS Super Resolution model preset (NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION).\n\n"
        "Picks the AI model variant:\n\n"
        "• (App Default): use the game's choice\n"
        "• Recommended (RENDER_PRESET_LATEST): the driver picks NVIDIA's "
        "recommendation for the quality mode — K for DLAA/Quality/Balanced, "
        "M for Performance, L for Ultra Performance\n\n"
        "Transformer models (DLSS 4 / 4.5):\n"
        "• M, L: DLSS 4.5, tuned for Performance / Ultra Performance\n"
        "• K: refined transformer, the default for DLAA, Quality and Balanced\n"
        "• J: 1st-gen transformer, similar to K\n\n"
        "CNN models (DLSS 2/3):\n"
        "• E, F: deprecated in DLSS 310\n"
        "• A–D: removed in DLSS 310 — only an older DLL still has them\n\n"
        "A preset the game's DLSS DLL does not know falls back to its default, "
        "so J–M need a DLSS 4 DLL: use DLSS Upgrade or the NGX updater for "
        "games that ship an older one.");
    formLayout->addRow("Render Preset:", m_srPreset);

    formLayout->addRow("Scaling Ratio:", createScalingRow(m_srScalingRatio, m_srScalingValue,
        "Render resolution in percent of the output resolution (33-100 %). "
        "Only with Mode CUSTOM — no other mode reads it.\n\n"
        "Lower = more performance, potentially lower quality.\n"
        "Higher = better quality, less performance gain.\n\n"
        "Examples at 4K:\n"
        "• 50 % = render at 1080p, upscale to 4K\n"
        "• 67 % = render at ~1440p, upscale to 4K\n\n"
        "Sets NGX_DLSS_SR_OVERRIDE_SCALING_RATIO. Needs Valve Proton 10.0-2, "
        "GE-Proton 10-4 or Proton-CachyOS 10.0-20250601 or newer."));

    layout->addLayout(formLayout);

    connect(m_srOverride, &QCheckBox::toggled, this, [this]() {
        updateSuperResolutionEnabled();
        emit changed();
    });
    connect(m_srMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        updateSuperResolutionEnabled();
        emit changed();
    });
    connect(m_srPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsPage::changed);

    updateSuperResolutionEnabled();

    return group;
}

QWidget* DlssPage::createScalingRow(QSlider*& slider, QLabel*& value, const QString& tooltip)
{
    auto* row = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(33, 100);
    slider->setSingleStep(1);
    slider->setPageStep(5);
    slider->setValue(DefaultScalingRatio);
    slider->setToolTip(tooltip);

    value = new QLabel(scalingText(DefaultScalingRatio), row);
    // Wide enough for "100 %", so the slider does not jump while dragging.
    value->setMinimumWidth(value->fontMetrics().horizontalAdvance(scalingText(100)) + 4);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setToolTip(tooltip);

    rowLayout->addWidget(slider, 1);
    rowLayout->addWidget(value);

    QLabel* label = value;
    connect(slider, &QSlider::valueChanged, this, [this, label](int ratio) {
        label->setText(scalingText(ratio));
        emit changed();
    });
    return row;
}

void DlssPage::updateSuperResolutionEnabled()
{
    const bool on = m_srOverride->isChecked();
    const bool custom = on && DLSSSettings::isCustomMode(m_srMode->currentData().toString());
    m_srMode->setEnabled(on);
    m_srPreset->setEnabled(on);
    m_srScalingRatio->setEnabled(custom);
    m_srScalingValue->setEnabled(custom);
}

void DlssPage::updateRayReconstructionEnabled()
{
    const bool on = m_rrOverride->isChecked();
    const bool custom = on && DLSSSettings::isCustomMode(m_rrMode->currentData().toString());
    m_rrMode->setEnabled(on);
    m_rrPreset->setEnabled(on);
    m_rrScalingRatio->setEnabled(custom);
    m_rrScalingValue->setEnabled(custom);
}

QGroupBox* DlssPage::createRayReconstructionGroup()
{
    m_rrGroup = new QGroupBox("Ray Reconstruction (DLSS RR)", this);
    QGroupBox* group = m_rrGroup;
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
        "• DLAA: Full ray count with AI denoising\n"
        "• CUSTOM: render at the scaling ratio set below\n\n"
        "Higher quality modes trace more rays but have lower performance impact.");
    formLayout->addRow("Mode:", m_rrMode);

    m_rrPreset = new QComboBox(this);
    for (const QString& preset : DLSSSettings::availableRRPresets()) {
        m_rrPreset->addItem(presetLabel(PresetKind::RayReconstruction, preset), preset);
    }
    m_rrPreset->setToolTip(
        "DLSS Ray Reconstruction model preset (NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION).\n\n"
        "Ray Reconstruction has its own presets, separate from Super Resolution:\n"
        "• (App Default): use the game's choice\n"
        "• Recommended (RENDER_PRESET_LATEST): the driver's pick\n"
        "• F: Ray Reconstruction 2 — needs DLSS 310.9.1 or newer\n"
        "• E: latest transformer — required when the game supplies a depth-of-field guide\n"
        "• D: transformer\n\n"
        "A preset the game's DLL does not know falls back to its default — "
        "use DLSS Upgrade or the NGX updater for F.");
    formLayout->addRow("Render Preset:", m_rrPreset);

    formLayout->addRow("Scaling Ratio:", createScalingRow(m_rrScalingRatio, m_rrScalingValue,
        "Render resolution Ray Reconstruction works from, in percent of the output "
        "resolution (33-100 %). Only with Mode CUSTOM — no other mode reads it.\n\n"
        "Lower = more performance, at the cost of reconstruction quality.\n\n"
        "Sets NGX_DLSS_RR_OVERRIDE_SCALING_RATIO. Needs Valve Proton 10.0-2, "
        "GE-Proton 10-4 or Proton-CachyOS 10.0-20250601 or newer."));

    layout->addLayout(formLayout);

    connect(m_rrOverride, &QCheckBox::toggled, this, [this]() {
        updateRayReconstructionEnabled();
        emit changed();
    });
    connect(m_rrMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        updateRayReconstructionEnabled();
        emit changed();
    });
    connect(m_rrPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsPage::changed);

    updateRayReconstructionEnabled();

    return group;
}

QGroupBox* DlssPage::createFrameGenerationGroup()
{
    m_fgGroup = new QGroupBox("Frame Generation (DLSS FG)", this);
    QGroupBox* group = m_fgGroup;
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
    m_fgMultiFrameCount->addItem("5x Frame Generation", 4);
    m_fgMultiFrameCount->addItem("6x Frame Generation", 5);
    m_fgMultiFrameCount->setToolTip(
        "Number of AI-generated frames inserted between each rendered frame:\n\n"
        "• 0 (App Default): Let the game decide\n"
        "• 1 (2x): Generate 1 frame → 2x total FPS\n"
        "• 2 (3x): Generate 2 frames → 3x total FPS\n"
        "• 3 (4x): Generate 3 frames → 4x total FPS\n"
        "• 4 (5x): Generate 4 frames → 5x total FPS\n"
        "• 5 (6x): Generate 5 frames → 6x total FPS\n\n"
        "Higher values give more FPS but increase input latency. "
        "DLSS 3.5+ required for 3x/4x; 5x/6x need a very recent DXVK-NVAPI/driver "
        "and may be ignored if unsupported.\n\n"
        "Example: 60 rendered FPS + 2x FG = 120 displayed FPS");
    formLayout->addRow("Multi-Frame Count:", m_fgMultiFrameCount);

    m_fgMode = new QComboBox(this);
    for (const QString& mode : DLSSSettings::availableFGModes()) {
        m_fgMode->addItem(mode.isEmpty() ? "(App Default)" : mode, mode);
    }
    m_fgMode->setToolTip(
        "DLSS Frame Generation mode (NGX_DLSSG_MODE):\n\n"
        "• (App Default): Let the game decide\n"
        "• On / Off: Force FG enabled or disabled\n"
        "• Auto: Driver decides based on conditions\n"
        "• Dynamic: Dynamically scale generated frames toward a target frame rate "
        "(RTX 50) — set by the two Dynamic fields below\n\n"
        "Requires the FG override above to be enabled.");
    formLayout->addRow("FG Mode:", m_fgMode);

    const QString dynamicNote = QStringLiteral(
        "\n\nOnly used with FG Mode \"Dynamic\": Dynamic Multi-Frame Generation "
        "changes the number of generated frames on the fly to reach the target "
        "frame rate. RTX 50 only, DXVK-NVAPI ≥ 0.9.2.\n\n"
        "Support under Proton is still incomplete — if the game ignores it, use "
        "a fixed Multi-Frame Count instead.");

    m_fgDynamicTargetFps = new QSpinBox(this);
    m_fgDynamicTargetFps->setRange(0, 1000);
    m_fgDynamicTargetFps->setSpecialValueText("(Driver Default)");
    m_fgDynamicTargetFps->setSuffix(" FPS");
    m_fgDynamicTargetFps->setToolTip(
        "Frame rate Dynamic Multi-Frame Generation aims for "
        "(NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE) — usually your monitor's refresh rate."
        + dynamicNote);
    m_fgDynamicTargetFpsLabel = new QLabel("Dynamic Target:", this);
    formLayout->addRow(m_fgDynamicTargetFpsLabel, m_fgDynamicTargetFps);

    m_fgDynamicMaxCount = new QComboBox(this);
    m_fgDynamicMaxCount->addItem("(Driver Default)", 0);
    m_fgDynamicMaxCount->addItem("up to 2x", 1);
    m_fgDynamicMaxCount->addItem("up to 3x", 2);
    m_fgDynamicMaxCount->addItem("up to 4x", 3);
    m_fgDynamicMaxCount->addItem("up to 5x", 4);
    m_fgDynamicMaxCount->addItem("up to 6x", 5);
    m_fgDynamicMaxCount->setToolTip(
        "The most frames Dynamic Multi-Frame Generation may generate "
        "(NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX)." + dynamicNote);
    m_fgDynamicMaxCountLabel = new QLabel("Dynamic Max:", this);
    formLayout->addRow(m_fgDynamicMaxCountLabel, m_fgDynamicMaxCount);

    m_fgPreset = new QComboBox(this);
    for (const QString& preset : DLSSSettings::availableFGPresets()) {
        m_fgPreset->addItem(presetLabel(PresetKind::FrameGeneration, preset), preset);
    }
    m_fgPreset->setToolTip(
        "DLSS Frame Generation model preset (NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION).\n\n"
        "Frame Generation only exposes a couple of real presets:\n"
        "• (App Default): use the game's choice\n"
        "• Recommended (RENDER_PRESET_LATEST): the driver's pick\n"
        "• C: Performance Mode (beta) — a lighter optical flow model, up to\n"
        "  35% faster, some loss in image quality; RTX 40/50. NVIDIA has not\n"
        "  said which DLSS version brings it — an older DLL ignores it\n"
        "• B: UI recomposition — keeps HUD and text cleaner (DLSS 310.6.0+)\n"
        "• A: original FG model\n\n"
        "Requires the FG override above to be enabled.");
    formLayout->addRow("FG Render Preset:", m_fgPreset);

    layout->addLayout(formLayout);

    connect(m_fgOverride, &QCheckBox::toggled, this, [this]() {
        updateFrameGenEnabled();
        emit changed();
    });
    connect(m_fgMultiFrameCount, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsPage::changed);
    connect(m_fgMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        updateFrameGenEnabled();
        emit changed();
    });
    connect(m_fgPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsPage::changed);
    connect(m_fgDynamicTargetFps, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsPage::changed);
    connect(m_fgDynamicMaxCount, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsPage::changed);

    updateFrameGenEnabled();

    return group;
}

void DlssPage::updateFrameGenEnabled()
{
    const bool enabled = m_fgOverride->isChecked();
    m_fgMultiFrameCount->setEnabled(enabled);
    m_fgMode->setEnabled(enabled);
    m_fgPreset->setEnabled(enabled);

    const bool dynamic = enabled && m_fgMode->currentData().toString() == QLatin1String("DYNAMIC");
    m_fgDynamicTargetFps->setEnabled(dynamic);
    m_fgDynamicTargetFpsLabel->setEnabled(dynamic);
    m_fgDynamicMaxCount->setEnabled(dynamic);
    m_fgDynamicMaxCountLabel->setEnabled(dynamic);
}

QGroupBox* DlssPage::createUpgradeGroup()
{
    m_upgradeGroup = new QGroupBox("DLSS Upgrade", this);
    QGroupBox* group = m_upgradeGroup;
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
        "Note: A few games may have compatibility issues with newer DLSS versions.\n\n"
        "Requires: Proton-CachyOS or GE-Proton 10-26+. Valve's Proton has no "
        "equivalent — use the NGX updater there.");
    layout->addWidget(m_dlssUpgrade);

    QHBoxLayout* versionRow = new QHBoxLayout();
    QLabel* versionLabel = new QLabel("DLSS version:", this);
    versionRow->addWidget(versionLabel);
    m_dlssVersion = new QLineEdit(this);
    m_dlssVersion->setPlaceholderText("latest");
    m_dlssVersion->setValidator(new QRegularExpressionValidator(
        QRegularExpression(R"(\d+(\.\d+){0,3})"), m_dlssVersion));
    m_dlssVersion->setToolTip(
        "Pin a specific DLSS version, e.g. 310.9.1 — written as "
        "PROTON_DLSS_UPGRADE=<version>.\n\n"
        "Empty: the newest version Proton knows (PROTON_DLSS_UPGRADE=1). "
        "A version Proton does not know falls back to the newest one.\n\n"
        "Requires: Proton-CachyOS or GE-Proton 10-26+.");
    versionRow->addWidget(m_dlssVersion, 1);
    layout->addLayout(versionRow);

    auto updateVersionEnabled = [this, versionLabel]() {
        const bool on = m_dlssUpgrade->isChecked();
        m_dlssVersion->setEnabled(on);
        versionLabel->setEnabled(on);
    };
    connect(m_dlssUpgrade, &QCheckBox::toggled, this, [this, updateVersionEnabled]() {
        updateVersionEnabled();
        emit changed();
    });
    connect(m_dlssVersion, &QLineEdit::textChanged, this, &SettingsPage::changed);
    updateVersionEnabled();

    return group;
}

QGroupBox* DlssPage::createSmoothMotionGroup()
{
    QGroupBox* group = new QGroupBox("Smooth Motion / Frame Rate Control", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_enableSmoothMotion = new QCheckBox("Enable Smooth Motion", this);
    m_enableSmoothMotion->setToolTip(
        "Enable driver-level frame generation (RTX 40/50).\n\n"
        "Doubles the displayed frame rate in games that have no frame generation "
        "of their own. NVIDIA advises against combining it with DLSS Frame "
        "Generation — use one or the other.\n\n"
        "Hangs at start-up and on Alt+Tab were fixed in NVIDIA driver 615.71.09.\n\n"
        "Sets NVPRESENT_ENABLE_SMOOTH_MOTION=1.");
    layout->addWidget(m_enableSmoothMotion);

    m_smoothMotionGraphicsQueue = new QCheckBox("Present on graphics queue (overlay compatibility)", this);
    m_smoothMotionGraphicsQueue->setToolTip(
        "Sets NVPRESENT_QUEUE_FAMILY=1.\n\n"
        "Smooth Motion normally presents on a queue of its own, which third-party "
        "overlays (MangoHud, OBS, Discord) do not see — they show nothing or flicker. "
        "Presenting on the graphics queue fixes that at a small performance cost.\n\n"
        "Only takes effect together with Smooth Motion.");
    QHBoxLayout* queueRow = new QHBoxLayout();
    queueRow->addSpacing(20);
    queueRow->addWidget(m_smoothMotionGraphicsQueue);
    layout->addLayout(queueRow);

    m_enableFrameRateLimit = new QCheckBox("Enable Frame Rate Limit", this);
    m_enableFrameRateLimit->setToolTip(
        "Limit the maximum frame rate for smoother, more consistent gameplay.\n\n"
        "Frame rate limiting can:\n"
        "• Reduce screen tearing\n"
        "• Lower GPU temperature and power consumption\n"
        "• Provide more consistent frame times\n"
        "• Reduce input latency spikes\n\n"
        "Sets dxvk.maxFrameRate in DXVK_CONFIG (D3D8-11) and VKD3D_FRAME_RATE (D3D12). "
        "DXVK 3.0 removed the older DXVK_FRAME_RATE.\n\n"
        "Recommended: Enable if you experience tearing or want to cap FPS below your monitor's refresh rate.");
    layout->addWidget(m_enableFrameRateLimit);

    QHBoxLayout* fpsLayout = new QHBoxLayout();
    m_targetFrameRateLabel = new QLabel("Target FPS:", this);
    fpsLayout->addWidget(m_targetFrameRateLabel);

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
        const bool on = m_enableFrameRateLimit->isChecked();
        m_targetFrameRate->setEnabled(on);
        m_targetFrameRateLabel->setEnabled(on);
    };

    connect(m_enableSmoothMotion, &QCheckBox::toggled, this, [this](bool on) {
        m_smoothMotionGraphicsQueue->setEnabled(on);
        emit changed();
    });
    connect(m_smoothMotionGraphicsQueue, &QCheckBox::toggled, this, &SettingsPage::changed);
    m_smoothMotionGraphicsQueue->setEnabled(m_enableSmoothMotion->isChecked());
    connect(m_enableFrameRateLimit, &QCheckBox::toggled, this, [updateFpsEnabled, this]() {
        updateFpsEnabled();
        emit changed();
    });
    connect(m_targetFrameRate, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsPage::changed);

    updateFpsEnabled();

    return group;
}

void DlssPage::load(const DLSSSettings& settings)
{
    SettingsPage::load(settings);

    // Super Resolution
    m_srOverride->setChecked(settings.srOverride);
    int srModeIndex = m_srMode->findData(settings.srMode);
    m_srMode->setCurrentIndex(srModeIndex >= 0 ? srModeIndex : 0);
    selectData(m_srPreset, settings.srPreset);
    // The panel blocks the slider's signals while loading, so the label is
    // set here rather than by valueChanged.
    const int srRatio = settings.srScalingRatio >= 33 && settings.srScalingRatio <= 100
                            ? settings.srScalingRatio : DefaultScalingRatio;
    m_srScalingRatio->setValue(srRatio);
    m_srScalingValue->setText(scalingText(srRatio));
    updateSuperResolutionEnabled();

    // Ray Reconstruction
    m_rrOverride->setChecked(settings.rrOverride);
    int rrModeIndex = m_rrMode->findData(settings.rrMode);
    m_rrMode->setCurrentIndex(rrModeIndex >= 0 ? rrModeIndex : 0);
    selectData(m_rrPreset, settings.rrPreset);
    const int rrRatio = settings.rrScalingRatio >= 33 && settings.rrScalingRatio <= 100
                            ? settings.rrScalingRatio : DefaultScalingRatio;
    m_rrScalingRatio->setValue(rrRatio);
    m_rrScalingValue->setText(scalingText(rrRatio));
    updateRayReconstructionEnabled();

    // Frame Generation
    m_fgOverride->setChecked(settings.fgOverride);
    int fgIndex = m_fgMultiFrameCount->findData(settings.fgMultiFrameCount);
    m_fgMultiFrameCount->setCurrentIndex(fgIndex >= 0 ? fgIndex : 0);
    int fgModeIndex = m_fgMode->findData(settings.fgMode);
    m_fgMode->setCurrentIndex(fgModeIndex >= 0 ? fgModeIndex : 0);
    selectData(m_fgPreset, settings.fgPreset);
    m_fgDynamicTargetFps->setValue(settings.fgDynamicTargetFps);
    int fgMaxIndex = m_fgDynamicMaxCount->findData(settings.fgDynamicMaxCount);
    m_fgDynamicMaxCount->setCurrentIndex(fgMaxIndex >= 0 ? fgMaxIndex : 0);
    updateFrameGenEnabled();

    // DLSS Upgrade
    m_dlssUpgrade->setChecked(settings.dlssUpgrade);
    m_dlssVersion->setText(settings.dlssVersion);
    m_dlssVersion->setEnabled(settings.dlssUpgrade);

    // Smooth Motion / frame cap
    m_enableSmoothMotion->setChecked(settings.enableSmoothMotion);
    m_smoothMotionGraphicsQueue->setChecked(settings.smoothMotionGraphicsQueue);
    m_smoothMotionGraphicsQueue->setEnabled(settings.enableSmoothMotion);
    m_enableFrameRateLimit->setChecked(settings.enableFrameRateLimit);
    m_targetFrameRate->setValue(settings.targetFrameRate);
    m_targetFrameRate->setEnabled(settings.enableFrameRateLimit);
}

void DlssPage::store(DLSSSettings& settings) const
{
    SettingsPage::store(settings);

    settings.srOverride = m_srOverride->isChecked();
    settings.srMode = m_srMode->currentData().toString();
    settings.srPreset = m_srPreset->currentData().toString();
    // Only CUSTOM reads a ratio; any other mode keeps the "default" 0.
    settings.srScalingRatio = DLSSSettings::isCustomMode(settings.srMode)
                                  ? m_srScalingRatio->value() : 0;

    settings.rrOverride = m_rrOverride->isChecked();
    settings.rrMode = m_rrMode->currentData().toString();
    settings.rrPreset = m_rrPreset->currentData().toString();
    settings.rrScalingRatio = DLSSSettings::isCustomMode(settings.rrMode)
                                  ? m_rrScalingRatio->value() : 0;

    settings.fgOverride = m_fgOverride->isChecked();
    settings.fgMultiFrameCount = m_fgMultiFrameCount->currentData().toInt();
    settings.fgMode = m_fgMode->currentData().toString();
    settings.fgPreset = m_fgPreset->currentData().toString();
    settings.fgDynamicTargetFps = m_fgDynamicTargetFps->value();
    settings.fgDynamicMaxCount = m_fgDynamicMaxCount->currentData().toInt();

    settings.dlssUpgrade = m_dlssUpgrade->isChecked();
    settings.dlssVersion = m_dlssVersion->text().trimmed();

    settings.enableSmoothMotion = m_enableSmoothMotion->isChecked();
    settings.smoothMotionGraphicsQueue = m_smoothMotionGraphicsQueue->isChecked();
    settings.enableFrameRateLimit = m_enableFrameRateLimit->isChecked();
    settings.targetFrameRate = m_targetFrameRate->value();
}

void DlssPage::setGame(const Game& game)
{
    SettingsPage::setGame(game);

    // Re-enabling a group box hands its children back their own state, but the
    // frame-rate row has no group of its own to restore it — it is only ever
    // live together with its checkbox.
    if (!game.isNativeLinux()) {
        const bool on = m_enableFrameRateLimit->isChecked();
        m_targetFrameRate->setEnabled(on);
        m_targetFrameRateLabel->setEnabled(on);
    }
}
