#include "GPUInfoDialog.h"
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

GPUInfoDialog::GPUInfoDialog(const QList<GPUInfo>& gpus, QWidget* parent)
    : QDialog(parent)
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
    connect(m_refreshTimer, &QTimer::timeout, this, &GPUInfoDialog::refreshDynamicValues);
}

GPUInfoDialog::~GPUInfoDialog()
{
    if (m_refreshTimer->isActive()) {
        m_refreshTimer->stop();
    }
}

void GPUInfoDialog::setupUI()
{
    setWindowTitle("GPU Information");
    setMinimumSize(700, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    if (m_gpus.size() > 1) {
        // Multiple GPUs: use tabs
        m_tabWidget = new QTabWidget(this);
        for (int i = 0; i < m_gpus.size(); ++i) {
            QWidget* tab = createGPUTab(m_gpus[i], i);
            m_tabWidget->addTab(tab, QString("GPU %1").arg(i));
        }
        mainLayout->addWidget(m_tabWidget);
    } else if (m_gpus.size() == 1) {
        // Single GPU: no tabs needed
        QWidget* content = createGPUTab(m_gpus[0], 0);
        mainLayout->addWidget(content);
    }

    // Bottom controls
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    // Auto-refresh checkbox
    m_autoRefreshCheckbox = new QCheckBox("Auto-Refresh (1.5s)", this);
    m_autoRefreshCheckbox->setChecked(true); // Start enabled by default
    connect(m_autoRefreshCheckbox, &QCheckBox::toggled, this, &GPUInfoDialog::toggleAutoRefresh);
    buttonLayout->addWidget(m_autoRefreshCheckbox);

    buttonLayout->addStretch();

    QPushButton* copyButton = new QPushButton("Copy to Clipboard", this);
    connect(copyButton, &QPushButton::clicked, this, &GPUInfoDialog::copyToClipboard);
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

QWidget* GPUInfoDialog::createGPUTab(const GPUInfo& gpu, int gpuIndex)
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
    layout->addWidget(createClocksPowerGroup(gpu, gpuIndex));

    layout->addStretch();

    scrollArea->setWidget(widget);
    return scrollArea;
}

QGroupBox* GPUInfoDialog::createGraphicsCardGroup(const GPUInfo& gpu)
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

QGroupBox* GPUInfoDialog::createMemoryGroup(const GPUInfo& gpu)
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

QGroupBox* GPUInfoDialog::createDriverBiosGroup(const GPUInfo& gpu)
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

QGroupBox* GPUInfoDialog::createPCIeGroup(const GPUInfo& gpu)
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

QGroupBox* GPUInfoDialog::createClocksPowerGroup(const GPUInfo& gpu, int gpuIndex)
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

QLabel* GPUInfoDialog::addInfoRow(QVBoxLayout* layout, const QString& label, const QString& value)
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

QString GPUInfoDialog::vendorToString(GPUInfo::Vendor vendor)
{
    switch (vendor) {
        case GPUInfo::NVIDIA: return "NVIDIA";
        case GPUInfo::AMD: return "AMD";
        case GPUInfo::Intel: return "Intel";
        default: return "Unknown";
    }
}

void GPUInfoDialog::refreshDynamicValues()
{
    // Fetch fresh GPU data
    QList<GPUInfo> freshGpus = GPUDetector::detectAllGPUs();

    if (freshGpus.size() != m_gpus.size()) {
        // GPU configuration changed, stop refreshing
        m_refreshTimer->stop();
        if (m_autoRefreshCheckbox) {
            m_autoRefreshCheckbox->setChecked(false);
        }
        return;
    }

    // Update stored GPU data and UI labels
    for (int i = 0; i < freshGpus.size(); ++i) {
        const GPUInfo& fresh = freshGpus[i];
        m_gpus[i] = fresh; // Update stored data

        const DynamicLabels& labels = m_dynamicLabels[i];

        // Update dynamic labels if they exist
        if (labels.gpuClock && fresh.currentGraphicsClock > 0) {
            labels.gpuClock->setText(QString("%1 MHz").arg(fresh.currentGraphicsClock));
        }
        if (labels.memoryClock && fresh.currentMemoryClock > 0) {
            labels.memoryClock->setText(QString("%1 MHz").arg(fresh.currentMemoryClock));
        }
        if (labels.powerDraw && fresh.currentPowerDraw > 0) {
            labels.powerDraw->setText(QString("%1 W").arg(fresh.currentPowerDraw));
        }
        if (labels.temperature && fresh.temperature > 0) {
            labels.temperature->setText(QString("%1 °C").arg(fresh.temperature));
        }
        if (labels.fanSpeed && fresh.fanSpeed > 0) {
            labels.fanSpeed->setText(QString("%1 %").arg(fresh.fanSpeed));
        }
        if (labels.performanceState && !fresh.performanceState.isEmpty()) {
            labels.performanceState->setText(fresh.performanceState);
        }
    }
}

void GPUInfoDialog::toggleAutoRefresh(bool enabled)
{
    if (enabled) {
        m_refreshTimer->start();
        // Immediately refresh once
        refreshDynamicValues();
    } else {
        m_refreshTimer->stop();
    }
}

void GPUInfoDialog::copyToClipboard()
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

        if (i < m_gpus.size() - 1) {
            text += "\n";
        }
    }

    QApplication::clipboard()->setText(text);
}
