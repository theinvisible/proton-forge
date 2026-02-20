#include "SystemInfoDialog.h"
#include "utils/CPUDetector.h"
#include <QtConcurrent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QClipboard>
#include <QApplication>
#include <QScrollArea>

SystemInfoDialog::SystemInfoDialog(const QList<GPUInfo>& gpus, QWidget* parent)
    : QDialog(parent)
    , m_cpuInfo(CPUDetector::detect())
    , m_gpus(gpus)
    , m_tabWidget(nullptr)
    , m_refreshTimer(new QTimer(this))
    , m_autoRefreshCheckbox(nullptr)
{
    // Initialize dynamic labels list
    for (int i = 0; i < gpus.size(); ++i) {
        m_dynamicLabels.append(DynamicLabels());
    }

    setupUI();

    // Setup refresh timer
    m_refreshTimer->setInterval(1500); // 1.5 seconds
    connect(m_refreshTimer, &QTimer::timeout, this, &SystemInfoDialog::refreshDynamicValues);
}

SystemInfoDialog::~SystemInfoDialog()
{
    if (m_refreshTimer->isActive()) {
        m_refreshTimer->stop();
    }
}

void SystemInfoDialog::setupUI()
{
    setWindowTitle("System Information");
    setMinimumSize(700, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Always use tabs: CPU first, then one tab per GPU
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(createCPUTab(), "CPU");
    for (int i = 0; i < m_gpus.size(); ++i) {
        const QString label = m_gpus.size() > 1 ? QString("GPU %1").arg(i) : "GPU";
        m_tabWidget->addTab(createGPUTab(m_gpus[i], i), label);
    }
    mainLayout->addWidget(m_tabWidget);

    // Bottom controls
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    // Auto-refresh checkbox
    m_autoRefreshCheckbox = new QCheckBox("Auto-Refresh (1.5s)", this);
    m_autoRefreshCheckbox->setChecked(true); // Start enabled by default
    connect(m_autoRefreshCheckbox, &QCheckBox::toggled, this, &SystemInfoDialog::toggleAutoRefresh);
    buttonLayout->addWidget(m_autoRefreshCheckbox);

    buttonLayout->addStretch();

    QPushButton* copyButton = new QPushButton("Copy to Clipboard", this);
    connect(copyButton, &QPushButton::clicked, this, &SystemInfoDialog::copyToClipboard);
    buttonLayout->addWidget(copyButton);

    QPushButton* closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton);

    mainLayout->addLayout(buttonLayout);

    // Start timer if auto-refresh is enabled
    if (m_autoRefreshCheckbox->isChecked()) {
        m_refreshTimer->start();
    }

    // Apply dark theme styling
    setStyleSheet(
        "QDialog {"
        "    background-color: #1e1e1e;"
        "    color: #cccccc;"
        "}"
        "QGroupBox {"
        "    font-weight: bold;"
        "    border: 1px solid #444;"
        "    border-radius: 4px;"
        "    margin-top: 12px;"
        "    padding-top: 8px;"
        "    background-color: #2a2a2a;"
        "    color: #cccccc;"
        "}"
        "QGroupBox::title {"
        "    color: #76B900;"
        "    subcontrol-origin: margin;"
        "    left: 10px;"
        "    padding: 0 5px 0 5px;"
        "}"
        "QLabel {"
        "    color: #cccccc;"
        "}"
        "QCheckBox {"
        "    color: #cccccc;"
        "    spacing: 8px;"
        "}"
        "QCheckBox::indicator {"
        "    width: 18px;"
        "    height: 18px;"
        "    border: 1px solid #555555;"
        "    border-radius: 3px;"
        "    background-color: #2a2a2a;"
        "}"
        "QCheckBox::indicator:checked {"
        "    background-color: #76B900;"
        "    border: 1px solid #76B900;"
        "}"
        "QCheckBox::indicator:hover {"
        "    border: 1px solid #76B900;"
        "}"
        "QPushButton {"
        "    background-color: #333333;"
        "    color: #cccccc;"
        "    border: 1px solid #555555;"
        "    padding: 6px 16px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #404040;"
        "    border: 1px solid #76B900;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #2a2a2a;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #444;"
        "    background-color: #1e1e1e;"
        "}"
        "QTabBar::tab {"
        "    background-color: #2a2a2a;"
        "    color: #cccccc;"
        "    padding: 8px 16px;"
        "    border: 1px solid #444;"
        "    border-bottom: none;"
        "    border-top-left-radius: 4px;"
        "    border-top-right-radius: 4px;"
        "}"
        "QTabBar::tab:selected {"
        "    background-color: #333333;"
        "    border-bottom: 2px solid #76B900;"
        "}"
        "QTabBar::tab:hover {"
        "    background-color: #404040;"
        "}"
        "QScrollArea {"
        "    border: none;"
        "    background-color: #1e1e1e;"
        "}"
    );
}

QString SystemInfoDialog::formatCacheSize(int kib)
{
    if (kib <= 0)       return QString();
    if (kib < 1024)     return QString("%1 KiB").arg(kib);
    return QString("%1 MiB").arg(kib / 1024);
}

QWidget* SystemInfoDialog::createCPUTab()
{
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setSpacing(10);

    layout->addWidget(createCPUProcessorGroup());
    layout->addWidget(createCPUFreqGroup());
    if (m_cpuInfo.l1dCacheKiB > 0 || m_cpuInfo.l2CacheKiB > 0 || m_cpuInfo.l3CacheKiB > 0)
        layout->addWidget(createCPUCacheGroup());
    layout->addStretch();

    scroll->setWidget(widget);
    return scroll;
}

QGroupBox* SystemInfoDialog::createCPUProcessorGroup()
{
    QGroupBox* group = new QGroupBox("Processor");
    QVBoxLayout* layout = new QVBoxLayout(group);

    if (!m_cpuInfo.modelName.isEmpty())
        addInfoRow(layout, "Name:", m_cpuInfo.modelName);
    if (!m_cpuInfo.vendor.isEmpty())
        addInfoRow(layout, "Vendor:", m_cpuInfo.vendor);
    if (!m_cpuInfo.architecture.isEmpty())
        addInfoRow(layout, "Architecture:", m_cpuInfo.architecture);
    if (m_cpuInfo.physicalCores > 0)
        addInfoRow(layout, "Physical Cores:", QString::number(m_cpuInfo.physicalCores));
    if (m_cpuInfo.logicalCores > 0)
        addInfoRow(layout, "Logical CPUs:", QString::number(m_cpuInfo.logicalCores));

    return group;
}

QGroupBox* SystemInfoDialog::createCPUFreqGroup()
{
    QGroupBox* group = new QGroupBox("Frequencies & Temperature");
    QVBoxLayout* layout = new QVBoxLayout(group);

    if (m_cpuInfo.baseFreqMHz > 0)
        addInfoRow(layout, "Base Frequency:", QString("%1 MHz").arg(m_cpuInfo.baseFreqMHz, 0, 'f', 0));
    if (m_cpuInfo.maxFreqMHz > 0)
        addInfoRow(layout, "Max Frequency:", QString("%1 MHz").arg(m_cpuInfo.maxFreqMHz, 0, 'f', 0));

    // Dynamic labels — refreshed by the timer
    if (m_cpuInfo.currentFreqMHz > 0 || m_cpuInfo.baseFreqMHz > 0) {
        const QString freqStr = m_cpuInfo.currentFreqMHz > 0
            ? QString("%1 MHz").arg(m_cpuInfo.currentFreqMHz, 0, 'f', 0)
            : QString("—");
        m_cpuDynamic.currentFreq = addInfoRow(layout, "Current Frequency:", freqStr);
    }
    if (m_cpuInfo.temperature > 0) {
        m_cpuDynamic.temperature = addInfoRow(layout, "Temperature:",
                                              QString("%1 °C").arg(m_cpuInfo.temperature));
    } else {
        // Still create the label so it can be filled on the first refresh
        m_cpuDynamic.temperature = addInfoRow(layout, "Temperature:", "—");
    }

    return group;
}

QGroupBox* SystemInfoDialog::createCPUCacheGroup()
{
    QGroupBox* group = new QGroupBox("Cache");
    QVBoxLayout* layout = new QVBoxLayout(group);

    if (m_cpuInfo.l1dCacheKiB > 0)
        addInfoRow(layout, "L1d Cache:", formatCacheSize(m_cpuInfo.l1dCacheKiB));
    if (m_cpuInfo.l1iCacheKiB > 0)
        addInfoRow(layout, "L1i Cache:", formatCacheSize(m_cpuInfo.l1iCacheKiB));
    if (m_cpuInfo.l2CacheKiB > 0)
        addInfoRow(layout, "L2 Cache:", formatCacheSize(m_cpuInfo.l2CacheKiB));
    if (m_cpuInfo.l3CacheKiB > 0)
        addInfoRow(layout, "L3 Cache:", formatCacheSize(m_cpuInfo.l3CacheKiB));

    return group;
}

QWidget* SystemInfoDialog::createGPUTab(const GPUInfo& gpu, int gpuIndex)
{
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setSpacing(10);

    layout->addWidget(createGraphicsCardGroup(gpu));
    layout->addWidget(createMemoryGroup(gpu));
    layout->addWidget(createDriverBiosGroup(gpu));
    layout->addWidget(createPCIeGroup(gpu));
    layout->addWidget(createUtilizationGroup(gpu, gpuIndex));
    layout->addWidget(createClocksPowerGroup(gpu, gpuIndex));

    layout->addStretch();

    scrollArea->setWidget(widget);
    return scrollArea;
}

QGroupBox* SystemInfoDialog::createGraphicsCardGroup(const GPUInfo& gpu)
{
    QGroupBox* group = new QGroupBox("Graphics Card");
    QVBoxLayout* layout = new QVBoxLayout(group);

    addInfoRow(layout, "Name:", gpu.name);
    addInfoRow(layout, "Vendor:", vendorToString(gpu.vendor));
    if (!gpu.architecture.isEmpty())
        addInfoRow(layout, "Architecture:", gpu.architecture);
    if (gpu.cudaCores > 0)
        addInfoRow(layout, "CUDA Cores:", QString::number(gpu.cudaCores));
    if (!gpu.gpuPartNumber.isEmpty())
        addInfoRow(layout, "GPU Part Number:", gpu.gpuPartNumber);
    if (!gpu.computeCapability.isEmpty())
        addInfoRow(layout, "Compute Capability:", gpu.computeCapability);

    return group;
}

QGroupBox* SystemInfoDialog::createMemoryGroup(const GPUInfo& gpu)
{
    QGroupBox* group = new QGroupBox("Memory");
    QVBoxLayout* layout = new QVBoxLayout(group);

    if (gpu.memoryTotalMB > 0) {
        QString memoryStr = QString("%1 MB (%2 GB)")
            .arg(gpu.memoryTotalMB)
            .arg(gpu.memoryTotalMB / 1024.0, 0, 'f', 2);
        addInfoRow(layout, "Total Memory:", memoryStr);
    }

    // Max Memory Clock (static value)
    if (gpu.maxMemoryClock > 0)
        addInfoRow(layout, "Max Memory Clock:", QString("%1 MHz").arg(gpu.maxMemoryClock));

    return group;
}

QGroupBox* SystemInfoDialog::createDriverBiosGroup(const GPUInfo& gpu)
{
    QGroupBox* group = new QGroupBox("Driver & BIOS");
    QVBoxLayout* layout = new QVBoxLayout(group);

    if (!gpu.driverVersion.isEmpty())
        addInfoRow(layout, "Driver Version:", gpu.driverVersion);
    if (!gpu.cudaVersion.isEmpty())
        addInfoRow(layout, "CUDA Version:", gpu.cudaVersion);
    if (!gpu.vbiosVersion.isEmpty())
        addInfoRow(layout, "VBIOS Version:", gpu.vbiosVersion);
    if (!gpu.uuid.isEmpty())
        addInfoRow(layout, "UUID:", gpu.uuid);

    return group;
}

QGroupBox* SystemInfoDialog::createPCIeGroup(const GPUInfo& gpu)
{
    QGroupBox* group = new QGroupBox("PCIe Interface");
    QVBoxLayout* layout = new QVBoxLayout(group);

    if (!gpu.pciId.isEmpty())
        addInfoRow(layout, "Bus ID:", gpu.pciId);
    if (!gpu.pcieCurrentGen.isEmpty())
        addInfoRow(layout, "Current Link:", gpu.pcieCurrentGen);
    if (!gpu.pcieMaxGen.isEmpty())
        addInfoRow(layout, "Max Link:", gpu.pcieMaxGen);
    if (!gpu.pcieLinkWidth.isEmpty())
        addInfoRow(layout, "Link Width:", gpu.pcieLinkWidth);
    if (!gpu.pcieLinkSpeed.isEmpty())
        addInfoRow(layout, "Link Speed:", gpu.pcieLinkSpeed);

    // Resizeable BAR status
    QString barStatus;
    if (gpu.bar1TotalMB > 0) {
        if (gpu.resizeableBarEnabled) {
            barStatus = QString("✓ Enabled (%1 MB)").arg(gpu.bar1TotalMB);
        } else {
            barStatus = QString("✗ Disabled (%1 MB)").arg(gpu.bar1TotalMB);
        }
        addInfoRow(layout, "Resizeable BAR:", barStatus);
    }

    return group;
}

QGroupBox* SystemInfoDialog::createUtilizationGroup(const GPUInfo& gpu, int gpuIndex)
{
    QGroupBox* group = new QGroupBox("Utilization");
    QVBoxLayout* layout = new QVBoxLayout(group);

    // Store dynamic labels for auto-refresh
    DynamicLabels& labels = m_dynamicLabels[gpuIndex];

    labels.gpuUtilization = addInfoRow(layout, "GPU:", QString("%1 %").arg(gpu.gpuUtilization));
    labels.memoryUtilization = addInfoRow(layout, "Memory:", QString("%1 %").arg(gpu.memoryUtilization));
    labels.encoderUtilization = addInfoRow(layout, "Encoder:", QString("%1 %").arg(gpu.encoderUtilization));
    labels.decoderUtilization = addInfoRow(layout, "Decoder:", QString("%1 %").arg(gpu.decoderUtilization));
    labels.jpegUtilization = addInfoRow(layout, "JPEG:", QString("%1 %").arg(gpu.jpegUtilization));
    labels.ofaUtilization = addInfoRow(layout, "OFA:", QString("%1 %").arg(gpu.ofaUtilization));

    return group;
}

QGroupBox* SystemInfoDialog::createClocksPowerGroup(const GPUInfo& gpu, int gpuIndex)
{
    QGroupBox* group = new QGroupBox("Clocks & Power");
    QVBoxLayout* layout = new QVBoxLayout(group);

    // Store dynamic labels for auto-refresh
    DynamicLabels& labels = m_dynamicLabels[gpuIndex];

    if (gpu.currentGraphicsClock > 0)
        labels.gpuClock = addInfoRow(layout, "GPU Clock:", QString("%1 MHz").arg(gpu.currentGraphicsClock));
    if (gpu.currentMemoryClock > 0)
        labels.memoryClock = addInfoRow(layout, "Memory Clock:", QString("%1 MHz").arg(gpu.currentMemoryClock));
    if (gpu.currentPowerDraw > 0)
        labels.powerDraw = addInfoRow(layout, "Power Draw:", QString("%1 W").arg(gpu.currentPowerDraw));
    if (gpu.powerLimit > 0)
        addInfoRow(layout, "Power Limit:", QString("%1 W").arg(gpu.powerLimit));
    if (gpu.temperature > 0)
        labels.temperature = addInfoRow(layout, "Temperature:", QString("%1 °C").arg(gpu.temperature));
    if (gpu.fanSpeed > 0)
        labels.fanSpeed = addInfoRow(layout, "Fan Speed:", QString("%1 %").arg(gpu.fanSpeed));
    if (!gpu.performanceState.isEmpty())
        labels.performanceState = addInfoRow(layout, "Performance State:", gpu.performanceState);

    return group;
}

QLabel* SystemInfoDialog::addInfoRow(QVBoxLayout* layout, const QString& label, const QString& value)
{
    if (value.isEmpty()) {
        return nullptr;
    }

    QHBoxLayout* rowLayout = new QHBoxLayout();

    QLabel* labelWidget = new QLabel(label);
    labelWidget->setStyleSheet("color: #aaaaaa;");
    labelWidget->setMinimumWidth(150);
    rowLayout->addWidget(labelWidget);

    QLabel* valueWidget = new QLabel(value);
    valueWidget->setStyleSheet("font-family: monospace; color: #cccccc;");
    valueWidget->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rowLayout->addWidget(valueWidget, 1);

    layout->addLayout(rowLayout);

    return valueWidget; // Return the value label so it can be updated
}

QString SystemInfoDialog::vendorToString(GPUInfo::Vendor vendor)
{
    switch (vendor) {
        case GPUInfo::NVIDIA: return "NVIDIA";
        case GPUInfo::AMD: return "AMD";
        case GPUInfo::Intel: return "Intel";
        default: return "Unknown";
    }
}

void SystemInfoDialog::refreshDynamicValues()
{
    // Skip if a refresh is already running — avoids piling up concurrent nvidia-smi calls
    if (m_refreshInProgress)
        return;
    m_refreshInProgress = true;

    // Capture a copy of m_cpuInfo for the background thread (no shared mutable state)
    const CPUInfo cpuBase = m_cpuInfo;

    using Result = QPair<CPUInfo, QList<GPUInfo>>;
    auto* watcher = new QFutureWatcher<Result>(this);

    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        m_refreshInProgress = false;
        const Result result = watcher->result();
        applyRefreshResult(result.first, result.second);
    });

    watcher->setFuture(QtConcurrent::run([cpuBase]() -> Result {
        return { CPUDetector::detectDynamic(cpuBase), GPUDetector::detectAllGPUs() };
    }));
}

void SystemInfoDialog::applyRefreshResult(const CPUInfo& freshCpu, const QList<GPUInfo>& freshGpus)
{
    // ── CPU ──────────────────────────────────────────────────────────────────
    m_cpuInfo = freshCpu;
    if (m_cpuDynamic.currentFreq) {
        m_cpuDynamic.currentFreq->setText(
            freshCpu.currentFreqMHz > 0
                ? QString("%1 MHz").arg(freshCpu.currentFreqMHz, 0, 'f', 0)
                : QString("—"));
    }
    if (m_cpuDynamic.temperature) {
        m_cpuDynamic.temperature->setText(
            freshCpu.temperature > 0
                ? QString("%1 °C").arg(freshCpu.temperature)
                : QString("—"));
    }

    // ── GPU ──────────────────────────────────────────────────────────────────
    if (freshGpus.size() != m_gpus.size()) {
        m_refreshTimer->stop();
        if (m_autoRefreshCheckbox)
            m_autoRefreshCheckbox->setChecked(false);
        return;
    }

    for (int i = 0; i < freshGpus.size(); ++i) {
        const GPUInfo& fresh = freshGpus[i];
        m_gpus[i] = fresh;

        const DynamicLabels& labels = m_dynamicLabels[i];

        if (labels.gpuClock && fresh.currentGraphicsClock > 0)
            labels.gpuClock->setText(QString("%1 MHz").arg(fresh.currentGraphicsClock));
        if (labels.memoryClock && fresh.currentMemoryClock > 0)
            labels.memoryClock->setText(QString("%1 MHz").arg(fresh.currentMemoryClock));
        if (labels.powerDraw && fresh.currentPowerDraw > 0)
            labels.powerDraw->setText(QString("%1 W").arg(fresh.currentPowerDraw));
        if (labels.temperature && fresh.temperature > 0)
            labels.temperature->setText(QString("%1 °C").arg(fresh.temperature));
        if (labels.fanSpeed && fresh.fanSpeed > 0)
            labels.fanSpeed->setText(QString("%1 %").arg(fresh.fanSpeed));
        if (labels.performanceState && !fresh.performanceState.isEmpty())
            labels.performanceState->setText(fresh.performanceState);

        if (labels.gpuUtilization)
            labels.gpuUtilization->setText(QString("%1 %").arg(fresh.gpuUtilization));
        if (labels.memoryUtilization)
            labels.memoryUtilization->setText(QString("%1 %").arg(fresh.memoryUtilization));
        if (labels.encoderUtilization)
            labels.encoderUtilization->setText(QString("%1 %").arg(fresh.encoderUtilization));
        if (labels.decoderUtilization)
            labels.decoderUtilization->setText(QString("%1 %").arg(fresh.decoderUtilization));
        if (labels.jpegUtilization)
            labels.jpegUtilization->setText(QString("%1 %").arg(fresh.jpegUtilization));
        if (labels.ofaUtilization)
            labels.ofaUtilization->setText(QString("%1 %").arg(fresh.ofaUtilization));
    }
}

void SystemInfoDialog::toggleAutoRefresh(bool enabled)
{
    if (enabled) {
        m_refreshTimer->start();
        // Immediately refresh once
        refreshDynamicValues();
    } else {
        m_refreshTimer->stop();
    }
}

void SystemInfoDialog::copyToClipboard()
{
    QString text;

    for (int i = 0; i < m_gpus.size(); ++i) {
        const GPUInfo& gpu = m_gpus[i];

        if (m_gpus.size() > 1) {
            text += QString("=== GPU %1 ===\n").arg(i);
        }

        text += QString("Name: %1\n").arg(gpu.name);
        text += QString("Vendor: %1\n").arg(vendorToString(gpu.vendor));

        if (!gpu.architecture.isEmpty())
            text += QString("Architecture: %1\n").arg(gpu.architecture);
        if (gpu.cudaCores > 0)
            text += QString("CUDA Cores: %1\n").arg(gpu.cudaCores);
        if (!gpu.gpuPartNumber.isEmpty())
            text += QString("GPU Part Number: %1\n").arg(gpu.gpuPartNumber);
        if (!gpu.computeCapability.isEmpty())
            text += QString("Compute Capability: %1\n").arg(gpu.computeCapability);

        if (gpu.memoryTotalMB > 0)
            text += QString("Total Memory: %1 MB (%2 GB)\n")
                .arg(gpu.memoryTotalMB)
                .arg(gpu.memoryTotalMB / 1024.0, 0, 'f', 2);

        if (!gpu.driverVersion.isEmpty())
            text += QString("Driver Version: %1\n").arg(gpu.driverVersion);
        if (!gpu.cudaVersion.isEmpty())
            text += QString("CUDA Version: %1\n").arg(gpu.cudaVersion);
        if (!gpu.vbiosVersion.isEmpty())
            text += QString("VBIOS Version: %1\n").arg(gpu.vbiosVersion);
        if (!gpu.uuid.isEmpty())
            text += QString("UUID: %1\n").arg(gpu.uuid);

        if (!gpu.pciId.isEmpty())
            text += QString("Bus ID: %1\n").arg(gpu.pciId);
        if (!gpu.pcieCurrentGen.isEmpty())
            text += QString("Current Link: %1\n").arg(gpu.pcieCurrentGen);
        if (!gpu.pcieMaxGen.isEmpty())
            text += QString("Max Link: %1\n").arg(gpu.pcieMaxGen);
        if (!gpu.pcieLinkWidth.isEmpty())
            text += QString("Link Width: %1\n").arg(gpu.pcieLinkWidth);
        if (!gpu.pcieLinkSpeed.isEmpty())
            text += QString("Link Speed: %1\n").arg(gpu.pcieLinkSpeed);
        if (gpu.bar1TotalMB > 0) {
            QString barStatus = gpu.resizeableBarEnabled
                ? QString("Enabled (%1 MB)").arg(gpu.bar1TotalMB)
                : QString("Disabled (%1 MB)").arg(gpu.bar1TotalMB);
            text += QString("Resizeable BAR: %1\n").arg(barStatus);
        }

        if (gpu.currentGraphicsClock > 0)
            text += QString("GPU Clock: %1 MHz\n").arg(gpu.currentGraphicsClock);
        if (gpu.currentMemoryClock > 0)
            text += QString("Memory Clock: %1 MHz\n").arg(gpu.currentMemoryClock);
        if (gpu.currentPowerDraw > 0)
            text += QString("Power Draw: %1 W\n").arg(gpu.currentPowerDraw);
        if (gpu.powerLimit > 0)
            text += QString("Power Limit: %1 W\n").arg(gpu.powerLimit);
        if (gpu.temperature > 0)
            text += QString("Temperature: %1 °C\n").arg(gpu.temperature);
        if (gpu.fanSpeed > 0)
            text += QString("Fan Speed: %1 %%\n").arg(gpu.fanSpeed);
        if (!gpu.performanceState.isEmpty())
            text += QString("Performance State: %1\n").arg(gpu.performanceState);

        // Utilization
        text += QString("\nUtilization:\n");
        text += QString("  GPU: %1 %%\n").arg(gpu.gpuUtilization);
        text += QString("  Memory: %1 %%\n").arg(gpu.memoryUtilization);
        text += QString("  Encoder: %1 %%\n").arg(gpu.encoderUtilization);
        text += QString("  Decoder: %1 %%\n").arg(gpu.decoderUtilization);
        text += QString("  JPEG: %1 %%\n").arg(gpu.jpegUtilization);
        text += QString("  OFA: %1 %%\n").arg(gpu.ofaUtilization);

        if (i < m_gpus.size() - 1) {
            text += "\n";
        }
    }

    QApplication::clipboard()->setText(text);
}
