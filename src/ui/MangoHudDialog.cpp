#include "MangoHudDialog.h"
#include "AppStyle.h"
#include "MangoHudPreview.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QFileDialog>
#include "utils/CPUDetector.h"
#include "utils/GPUDetector.h"
#include "utils/NvidiaGPUDetector.h"
#include <QPushButton>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QResizeEvent>
#include <functional>

// The preview canvas. A window onto the game, with the overlay drawn into the
// corner the Appearance group picked. Deliberately dumb: it holds a state struct
// and paints it, and every decision about what that state means lives in
// MangoHudPreview.h where a unit test can reach it.
class MangoHudPreviewCanvas : public QWidget
{
public:
    explicit MangoHudPreviewCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(230);
    }

    void setState(const MangoHudPreview::State& state)
    {
        m_state = state;
        update();
    }

    const MangoHudPreview::State& state() const { return m_state; }

    // Called after a resize, because how much the overlay had to shrink depends on
    // the canvas size and the dialog can only say so once that is known.
    std::function<void()> onGeometryChanged;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (onGeometryChanged) {
            onGeometryChanged();
        }
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);

        const QRectF frame = QRectF(rect()).adjusted(1, 1, -1, -1);

        // A stand-in for the game: enough contrast for the background alpha to
        // mean something, quiet enough not to compete with the overlay.
        QLinearGradient backdrop(frame.topLeft(), frame.bottomRight());
        backdrop.setColorAt(0.0, QColor("#243447"));
        backdrop.setColorAt(0.55, QColor("#3c5064"));
        backdrop.setColorAt(1.0, QColor("#16202b"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(backdrop);
        painter.drawRect(frame);

        painter.setBrush(QColor(255, 255, 255, 18));
        painter.drawEllipse(QRectF(frame.center().x() - frame.width() * 0.18,
                                   frame.top() + frame.height() * 0.18,
                                   frame.width() * 0.36, frame.width() * 0.36));
        painter.setBrush(QColor(0, 0, 0, 40));
        painter.drawRect(QRectF(frame.left(), frame.bottom() - frame.height() * 0.28,
                                frame.width(), frame.height() * 0.28));

        MangoHudPreview::draw(&painter, frame, m_state);

        if (MangoHudPreview::rows(m_state).isEmpty()) {
            painter.setPen(QColor(AppStyle::ColorTextMuted));
            painter.drawText(frame, Qt::AlignCenter,
                             "Nothing is enabled —\nthe overlay would be empty.");
        }
    }

private:
    MangoHudPreview::State m_state;
};

MangoHudDialog::MangoHudDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("MangoHud Configuration");
    // Wide enough for the options plus the preview beside them, and still inside
    // a 1280x800 laptop screen — both this and the minimum, which a window
    // manager will not let the user shrink past.
    setMinimumSize(980, 560);
    resize(1200, 680);
    setupUI();
    loadConfig();
    connectPreviewSignals();
    updatePreview();
}

bool MangoHudDialog::isMangoHudInstalled()
{
    // A PATH lookup, not a subprocess — same as KdeDisplayProbe::available().
    // The previous "which mangohud" dropped waitForFinished()'s return value and
    // then read exitCode(), which is 0 on a process that never ran: an empty
    // PATH reported MangoHud as installed. This is also on the per-game-click
    // path via DLSSSettingsWidget::setSettings().
    return !QStandardPaths::findExecutable("mangohud").isEmpty();
}

QString MangoHudDialog::configFilePath() const
{
    return QDir::homePath() + "/.config/MangoHud/MangoHud.conf";
}

void MangoHudDialog::setupUI()
{
    setStyleSheet(AppStyle::dialogButtonStyle());

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 12);
    mainLayout->setSpacing(0);

    // Scrollable content
    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(
        QString("QScrollArea { background: %1; border: none; }"
                "QScrollBar:vertical { background: %1; width: 8px; }"
                "QScrollBar::handle:vertical { background: %2; border-radius: 4px; min-height: 20px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(AppStyle::ColorBgBase, AppStyle::ColorBorder));

    auto* contentWidget = new QWidget;
    auto* contentLayout = new QHBoxLayout(contentWidget);
    contentLayout->setContentsMargins(16, 16, 16, 16);
    contentLayout->setSpacing(16);

    // Left column
    auto* leftColumn = new QVBoxLayout;
    leftColumn->setSpacing(12);
    leftColumn->addWidget(createPerformanceGroup());
    leftColumn->addWidget(createCpuGroup());
    leftColumn->addWidget(createGpuGroup());
    leftColumn->addWidget(createKeybindsGroup());
    leftColumn->addStretch();

    // Right column
    auto* rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(12);
    rightColumn->addWidget(createMetricsGroup());
    rightColumn->addWidget(createSystemGroup());
    rightColumn->addWidget(createAppearanceGroup());
    rightColumn->addWidget(createLoggingGroup());
    rightColumn->addStretch();

    contentLayout->addLayout(leftColumn, 1);
    contentLayout->addLayout(rightColumn, 1);

    scrollArea->setWidget(contentWidget);

    // The preview sits beside the scroll area rather than inside it: it is only
    // useful while options are being ticked, so it must not scroll away.
    auto* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(scrollArea, 1);
    bodyLayout->addWidget(createPreviewPanel());
    mainLayout->addLayout(bodyLayout, 1);

    // Button bar
    auto* btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(12, 8, 12, 0);
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* saveBtn = new QPushButton("Save");
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &MangoHudDialog::saveConfig);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addSpacing(8);
    btnLayout->addWidget(saveBtn);
    mainLayout->addLayout(btnLayout);
}

static QGroupBox* makeGroup(const QString& title)
{
    auto* group = new QGroupBox(title);
    group->setStyleSheet(
        QString("QGroupBox { font-weight: bold; color: %1; border: 1px solid %2; "
                "border-radius: 6px; margin-top: 12px; padding-top: 16px; } "
                "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }")
            .arg(AppStyle::ColorTextPrimary, AppStyle::ColorBorder));
    return group;
}

static QLineEdit* makeLineEdit(const QString& placeholder = {})
{
    auto* edit = new QLineEdit;
    if (!placeholder.isEmpty())
        edit->setPlaceholderText(placeholder);
    edit->setStyleSheet(
        QString("QLineEdit { background: %1; border: 1px solid %2; border-radius: 4px; "
                "padding: 4px 8px; color: %3; }")
            .arg(AppStyle::ColorBgElevated, AppStyle::ColorBorderLight, AppStyle::ColorTextPrimary));
    return edit;
}

static void addRow(QVBoxLayout* layout, const QString& label, QWidget* widget)
{
    auto* row = new QHBoxLayout;
    auto* lbl = new QLabel(label);
    lbl->setStyleSheet(QString("color: %1;").arg(AppStyle::ColorTextSecondary));
    lbl->setMinimumWidth(130);
    row->addWidget(lbl);
    row->addWidget(widget, 1);
    layout->addLayout(row);
}

// One keybind row: the checkbox is the label, and the field beside it is only
// editable while it is ticked. The field still shows MangoHud's built-in binding
// when unticked, greyed out — that is the whole point of the row, since an absent
// line does not mean "no key", it means "MangoHud's default key".
//
// MangoHud wants X11 keysym names joined with '+', which is not guessable, hence
// the tooltip. Returns the field and hands back the checkbox through `box`.
static QLineEdit* addKeybindRow(QVBoxLayout* layout, const QString& label,
                                const QString& mangoHudDefault, QCheckBox** box)
{
    auto* check = new QCheckBox(label);
    check->setMinimumWidth(140);

    auto* edit = makeLineEdit(mangoHudDefault);
    edit->setText(mangoHudDefault);
    edit->setEnabled(false);

    const QString hint =
        QString("X11 keysym names joined with '+', e.g. %1.\n"
                "Unticked, MangoHud's own default applies.").arg(mangoHudDefault);
    check->setToolTip(hint);
    edit->setToolTip(hint);

    auto* row = new QHBoxLayout;
    row->addWidget(check);
    row->addWidget(edit, 1);
    layout->addLayout(row);

    QObject::connect(check, &QCheckBox::toggled, edit, &QWidget::setEnabled);

    *box = check;
    return edit;
}

static QPushButton* makeDetectButton()
{
    auto* btn = new QPushButton("Detect");
    btn->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %2; padding: 4px 10px; "
                "border: 1px solid %3; border-radius: 4px; font-size: 11px; }"
                "QPushButton:hover { background-color: %4; border: 1px solid %5; }")
            .arg(AppStyle::ColorBgButton, AppStyle::ColorTextPrimary,
                 AppStyle::ColorBorder, AppStyle::ColorBgButtonHover, AppStyle::ColorAccent));
    return btn;
}

static QString shortenCpuName(const QString& name)
{
    QString s = name;
    // Remove common verbose suffixes
    s.remove(QRegularExpression("\\s*\\d+-Core Processor$", QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression("\\s*with Radeon.*$", QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression("\\s*@ \\d+\\.\\d+GHz$"));
    s.remove("(R)").remove("(TM)").remove("(tm)");
    s.remove("CPU ");
    return s.simplified();
}

static QString shortenGpuName(const QString& name)
{
    QString s = name;
    s.remove("NVIDIA ").remove("GeForce ");
    return s.simplified();
}

QGroupBox* MangoHudDialog::createPerformanceGroup()
{
    auto* group = makeGroup("Performance");
    auto* layout = new QVBoxLayout(group);

    m_fpsLimitEnabled = new QCheckBox("FPS Limit:");
    m_fpsLimitEnabled->setMinimumWidth(110);
    m_fpsLimit = makeLineEdit("e.g. 0,30,60");
    m_fpsLimit->setEnabled(false);
    auto* fpsRow = new QHBoxLayout;
    fpsRow->addWidget(m_fpsLimitEnabled);
    fpsRow->addWidget(m_fpsLimit, 1);
    layout->addLayout(fpsRow);
    connect(m_fpsLimitEnabled, &QCheckBox::toggled, m_fpsLimit, &QWidget::setEnabled);

    m_vsyncEnabled = new QCheckBox("VSync:");
    m_vsyncEnabled->setMinimumWidth(110);
    m_vsync = new QComboBox;
    m_vsync->addItem("Adaptive", 0);
    m_vsync->addItem("Off", 1);
    m_vsync->addItem("Mailbox", 2);
    m_vsync->addItem("On", 3);
    m_vsync->setEnabled(false);
    auto* vsyncRow = new QHBoxLayout;
    vsyncRow->addWidget(m_vsyncEnabled);
    vsyncRow->addWidget(m_vsync, 1);
    layout->addLayout(vsyncRow);
    connect(m_vsyncEnabled, &QCheckBox::toggled, m_vsync, &QWidget::setEnabled);

    return group;
}

QGroupBox* MangoHudDialog::createCpuGroup()
{
    auto* group = makeGroup("CPU Display");
    auto* layout = new QVBoxLayout(group);

    m_cpuStats = new QCheckBox("CPU Stats");
    m_cpuTemp = new QCheckBox("CPU Temperature");
    m_cpuPower = new QCheckBox("CPU Power");
    m_cpuMhz = new QCheckBox("CPU Frequency");

    layout->addWidget(m_cpuStats);
    layout->addWidget(m_cpuTemp);
    layout->addWidget(m_cpuPower);
    layout->addWidget(m_cpuMhz);

    m_cpuText = makeLineEdit("e.g. Ryzen 9 7950X");
    auto* cpuDetectBtn = makeDetectButton();
    auto* cpuLabelRow = new QHBoxLayout;
    auto* cpuLbl = new QLabel("CPU Label:");
    cpuLbl->setStyleSheet(QString("color: %1;").arg(AppStyle::ColorTextSecondary));
    cpuLbl->setMinimumWidth(130);
    cpuLabelRow->addWidget(cpuLbl);
    cpuLabelRow->addWidget(m_cpuText, 1);
    cpuLabelRow->addWidget(cpuDetectBtn);
    layout->addLayout(cpuLabelRow);

    connect(cpuDetectBtn, &QPushButton::clicked, this, [this]() {
        CPUInfo info = CPUDetector::detect();
        if (!info.modelName.isEmpty())
            m_cpuText->setText(shortenCpuName(info.modelName));
    });

    return group;
}

QGroupBox* MangoHudDialog::createGpuGroup()
{
    auto* group = makeGroup("GPU Display");
    auto* layout = new QVBoxLayout(group);

    m_gpuStats = new QCheckBox("GPU Stats");
    m_gpuTemp = new QCheckBox("GPU Temperature");
    m_gpuCoreClock = new QCheckBox("GPU Core Clock");
    m_gpuMemClock = new QCheckBox("GPU Memory Clock");
    m_gpuPower = new QCheckBox("GPU Power");
    m_gpuName = new QCheckBox("GPU Name");
    m_vulkanDriver = new QCheckBox("Vulkan Driver");

    layout->addWidget(m_gpuStats);
    layout->addWidget(m_gpuTemp);
    layout->addWidget(m_gpuCoreClock);
    layout->addWidget(m_gpuMemClock);
    layout->addWidget(m_gpuPower);
    layout->addWidget(m_gpuName);
    layout->addWidget(m_vulkanDriver);

    m_gpuText = makeLineEdit("e.g. RTX 4090");
    auto* gpuDetectBtn = makeDetectButton();
    auto* gpuLabelRow = new QHBoxLayout;
    auto* gpuLbl = new QLabel("GPU Label:");
    gpuLbl->setStyleSheet(QString("color: %1;").arg(AppStyle::ColorTextSecondary));
    gpuLbl->setMinimumWidth(130);
    gpuLabelRow->addWidget(gpuLbl);
    gpuLabelRow->addWidget(m_gpuText, 1);
    gpuLabelRow->addWidget(gpuDetectBtn);
    layout->addLayout(gpuLabelRow);

    connect(gpuDetectBtn, &QPushButton::clicked, this, [this]() {
        QList<GPUInfo> gpus = GPUDetector::detectAllGPUs();
        if (!gpus.isEmpty())
            m_gpuText->setText(shortenGpuName(gpus.first().name));
    });

    return group;
}

QGroupBox* MangoHudDialog::createMetricsGroup()
{
    auto* group = makeGroup("Metrics");
    auto* layout = new QVBoxLayout(group);

    m_fps = new QCheckBox("FPS");
    m_frametime = new QCheckBox("Frame Time");
    m_frameTiming = new QCheckBox("Frame Time Graph");
    m_histogram = new QCheckBox("Histogram");
    m_showFpsLimit = new QCheckBox("Show FPS Limit");

    layout->addWidget(m_fps);
    layout->addWidget(m_frametime);
    layout->addWidget(m_frameTiming);
    layout->addWidget(m_histogram);
    layout->addWidget(m_showFpsLimit);

    return group;
}

QGroupBox* MangoHudDialog::createSystemGroup()
{
    auto* group = makeGroup("System Info");
    auto* layout = new QVBoxLayout(group);

    m_ram = new QCheckBox("RAM Usage");
    m_swap = new QCheckBox("Swap Usage");
    m_vram = new QCheckBox("VRAM Usage");
    m_resolution = new QCheckBox("Resolution");
    m_time = new QCheckBox("System Time");
    m_arch = new QCheckBox("Architecture");
    m_version = new QCheckBox("MangoHud Version");
    m_engineVersion = new QCheckBox("Engine Version");
    m_wine = new QCheckBox("Wine Version");

    layout->addWidget(m_ram);
    layout->addWidget(m_swap);
    layout->addWidget(m_vram);
    layout->addWidget(m_resolution);
    layout->addWidget(m_time);
    layout->addWidget(m_arch);
    layout->addWidget(m_version);
    layout->addWidget(m_engineVersion);
    layout->addWidget(m_wine);

    return group;
}

QGroupBox* MangoHudDialog::createAppearanceGroup()
{
    auto* group = makeGroup("Appearance");
    auto* layout = new QVBoxLayout(group);

    m_position = new QComboBox;
    m_position->addItem("Top Left", "top-left");
    m_position->addItem("Top Right", "top-right");
    m_position->addItem("Bottom Left", "bottom-left");
    m_position->addItem("Bottom Right", "bottom-right");
    m_position->addItem("Top Center", "top-center");
    m_position->addItem("Bottom Center", "bottom-center");
    addRow(layout, "Position:", m_position);

    m_fontSize = new QSpinBox;
    m_fontSize->setRange(8, 72);
    m_fontSize->setValue(24);
    addRow(layout, "Font Size:", m_fontSize);

    m_backgroundAlpha = new QDoubleSpinBox;
    m_backgroundAlpha->setRange(0.0, 1.0);
    m_backgroundAlpha->setSingleStep(0.05);
    m_backgroundAlpha->setDecimals(2);
    m_backgroundAlpha->setValue(0.5);
    addRow(layout, "Background Alpha:", m_backgroundAlpha);

    m_customTextCenter = makeLineEdit("Custom header text");
    addRow(layout, "Custom Text:", m_customTextCenter);

    return group;
}

QGroupBox* MangoHudDialog::createLoggingGroup()
{
    auto* group = makeGroup("Logging");
    auto* layout = new QVBoxLayout(group);

    m_autostartLogEnabled = new QCheckBox("Auto-start Log:");
    m_autostartLogEnabled->setMinimumWidth(130);
    m_autostartLog = new QSpinBox;
    m_autostartLog->setRange(0, 3600);
    m_autostartLog->setSpecialValueText("Disabled");
    m_autostartLog->setSuffix(" sec");
    m_autostartLog->setEnabled(false);
    auto* autoLogRow = new QHBoxLayout;
    autoLogRow->addWidget(m_autostartLogEnabled);
    autoLogRow->addWidget(m_autostartLog, 1);
    layout->addLayout(autoLogRow);
    connect(m_autostartLogEnabled, &QCheckBox::toggled, m_autostartLog, &QWidget::setEnabled);

    m_logDurationEnabled = new QCheckBox("Log Duration:");
    m_logDurationEnabled->setMinimumWidth(130);
    m_logDuration = new QSpinBox;
    m_logDuration->setRange(0, 86400);
    m_logDuration->setSpecialValueText("Unlimited");
    m_logDuration->setSuffix(" sec");
    m_logDuration->setEnabled(false);
    auto* logDurRow = new QHBoxLayout;
    logDurRow->addWidget(m_logDurationEnabled);
    logDurRow->addWidget(m_logDuration, 1);
    layout->addLayout(logDurRow);
    connect(m_logDurationEnabled, &QCheckBox::toggled, m_logDuration, &QWidget::setEnabled);

    m_outputFolderEnabled = new QCheckBox("Output Folder:");
    m_outputFolderEnabled->setMinimumWidth(130);
    m_outputFolder = makeLineEdit("/home/user/mangologs");
    m_outputFolder->setEnabled(false);
    auto* browseBtn = makeDetectButton();
    browseBtn->setText("Browse");
    browseBtn->setEnabled(false);
    auto* outFolderRow = new QHBoxLayout;
    outFolderRow->addWidget(m_outputFolderEnabled);
    outFolderRow->addWidget(m_outputFolder, 1);
    outFolderRow->addWidget(browseBtn);
    layout->addLayout(outFolderRow);
    connect(m_outputFolderEnabled, &QCheckBox::toggled, m_outputFolder, &QWidget::setEnabled);
    connect(m_outputFolderEnabled, &QCheckBox::toggled, browseBtn, &QWidget::setEnabled);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Log Output Folder",
            m_outputFolder->text().isEmpty() ? QDir::homePath() : m_outputFolder->text());
        if (!dir.isEmpty())
            m_outputFolder->setText(dir);
    });

    return group;
}

// The defaults MangoHud itself uses when the key is absent — from its shipped
// MangoHud.conf.example, INTERACTION section. They are also what the fields show
// while unticked, so they have to be right rather than merely plausible.
QGroupBox* MangoHudDialog::createKeybindsGroup()
{
    auto* group = makeGroup("Keybinds");
    auto* layout = new QVBoxLayout(group);

    m_keyToggleHud = addKeybindRow(layout, "Toggle HUD:", "Shift_R+F12",
                                   &m_keyToggleHudEnabled);
    m_keyToggleFpsLimit = addKeybindRow(layout, "Toggle FPS Limit:", "Shift_L+F1",
                                        &m_keyToggleFpsLimitEnabled);
    m_keyToggleLogging = addKeybindRow(layout, "Toggle Logging:", "Shift_L+F2",
                                       &m_keyToggleLoggingEnabled);

    return group;
}

// ── Preview ────────────────────────────────────────────────────────────────

QWidget* MangoHudDialog::createPreviewPanel()
{
    // Detected once, here, rather than on every repaint: the row is supposed to
    // show what this machine would actually print.
    const QList<GPUInfo> gpus = GPUDetector::detectAllGPUs();
    if (!gpus.isEmpty()) {
        m_detectedGpu = shortenGpuName(gpus.first().name);
        const QString module = gpus.first().driverInfo.moduleName;
        const QString version = gpus.first().driverVersion;
        if (!module.isEmpty() || !version.isEmpty()) {
            m_detectedDriver = QString("%1 %2").arg(module, version).trimmed();
        }
    }

    auto* panel = new QWidget;
    panel->setFixedWidth(320);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 16, 16, 16);
    layout->setSpacing(8);

    auto* title = new QLabel("Preview");
    title->setStyleSheet(QString("color: %1; font-weight: bold;").arg(AppStyle::ColorTextPrimary));
    layout->addWidget(title);

    m_preview = new MangoHudPreviewCanvas(panel);
    m_preview->onGeometryChanged = [this]() { refreshPreviewHint(); };
    // Takes the height the panel has: the wider the window onto the game, the less
    // the overlay has to be shrunk to fit into it.
    layout->addWidget(m_preview, 1);

    m_previewScaleHint = new QLabel;
    m_previewScaleHint->setWordWrap(true);
    m_previewScaleHint->setStyleSheet(
        QString("color: %1; font-size: 11px;").arg(AppStyle::ColorTextMuted));
    layout->addWidget(m_previewScaleHint);
    return panel;
}

void MangoHudDialog::connectPreviewSignals()
{
    // Every option widget at once, by type, instead of one connect per option.
    // Naming them individually is how a preview quietly stops covering the option
    // somebody adds next year; this cannot miss one. The keybind fields are
    // included and simply do not affect what rows() returns.
    for (QCheckBox* box : findChildren<QCheckBox*>()) {
        connect(box, &QCheckBox::toggled, this, &MangoHudDialog::updatePreview);
    }
    for (QLineEdit* edit : findChildren<QLineEdit*>()) {
        connect(edit, &QLineEdit::textChanged, this, &MangoHudDialog::updatePreview);
    }
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        connect(combo, &QComboBox::currentIndexChanged, this, &MangoHudDialog::updatePreview);
    }
    for (QSpinBox* spin : findChildren<QSpinBox*>()) {
        connect(spin, &QSpinBox::valueChanged, this, &MangoHudDialog::updatePreview);
    }
    for (QDoubleSpinBox* spin : findChildren<QDoubleSpinBox*>()) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &MangoHudDialog::updatePreview);
    }
}

void MangoHudDialog::updatePreview()
{
    if (!m_preview) {
        return;
    }

    MangoHudPreview::State state;

    state.gpuStats = m_gpuStats->isChecked();
    state.gpuTemp = m_gpuTemp->isChecked();
    state.gpuCoreClock = m_gpuCoreClock->isChecked();
    state.gpuMemClock = m_gpuMemClock->isChecked();
    state.gpuPower = m_gpuPower->isChecked();
    state.gpuName = m_gpuName->isChecked();
    state.vulkanDriver = m_vulkanDriver->isChecked();
    state.gpuLabel = m_gpuText->text().trimmed();
    state.detectedGpu = m_detectedGpu;
    state.vulkanDriverName = m_detectedDriver;

    state.cpuStats = m_cpuStats->isChecked();
    state.cpuTemp = m_cpuTemp->isChecked();
    state.cpuPower = m_cpuPower->isChecked();
    state.cpuMhz = m_cpuMhz->isChecked();
    state.cpuLabel = m_cpuText->text().trimmed();

    state.fps = m_fps->isChecked();
    state.frametime = m_frametime->isChecked();
    state.frameTiming = m_frameTiming->isChecked();
    state.histogram = m_histogram->isChecked();
    state.showFpsLimit = m_showFpsLimit->isChecked();
    // Empty unless a limit is actually in force — MangoHud has nothing to show
    // otherwise. "0" is MangoHud's own "no limit".
    if (m_fpsLimitEnabled->isChecked()) {
        const QString limit = m_fpsLimit->text().trimmed().section(',', 0, 0).trimmed();
        if (!limit.isEmpty() && limit != QLatin1String("0")) {
            state.fpsLimit = limit;
        }
    }

    state.ram = m_ram->isChecked();
    state.swap = m_swap->isChecked();
    state.vram = m_vram->isChecked();
    state.resolution = m_resolution->isChecked();
    state.time = m_time->isChecked();
    state.arch = m_arch->isChecked();
    state.version = m_version->isChecked();
    state.engineVersion = m_engineVersion->isChecked();
    state.wine = m_wine->isChecked();

    state.position = m_position->currentData().toString();
    state.fontSize = m_fontSize->value();
    state.backgroundAlpha = m_backgroundAlpha->value();
    state.customText = m_customTextCenter->text().trimmed();

    m_preview->setState(state);
    refreshPreviewHint();
}

void MangoHudDialog::refreshPreviewHint()
{
    if (!m_preview || !m_previewScaleHint) {
        return;
    }

    const MangoHudPreview::State& state = m_preview->state();
    if (MangoHudPreview::rows(state).isEmpty()) {
        m_previewScaleHint->setText("A window onto the game, with the overlay where "
                                    "the Appearance group puts it.");
        return;
    }

    const qreal scale = MangoHudPreview::scaleToFit(state, QSizeF(m_preview->size()));
    if (scale < 0.999) {
        m_previewScaleHint->setText(
            QString("Shrunk to %1% to fit the panel — in game it is drawn at font "
                    "size %2, roughly %3x%4 pixels.")
                .arg(qRound(scale * 100))
                .arg(state.fontSize)
                .arg(qRound(MangoHudPreview::boxSize(state).width()))
                .arg(qRound(MangoHudPreview::boxSize(state).height())));
    } else {
        m_previewScaleHint->setText(
            QString("Actual size: font size %1, roughly %2x%3 pixels in game.")
                .arg(state.fontSize)
                .arg(qRound(MangoHudPreview::boxSize(state).width()))
                .arg(qRound(MangoHudPreview::boxSize(state).height())));
    }
}

// ── Config file parsing ────────────────────────────────────────────────────

void MangoHudDialog::loadConfig()
{
    QString path = configFilePath();
    if (QFileInfo::exists(path))
        parseConfigFile(path);
}

void MangoHudDialog::parseConfigFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    m_originalLines.clear();
    m_parsedValues.clear();
    m_activeKeys.clear();

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        m_originalLines.append(line);

        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("###"))
            continue;

        bool commented = trimmed.startsWith('#');
        QString content = commented ? trimmed.mid(1).trimmed() : trimmed;

        // Remove inline comments (but not inside values)
        int eqPos = content.indexOf('=');
        QString key, value;
        if (eqPos >= 0) {
            key = content.left(eqPos).trimmed();
            value = content.mid(eqPos + 1).trimmed();
        } else {
            key = content.trimmed();
        }

        if (key.isEmpty())
            continue;

        m_parsedValues[key] = value;
        if (!commented)
            m_activeKeys.insert(key);
    }
    file.close();

    // Helper lambdas
    auto isActive = [&](const QString& key) { return m_activeKeys.contains(key); };
    auto getValue = [&](const QString& key, const QString& def = {}) -> QString {
        return m_parsedValues.value(key, def);
    };

    // Performance
    m_fpsLimitEnabled->setChecked(isActive("fps_limit"));
    m_fpsLimit->setText(getValue("fps_limit"));
    m_vsyncEnabled->setChecked(isActive("vsync"));
    int vsyncVal = getValue("vsync", "-1").toInt();
    int vsyncIdx = m_vsync->findData(vsyncVal);
    if (vsyncIdx >= 0)
        m_vsync->setCurrentIndex(vsyncIdx);

    // CPU
    m_cpuStats->setChecked(isActive("cpu_stats"));
    m_cpuTemp->setChecked(isActive("cpu_temp"));
    m_cpuPower->setChecked(isActive("cpu_power"));
    m_cpuMhz->setChecked(isActive("cpu_mhz"));
    m_cpuText->setText(getValue("cpu_text"));

    // GPU
    m_gpuStats->setChecked(isActive("gpu_stats"));
    m_gpuTemp->setChecked(isActive("gpu_temp"));
    m_gpuCoreClock->setChecked(isActive("gpu_core_clock"));
    m_gpuMemClock->setChecked(isActive("gpu_mem_clock"));
    m_gpuPower->setChecked(isActive("gpu_power"));
    m_gpuName->setChecked(isActive("gpu_name"));
    m_vulkanDriver->setChecked(isActive("vulkan_driver"));
    m_gpuText->setText(getValue("gpu_text"));

    // Metrics
    m_fps->setChecked(isActive("fps"));
    m_frametime->setChecked(isActive("frametime"));
    m_frameTiming->setChecked(isActive("frame_timing"));
    m_histogram->setChecked(isActive("histogram"));
    m_showFpsLimit->setChecked(isActive("show_fps_limit"));

    // System
    m_ram->setChecked(isActive("ram"));
    m_swap->setChecked(isActive("swap"));
    m_vram->setChecked(isActive("vram"));
    m_resolution->setChecked(isActive("resolution"));
    m_time->setChecked(isActive("time"));
    m_arch->setChecked(isActive("arch"));
    m_version->setChecked(isActive("version"));
    m_engineVersion->setChecked(isActive("engine_version"));
    m_wine->setChecked(isActive("wine"));

    // Appearance
    QString pos = getValue("position", "top-left");
    int posIdx = m_position->findData(pos);
    if (posIdx >= 0)
        m_position->setCurrentIndex(posIdx);

    m_fontSize->setValue(getValue("font_size", "24").toInt());
    m_backgroundAlpha->setValue(getValue("background_alpha", "0.5").toDouble());
    m_customTextCenter->setText(getValue("custom_text_center"));

    // Logging
    m_autostartLogEnabled->setChecked(isActive("autostart_log"));
    m_autostartLog->setValue(getValue("autostart_log", "0").toInt());
    m_logDurationEnabled->setChecked(isActive("log_duration"));
    m_logDuration->setValue(getValue("log_duration", "0").toInt());
    m_outputFolderEnabled->setChecked(isActive("output_folder"));
    m_outputFolder->setText(getValue("output_folder"));

    // Keybinds. MangoHud ships these commented out, which is exactly the state
    // getValue/isActive already distinguishes: the value fills the field, the
    // missing tick says the default is in force.
    //
    // A keysym name never contains '#', so a trailing one on these lines is a
    // comment. Cut here rather than in the shared parser, where custom_text_center
    // may legitimately hold a '#'. The field keeps its constructed default when
    // the file has nothing to say.
    auto keybind = [&](const QString& key, QLineEdit* field) {
        const QString value = getValue(key).section('#', 0, 0).trimmed();
        if (!value.isEmpty())
            field->setText(value);
    };

    m_keyToggleHudEnabled->setChecked(isActive("toggle_hud"));
    keybind("toggle_hud", m_keyToggleHud);
    m_keyToggleFpsLimitEnabled->setChecked(isActive("toggle_fps_limit"));
    keybind("toggle_fps_limit", m_keyToggleFpsLimit);
    m_keyToggleLoggingEnabled->setChecked(isActive("toggle_logging"));
    keybind("toggle_logging", m_keyToggleLogging);
}

void MangoHudDialog::saveConfig()
{
    QString path = configFilePath();

    // Ensure directory exists
    QDir().mkpath(QFileInfo(path).absolutePath());

    writeConfigFile(path);
    accept();
}

void MangoHudDialog::writeConfigFile(const QString& path)
{
    // Build desired state from UI
    struct ToggleEntry { QString key; bool enabled; };
    struct ValueEntry  { QString key; QString value; bool onlyIfNonEmpty; bool enabled; };

    QList<ToggleEntry> toggles = {
        {"cpu_stats",       m_cpuStats->isChecked()},
        {"cpu_temp",        m_cpuTemp->isChecked()},
        {"cpu_power",       m_cpuPower->isChecked()},
        {"cpu_mhz",         m_cpuMhz->isChecked()},
        {"gpu_stats",       m_gpuStats->isChecked()},
        {"gpu_temp",        m_gpuTemp->isChecked()},
        {"gpu_core_clock",  m_gpuCoreClock->isChecked()},
        {"gpu_mem_clock",   m_gpuMemClock->isChecked()},
        {"gpu_power",       m_gpuPower->isChecked()},
        {"gpu_name",        m_gpuName->isChecked()},
        {"vulkan_driver",   m_vulkanDriver->isChecked()},
        {"fps",             m_fps->isChecked()},
        {"frametime",       m_frametime->isChecked()},
        {"frame_timing",    m_frameTiming->isChecked()},
        {"histogram",       m_histogram->isChecked()},
        {"show_fps_limit",  m_showFpsLimit->isChecked()},
        {"ram",             m_ram->isChecked()},
        {"swap",            m_swap->isChecked()},
        {"vram",            m_vram->isChecked()},
        {"resolution",      m_resolution->isChecked()},
        {"time",            m_time->isChecked()},
        {"arch",            m_arch->isChecked()},
        {"version",         m_version->isChecked()},
        {"engine_version",  m_engineVersion->isChecked()},
        {"wine",            m_wine->isChecked()},
    };

    QList<ValueEntry> values = {
        {"fps_limit",          m_fpsLimit->text().trimmed(),                             false, m_fpsLimitEnabled->isChecked()},
        {"vsync",              QString::number(m_vsync->currentData().toInt()),          false, m_vsyncEnabled->isChecked()},
        {"cpu_text",           m_cpuText->text().trimmed(),                              true,  true},
        {"gpu_text",           m_gpuText->text().trimmed(),                              true,  true},
        {"position",           m_position->currentData().toString(),                     false, true},
        {"font_size",          QString::number(m_fontSize->value()),                     false, true},
        {"background_alpha",   QString::number(m_backgroundAlpha->value(), 'f', 2),      false, true},
        {"custom_text_center", m_customTextCenter->text().trimmed(),                     true,  true},
        {"autostart_log",      QString::number(m_autostartLog->value()),  false, m_autostartLogEnabled->isChecked()},
        {"log_duration",       QString::number(m_logDuration->value()),  false, m_logDurationEnabled->isChecked()},
        {"output_folder",      m_outputFolder->text().trimmed(),         false, m_outputFolderEnabled->isChecked()},
        // onlyIfNonEmpty: a bare "toggle_hud=" would read as a binding to nothing,
        // so a field cleared while ticked comments the line out instead.
        {"toggle_hud",         m_keyToggleHud->text().trimmed(),         true,  m_keyToggleHudEnabled->isChecked()},
        {"toggle_fps_limit",   m_keyToggleFpsLimit->text().trimmed(),    true,  m_keyToggleFpsLimitEnabled->isChecked()},
        {"toggle_logging",     m_keyToggleLogging->text().trimmed(),     true,  m_keyToggleLoggingEnabled->isChecked()},
    };

    // Build sets of all managed keys
    QSet<QString> managedKeys;
    for (const auto& t : toggles) managedKeys.insert(t.key);
    for (const auto& v : values) managedKeys.insert(v.key);

    // Build desired state maps
    QSet<QString> desiredActive;
    QMap<QString, QString> desiredValues;

    for (const auto& t : toggles) {
        if (t.enabled) desiredActive.insert(t.key);
    }
    for (const auto& v : values) {
        desiredValues[v.key] = v.value;
        if (v.enabled && (!v.onlyIfNonEmpty || !v.value.isEmpty())) {
            desiredActive.insert(v.key);
        }
    }

    // Process existing lines, updating managed keys in place
    QStringList output;
    QSet<QString> handledKeys;

    for (const QString& line : m_originalLines) {
        QString trimmed = line.trimmed();

        // Preserve blank lines and section headers
        if (trimmed.isEmpty() || trimmed.startsWith("###")) {
            output.append(line);
            continue;
        }

        bool commented = trimmed.startsWith('#');
        QString content = commented ? trimmed.mid(1).trimmed() : trimmed;

        int eqPos = content.indexOf('=');
        QString key = (eqPos >= 0) ? content.left(eqPos).trimmed() : content.trimmed();

        if (!managedKeys.contains(key)) {
            output.append(line);
            continue;
        }

        handledKeys.insert(key);

        if (desiredActive.contains(key)) {
            // Write active line
            if (desiredValues.contains(key))
                output.append(key + "=" + desiredValues[key]);
            else
                output.append(key);
        } else {
            // Comment it out
            if (desiredValues.contains(key))
                output.append("# " + key + "=" + desiredValues[key]);
            else
                output.append("# " + key);
        }
    }

    // Append any managed keys that weren't in the original file
    for (const auto& t : toggles) {
        if (!handledKeys.contains(t.key) && t.enabled)
            output.append(t.key);
    }
    for (const auto& v : values) {
        if (!handledKeys.contains(v.key) && desiredActive.contains(v.key))
            output.append(v.key + "=" + desiredValues[v.key]);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error",
            QString("Could not write to %1:\n%2").arg(path, file.errorString()));
        return;
    }

    QTextStream out(&file);
    for (const QString& l : output)
        out << l << "\n";
}
