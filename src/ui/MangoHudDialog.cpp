#include "MangoHudDialog.h"
#include "AppStyle.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QProcess>
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

MangoHudDialog::MangoHudDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("MangoHud Configuration");
    setMinimumSize(850, 550);
    resize(920, 650);
    setupUI();
    loadConfig();
}

bool MangoHudDialog::isMangoHudInstalled()
{
    QProcess process;
    process.start("which", {"mangohud"});
    process.waitForFinished(1000);
    return process.exitCode() == 0;
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
    mainLayout->addWidget(scrollArea, 1);

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
