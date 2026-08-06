#include "SystemInfoDialog.h"
#include "AppStyle.h"
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
    , m_displays(DisplayDetector::detect())
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
    if (m_gpus.isEmpty()) {
        // No GPU is not a reason to withhold the dialog — CPU and monitor details
        // are still there. Explain the gap where the GPU data would have been.
        m_tabWidget->addTab(createNoGpuTab(), "GPU");
    }
    for (int i = 0; i < m_gpus.size(); ++i) {
        const QString label = m_gpus.size() > 1 ? QString("GPU %1").arg(i) : "GPU";
        m_tabWidget->addTab(createGPUTab(m_gpus[i], i), label);
    }
    if (!m_displays.isEmpty())
        m_tabWidget->addTab(createMonitorTab(), "Monitor");
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

    // Apply dark theme styling directly (app-level class selectors don't reliably
    // reach into modal dialogs created on-demand)
    setStyleSheet(QString(
        "QDialog {"
        "    background-color: %1;"
        "    color: #cccccc;"
        "}"
        "QGroupBox {"
        "    font-weight: bold;"
        "    border: 1px solid #444;"
        "    border-radius: 4px;"
        "    margin-top: 12px;"
        "    padding-top: 8px;"
        "    background-color: %2;"
        "    color: #cccccc;"
        "}"
        "QGroupBox::title {"
        "    color: %3;"
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
        "    border: 1px solid %4;"
        "    border-radius: 3px;"
        "    background-color: %2;"
        "}"
        "QCheckBox::indicator:checked {"
        "    background-color: %3;"
        "    border: 1px solid %3;"
        "}"
        "QCheckBox::indicator:hover {"
        "    border: 1px solid %3;"
        "}"
        "QPushButton {"
        "    background-color: #333333;"
        "    color: #cccccc;"
        "    border: 1px solid %4;"
        "    padding: 6px 16px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #404040;"
        "    border: 1px solid %3;"
        "}"
        "QPushButton:pressed {"
        "    background-color: %2;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #444;"
        "    background-color: %1;"
        "}"
        "QTabBar::tab {"
        "    background-color: %2;"
        "    color: #cccccc;"
        "    padding: 8px 16px;"
        "    border: 1px solid #444;"
        "    border-bottom: none;"
        "    border-top-left-radius: 4px;"
        "    border-top-right-radius: 4px;"
        "}"
        "QTabBar::tab:selected {"
        "    background-color: #333333;"
        "    border-bottom: 2px solid %3;"
        "}"
        "QTabBar::tab:hover {"
        "    background-color: #404040;"
        "}"
        "QScrollArea {"
        "    border: none;"
        "    background-color: %1;"
        "}"
        "QScrollBar:vertical {"
        "    background: transparent;"
        "    width: 8px;"
        "    margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: %4;"
        "    border-radius: 4px;"
        "    min-height: 30px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: #6a6a6a;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    height: 0;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: transparent;"
        "}"
    ).arg(AppStyle::ColorBgInput, AppStyle::ColorBgElevated,
          AppStyle::ColorAccent, AppStyle::ColorBorderLight));
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
    if (!m_cpuInfo.simd.isEmpty() || !m_cpuInfo.crypto.isEmpty() ||
        !m_cpuInfo.virtualization.isEmpty() || !m_cpuInfo.otherIsa.isEmpty())
        layout->addWidget(createCPUInstructionSetsGroup());
    layout->addWidget(createCPUFreqGroup());
    layout->addWidget(createCPUUtilizationGroup());
    if (!m_cpuInfo.perCoreFreqMHz.isEmpty())
        layout->addWidget(createCPUPerCoreFreqGroup());
    if (!m_cpuInfo.tempSensors.isEmpty())
        layout->addWidget(createCPUPerCoreTempGroup());
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

    // Hybrid CPUs get a P/E breakdown; homogeneous CPUs the classic core/thread rows.
    if (m_cpuInfo.pCores > 0 && m_cpuInfo.eCores > 0) {
        addInfoRow(layout, "Cores:",
                   QString("%1 P + %2 E  (%3 cores, %4 threads)")
                       .arg(m_cpuInfo.pCores)
                       .arg(m_cpuInfo.eCores)
                       .arg(m_cpuInfo.pCores + m_cpuInfo.eCores)
                       .arg(m_cpuInfo.logicalCores));
    } else {
        if (m_cpuInfo.physicalCores > 0)
            addInfoRow(layout, "Physical Cores:", QString::number(m_cpuInfo.physicalCores));
        if (m_cpuInfo.logicalCores > 0)
            addInfoRow(layout, "Logical CPUs:", QString::number(m_cpuInfo.logicalCores));
    }
    if (m_cpuInfo.threadsPerCore > 0)
        addInfoRow(layout, "Threads per Core:", QString::number(m_cpuInfo.threadsPerCore));
    if (m_cpuInfo.sockets > 0)
        addInfoRow(layout, "Sockets:", QString::number(m_cpuInfo.sockets));
    if (m_cpuInfo.numaNodes > 0)
        addInfoRow(layout, "NUMA Nodes:", QString::number(m_cpuInfo.numaNodes));
    if (m_cpuInfo.cpuFamily > 0)
        addInfoRow(layout, "Family / Model / Stepping:",
                   QString("%1 / %2 / %3").arg(m_cpuInfo.cpuFamily)
                       .arg(m_cpuInfo.cpuModel).arg(m_cpuInfo.stepping));
    if (!m_cpuInfo.microcode.isEmpty())
        addInfoRow(layout, "Microcode:", m_cpuInfo.microcode);
    if (!m_cpuInfo.virtualization.isEmpty())
        addInfoRow(layout, "Virtualization:", m_cpuInfo.virtualization);

    return group;
}

QGroupBox* SystemInfoDialog::createCPUFreqGroup()
{
    QGroupBox* group = new QGroupBox("Frequencies & Power");
    QVBoxLayout* layout = new QVBoxLayout(group);

    // "Min Frequency" (this is lscpu's CPU min MHz — the old "Base Frequency" label
    // was a misnomer; the value is the minimum clock, not a nominal/base clock).
    if (m_cpuInfo.baseFreqMHz > 0)
        addInfoRow(layout, "Min Frequency:", QString("%1 MHz").arg(m_cpuInfo.baseFreqMHz, 0, 'f', 0));
    if (m_cpuInfo.maxFreqMHz > 0)
        addInfoRow(layout, "Max Frequency:", QString("%1 MHz").arg(m_cpuInfo.maxFreqMHz, 0, 'f', 0));

    // Dynamic labels — refreshed by the timer
    if (m_cpuInfo.currentFreqMHz > 0 || m_cpuInfo.baseFreqMHz > 0) {
        const QString freqStr = m_cpuInfo.currentFreqMHz > 0
            ? QString("%1 MHz").arg(m_cpuInfo.currentFreqMHz, 0, 'f', 0)
            : QString("—");
        m_cpuDynamic.currentFreq = addInfoRow(layout, "Current Frequency:", freqStr);
    }
    m_cpuDynamic.temperature = addInfoRow(layout, "Temperature:",
        m_cpuInfo.temperature > 0 ? QString("%1 °C").arg(m_cpuInfo.temperature) : QString("—"));

    if (!m_cpuInfo.governor.isEmpty())
        m_cpuDynamic.governor = addInfoRow(layout, "Governor:", m_cpuInfo.governor);
    if (!m_cpuInfo.scalingDriver.isEmpty())
        addInfoRow(layout, "Scaling Driver:", m_cpuInfo.scalingDriver);
    if (m_cpuInfo.turboEnabled >= 0)
        m_cpuDynamic.turbo = addInfoRow(layout, "Turbo / Boost:",
                                        m_cpuInfo.turboEnabled ? "Enabled" : "Disabled");
    if (!m_cpuInfo.epp.isEmpty())
        addInfoRow(layout, "Energy Pref (EPP):", m_cpuInfo.epp);
    if (m_cpuInfo.powerLimitLongW > 0)
        addInfoRow(layout, "Power Limit (PL1):", QString("%1 W").arg(m_cpuInfo.powerLimitLongW, 0, 'f', 0));
    if (m_cpuInfo.powerLimitShortW > 0)
        addInfoRow(layout, "Max Power (PL2):", QString("%1 W").arg(m_cpuInfo.powerLimitShortW, 0, 'f', 0));

    return group;
}

QGroupBox* SystemInfoDialog::createCPUInstructionSetsGroup()
{
    QGroupBox* group = new QGroupBox("Instruction Sets");
    QVBoxLayout* layout = new QVBoxLayout(group);

    if (!m_cpuInfo.simd.isEmpty())
        addInfoRow(layout, "SIMD:", m_cpuInfo.simd.join(", "));
    if (!m_cpuInfo.crypto.isEmpty())
        addInfoRow(layout, "Crypto:", m_cpuInfo.crypto.join(", "));
    if (!m_cpuInfo.otherIsa.isEmpty())
        addInfoRow(layout, "Other:", m_cpuInfo.otherIsa.join(", "));

    return group;
}

QGroupBox* SystemInfoDialog::createCPUUtilizationGroup()
{
    QGroupBox* group = new QGroupBox("Utilization");
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_cpuDynamic.utilization = addInfoRow(layout, "CPU Usage:",
        m_cpuInfo.cpuUtilization >= 0
            ? QString("%1 %").arg(m_cpuInfo.cpuUtilization, 0, 'f', 0) : QString("—"));
    m_cpuDynamic.loadAvg = addInfoRow(layout, "Load Average (1m):",
        QString("%1").arg(m_cpuInfo.loadAvg1, 0, 'f', 2));

    return group;
}

QGroupBox* SystemInfoDialog::createCPUPerCoreFreqGroup()
{
    QGroupBox* group = new QGroupBox("Per-Core Frequencies");
    QVBoxLayout* layout = new QVBoxLayout(group);

    const int n = m_cpuInfo.perCoreFreqMHz.size();
    m_cpuDynamic.perCoreFreq = QList<QLabel*>(n, nullptr);

    auto addCore = [&](int cpu, const QString& tag) {
        if (cpu < 0 || cpu >= n)
            return;
        m_cpuDynamic.perCoreFreq[cpu] = addInfoRow(layout,
            QString("CPU %1%2:").arg(cpu).arg(tag),
            QString("%1 MHz").arg(m_cpuInfo.perCoreFreqMHz[cpu], 0, 'f', 0));
    };

    // Group by P/E on hybrid CPUs (tag each core), else a flat list.
    if (!m_cpuInfo.pCoreCpus.isEmpty() && !m_cpuInfo.eCoreCpus.isEmpty()) {
        for (int cpu : m_cpuInfo.pCoreCpus) addCore(cpu, " (P)");
        for (int cpu : m_cpuInfo.eCoreCpus) addCore(cpu, " (E)");
    } else {
        for (int cpu = 0; cpu < n; ++cpu) addCore(cpu, "");
    }

    return group;
}

QGroupBox* SystemInfoDialog::createCPUPerCoreTempGroup()
{
    QGroupBox* group = new QGroupBox("Per-Core Temperatures");
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_cpuDynamic.perCoreTemp.clear();
    for (const QPair<QString, int>& s : m_cpuInfo.tempSensors) {
        QLabel* lbl = addInfoRow(layout, s.first + ":", QString("%1 °C").arg(s.second));
        m_cpuDynamic.perCoreTemp.append(lbl);
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

QWidget* SystemInfoDialog::createNoGpuTab()
{
    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setSpacing(10);

    QLabel* note = new QLabel(
        "⚠  Could not detect a compatible GPU.\n\n"
        "ProtonForge reads GPU details from the NVIDIA driver (nvidia-smi and "
        "/proc/driver/nvidia). Make sure the proprietary NVIDIA driver is installed "
        "and loaded.\n\n"
        "CPU and monitor details are unaffected and shown in the other tabs.");
    note->setWordWrap(true);
    note->setStyleSheet(QString("color: %1; background-color: %2; "
                                "border: 1px solid %1; border-radius: 4px; padding: 8px;")
                            .arg(AppStyle::ColorWarning, AppStyle::ColorBgElevated));
    layout->addWidget(note);
    layout->addStretch();

    return widget;
}

QWidget* SystemInfoDialog::createGPUTab(const GPUInfo& gpu, int gpuIndex)
{
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setSpacing(10);

    if (!gpu.telemetryAvailable) {
        // Optimus dGPU asleep in D3cold: show the static details plus an
        // explanatory note, and skip the live groups (clocks/power/utilization)
        // which would otherwise show misleading zeros.
        QLabel* note = new QLabel(
            "⚠  This GPU is in power-saving suspend (D3cold) and is not currently active. "
            "Live clocks, temperatures, and utilization are unavailable until an application "
            "uses it (e.g. via PRIME render offload). Static details are shown below.");
        note->setWordWrap(true);
        note->setStyleSheet(QString("color: %1; background-color: %2; "
                                    "border: 1px solid %1; border-radius: 4px; padding: 8px;")
                                .arg(AppStyle::ColorWarning, AppStyle::ColorBgElevated));
        layout->addWidget(note);

        layout->addWidget(createGraphicsCardGroup(gpu));
        layout->addWidget(createDriverInfoGroup(gpu));
        layout->addStretch();

        scrollArea->setWidget(widget);
        return scrollArea;
    }

    layout->addWidget(createGraphicsCardGroup(gpu));
    layout->addWidget(createMemoryGroup(gpu, gpuIndex));
    layout->addWidget(createDriverInfoGroup(gpu));
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

QGroupBox* SystemInfoDialog::createMemoryGroup(const GPUInfo& gpu, int gpuIndex)
{
    QGroupBox* group = new QGroupBox("Memory");
    QVBoxLayout* layout = new QVBoxLayout(group);

    DynamicLabels& labels = m_dynamicLabels[gpuIndex];

    if (gpu.memoryTotalMB > 0) {
        QString memoryStr = QString("%1 MB (%2 GB)")
            .arg(gpu.memoryTotalMB)
            .arg(gpu.memoryTotalMB / 1024.0, 0, 'f', 2);
        addInfoRow(layout, "Total Memory:", memoryStr);
    }

    // VRAM used/free are live values (NVML) — create unconditionally so they keep
    // updating even when a fresh value happens to be low.
    labels.vramUsed = addInfoRow(layout, "VRAM Used:", QString("%1 MB").arg(gpu.memoryUsedMB));
    labels.vramFree = addInfoRow(layout, "VRAM Free:", QString("%1 MB").arg(gpu.memoryFreeMB));

    // Memory bus width (static value)
    if (gpu.memoryBusWidth > 0)
        addInfoRow(layout, "Memory Bus Width:", QString("%1-bit").arg(gpu.memoryBusWidth));

    // Max Memory Clock (static value)
    if (gpu.maxMemoryClock > 0)
        addInfoRow(layout, "Max Memory Clock:", QString("%1 MHz").arg(gpu.maxMemoryClock));

    return group;
}

QGroupBox* SystemInfoDialog::createDriverInfoGroup(const GPUInfo& gpu)
{
    QGroupBox* group = new QGroupBox("Driver Information");
    QVBoxLayout* layout = new QVBoxLayout(group);

    const DriverInfo& driver = gpu.driverInfo;

    // Prefer the richer driverInfo.version, fall back to the plain
    // driverVersion reported by the vendor tool (nvidia-smi) so we still
    // show something useful when /proc/driver/nvidia/version is unavailable.
    const QString version = !driver.version.isEmpty() ? driver.version : gpu.driverVersion;
    if (!version.isEmpty())
        addInfoRow(layout, "Driver Version:", version);
    if (!driver.branch.isEmpty())
        addInfoRow(layout, "Driver Branch:", driver.branch);
    if (!driver.releaseDate.isEmpty())
        addInfoRow(layout, "Release Date:", driver.releaseDate);
    if (!driver.moduleType.isEmpty())
        addInfoRow(layout, "Kernel Module:", driver.moduleType);
    if (!driver.moduleName.isEmpty())
        addInfoRow(layout, "Module Name:", driver.moduleName);
    if (!gpu.cudaVersion.isEmpty())
        addInfoRow(layout, "CUDA Version:", gpu.cudaVersion);

    return group;
}

QGroupBox* SystemInfoDialog::createDriverBiosGroup(const GPUInfo& gpu)
{
    QGroupBox* group = new QGroupBox("BIOS & Identifiers");
    QVBoxLayout* layout = new QVBoxLayout(group);

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

// Human-readable rendering of an NVML clocks-throttle-reasons bitmask.
// -1 → unknown (return empty so no row is shown); 0 → "None"; otherwise the
// active reasons joined with commas. Bit values are the stable NVML
// nvmlClocksThrottleReason* constants.
QString SystemInfoDialog::throttleReasonsToString(qint64 mask)
{
    if (mask < 0)
        return QString();
    if (mask == 0)
        return "None";

    struct Reason { qint64 bit; const char* label; };
    static const Reason reasons[] = {
        { 0x0000000000000001LL, "GPU Idle" },
        { 0x0000000000000002LL, "Applications Clocks Setting" },
        { 0x0000000000000004LL, "SW Power Cap" },
        { 0x0000000000000008LL, "HW Slowdown" },
        { 0x0000000000000010LL, "Sync Boost" },
        { 0x0000000000000020LL, "SW Thermal Slowdown" },
        { 0x0000000000000040LL, "HW Thermal Slowdown" },
        { 0x0000000000000080LL, "HW Power Brake Slowdown" },
        { 0x0000000000000100LL, "Display Clock Setting" },
    };

    QStringList active;
    for (const Reason& r : reasons) {
        if (mask & r.bit)
            active << r.label;
    }
    return active.isEmpty() ? QString("0x%1").arg(mask, 0, 16) : active.join(", ");
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
    if (gpu.powerDefaultLimit > 0)
        addInfoRow(layout, "Default Power Limit:", QString("%1 W").arg(gpu.powerDefaultLimit));
    if (gpu.powerLimitMin > 0 && gpu.powerLimitMax > 0)
        addInfoRow(layout, "Power Limit Range:",
                   QString("%1 – %2 W").arg(gpu.powerLimitMin).arg(gpu.powerLimitMax));
    // Power source (AC/Battery) is live on laptops — keep a handle to update it.
    labels.powerSource = addInfoRow(layout, "Power Source:", gpu.powerSource);
    if (gpu.temperature > 0)
        labels.temperature = addInfoRow(layout, "Temperature:", QString("%1 °C").arg(gpu.temperature));
    if (gpu.tempSlowdown > 0)
        addInfoRow(layout, "Slowdown Temp:", QString("%1 °C").arg(gpu.tempSlowdown));
    if (gpu.tempShutdown > 0)
        addInfoRow(layout, "Shutdown Temp:", QString("%1 °C").arg(gpu.tempShutdown));
    if (gpu.fanSpeed > 0)
        labels.fanSpeed = addInfoRow(layout, "Fan Speed:", QString("%1 %").arg(gpu.fanSpeed));
    if (!gpu.performanceState.isEmpty())
        labels.performanceState = addInfoRow(layout, "Performance State:", gpu.performanceState);
    // Throttle reason is a live diagnostic — create it whenever NVML reported a
    // value (incl. "None"), so it updates when the GPU starts/stops throttling.
    labels.throttle = addInfoRow(layout, "Throttle Reason:", throttleReasonsToString(gpu.throttleReasons));

    return group;
}

QWidget* SystemInfoDialog::createMonitorTab()
{
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setSpacing(10);

    for (int i = 0; i < m_displays.size(); ++i)
        layout->addWidget(createMonitorGroup(m_displays[i], i, m_displays.size()));
    layout->addStretch();

    scroll->setWidget(widget);
    return scroll;
}

QGroupBox* SystemInfoDialog::createMonitorGroup(const DisplayInfo& d, int index, int count)
{
    QString title = d.name.isEmpty() ? QString("Monitor %1").arg(index + 1) : d.name;
    if (d.primary && count > 1)
        title += "  (Primary)";

    QGroupBox* group = new QGroupBox(title);
    QVBoxLayout* layout = new QVBoxLayout(group);

    if (!d.manufacturer.isEmpty())
        addInfoRow(layout, "Manufacturer:", d.manufacturer);
    if (!d.model.isEmpty())
        addInfoRow(layout, "Model:", d.model);

    if (d.width > 0 && d.height > 0)
        addInfoRow(layout, "Resolution:", QString("%1 × %2").arg(d.width).arg(d.height));
    if (d.refreshRate > 0.0)
        addInfoRow(layout, "Refresh Rate:", QString("%1 Hz").arg(d.refreshRate, 0, 'f', 2));

    // Color depth: prefer per-channel bit depth (from the DE probe) when known,
    // otherwise the framebuffer bpp from QScreen.
    if (d.bitsPerColor > 0)
        addInfoRow(layout, "Color Depth:", QString("%1 bit per channel").arg(d.bitsPerColor));
    else if (d.depthBpp > 0)
        addInfoRow(layout, "Color Depth:", QString("%1-bit").arg(d.depthBpp));

    if (d.scaleFactor > 0.0)
        addInfoRow(layout, "Scale:", QString("%1×").arg(d.scaleFactor, 0, 'f', 2));

    if (d.physWidthMM > 0.0 && d.physHeightMM > 0.0) {
        QString size = QString("%1 × %2 mm").arg(qRound(d.physWidthMM)).arg(qRound(d.physHeightMM));
        if (d.diagonalInch > 0.0)
            size += QString(" (≈ %1\")").arg(d.diagonalInch, 0, 'f', 1);
        addInfoRow(layout, "Physical Size:", size);
    }

    addInfoRow(layout, "HDR:",
               d.hdrSupported ? (d.hdrEnabled ? "Supported, enabled" : "Supported, disabled")
                              : "Not supported");

    // Variable Refresh Rate (G-Sync / FreeSync).
    QString vrr;
    switch (d.vrr) {
        case DisplayInfo::Vrr::Supported:   vrr = "Supported";     break;
        case DisplayInfo::Vrr::Unsupported: vrr = "Not supported"; break;
        default:                            vrr = "Unknown";       break;
    }
    if (!d.vrrRaw.isEmpty() && d.vrr != DisplayInfo::Vrr::Unknown)
        vrr += QString(" (%1)").arg(d.vrrRaw);
    addInfoRow(layout, "G-Sync / FreeSync:", vrr);

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
    // Skip if a refresh is already running — avoids piling up concurrent probes
    if (m_refreshInProgress)
        return;
    m_refreshInProgress = true;

    // Capture a copy of m_cpuInfo for the background thread (no shared mutable state)
    const CPUInfo cpuBase = m_cpuInfo;

    // Only re-probe GPUs if at least one exposes live telemetry. A suspended
    // Optimus dGPU has none, so there is nothing live to read — keep the existing
    // static snapshot instead.
    bool anyGpuTelemetry = false;
    for (const GPUInfo& g : m_gpus) {
        if (g.telemetryAvailable) { anyGpuTelemetry = true; break; }
    }
    const QList<GPUInfo> gpuSnapshot = m_gpus;

    using Result = QPair<CPUInfo, QList<GPUInfo>>;
    auto* watcher = new QFutureWatcher<Result>(this);

    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        m_refreshInProgress = false;
        const Result result = watcher->result();
        applyRefreshResult(result.first, result.second);
    });

    watcher->setFuture(QtConcurrent::run([cpuBase, anyGpuTelemetry, gpuSnapshot]() -> Result {
        // Live GPU refresh queries the driver directly (NVML for NVIDIA) instead of
        // re-running the full detector — no nvidia-smi/lspci subprocess per tick.
        QList<GPUInfo> gpus = gpuSnapshot;
        if (anyGpuTelemetry) {
            for (GPUInfo& g : gpus)
                GPUDetector::enrichTelemetry(g);
        }
        return { CPUDetector::detectDynamic(cpuBase), gpus };
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
    if (m_cpuDynamic.governor && !freshCpu.governor.isEmpty())
        m_cpuDynamic.governor->setText(freshCpu.governor);
    if (m_cpuDynamic.turbo && freshCpu.turboEnabled >= 0)
        m_cpuDynamic.turbo->setText(freshCpu.turboEnabled ? "Enabled" : "Disabled");
    if (m_cpuDynamic.utilization) {
        m_cpuDynamic.utilization->setText(
            freshCpu.cpuUtilization >= 0
                ? QString("%1 %").arg(freshCpu.cpuUtilization, 0, 'f', 0)
                : QString("—"));
    }
    if (m_cpuDynamic.loadAvg)
        m_cpuDynamic.loadAvg->setText(QString("%1").arg(freshCpu.loadAvg1, 0, 'f', 2));
    for (int i = 0; i < m_cpuDynamic.perCoreFreq.size() && i < freshCpu.perCoreFreqMHz.size(); ++i) {
        if (m_cpuDynamic.perCoreFreq[i])
            m_cpuDynamic.perCoreFreq[i]->setText(
                QString("%1 MHz").arg(freshCpu.perCoreFreqMHz[i], 0, 'f', 0));
    }
    for (int i = 0; i < m_cpuDynamic.perCoreTemp.size() && i < freshCpu.tempSensors.size(); ++i) {
        if (m_cpuDynamic.perCoreTemp[i])
            m_cpuDynamic.perCoreTemp[i]->setText(QString("%1 °C").arg(freshCpu.tempSensors[i].second));
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
        if (labels.powerSource && !fresh.powerSource.isEmpty())
            labels.powerSource->setText(fresh.powerSource);
        if (labels.temperature && fresh.temperature > 0)
            labels.temperature->setText(QString("%1 °C").arg(fresh.temperature));
        if (labels.fanSpeed && fresh.fanSpeed > 0)
            labels.fanSpeed->setText(QString("%1 %").arg(fresh.fanSpeed));
        if (labels.performanceState && !fresh.performanceState.isEmpty())
            labels.performanceState->setText(fresh.performanceState);
        if (labels.throttle && fresh.throttleReasons >= 0)
            labels.throttle->setText(throttleReasonsToString(fresh.throttleReasons));

        if (labels.vramUsed)
            labels.vramUsed->setText(QString("%1 MB").arg(fresh.memoryUsedMB));
        if (labels.vramFree)
            labels.vramFree->setText(QString("%1 MB").arg(fresh.memoryFreeMB));

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

    // ── CPU ──
    {
        const CPUInfo& c = m_cpuInfo;
        text += "=== CPU ===\n";
        if (!c.modelName.isEmpty())    text += QString("Name: %1\n").arg(c.modelName);
        if (!c.vendor.isEmpty())       text += QString("Vendor: %1\n").arg(c.vendor);
        if (!c.architecture.isEmpty()) text += QString("Architecture: %1\n").arg(c.architecture);
        if (c.pCores > 0 && c.eCores > 0)
            text += QString("Cores: %1 P + %2 E (%3 cores, %4 threads)\n")
                        .arg(c.pCores).arg(c.eCores).arg(c.pCores + c.eCores).arg(c.logicalCores);
        else {
            if (c.physicalCores > 0) text += QString("Physical Cores: %1\n").arg(c.physicalCores);
            if (c.logicalCores > 0)  text += QString("Logical CPUs: %1\n").arg(c.logicalCores);
        }
        if (c.threadsPerCore > 0) text += QString("Threads per Core: %1\n").arg(c.threadsPerCore);
        if (c.sockets > 0)        text += QString("Sockets: %1\n").arg(c.sockets);
        if (c.numaNodes > 0)      text += QString("NUMA Nodes: %1\n").arg(c.numaNodes);
        if (c.cpuFamily > 0)
            text += QString("Family / Model / Stepping: %1 / %2 / %3\n")
                        .arg(c.cpuFamily).arg(c.cpuModel).arg(c.stepping);
        if (!c.microcode.isEmpty())      text += QString("Microcode: %1\n").arg(c.microcode);
        if (!c.virtualization.isEmpty()) text += QString("Virtualization: %1\n").arg(c.virtualization);
        if (!c.simd.isEmpty())           text += QString("SIMD: %1\n").arg(c.simd.join(", "));
        if (!c.crypto.isEmpty())         text += QString("Crypto: %1\n").arg(c.crypto.join(", "));
        if (!c.otherIsa.isEmpty())       text += QString("Other ISA: %1\n").arg(c.otherIsa.join(", "));
        if (c.baseFreqMHz > 0) text += QString("Min Frequency: %1 MHz\n").arg(c.baseFreqMHz, 0, 'f', 0);
        if (c.maxFreqMHz > 0)  text += QString("Max Frequency: %1 MHz\n").arg(c.maxFreqMHz, 0, 'f', 0);
        if (c.currentFreqMHz > 0) text += QString("Current Frequency: %1 MHz\n").arg(c.currentFreqMHz, 0, 'f', 0);
        if (c.temperature > 0)    text += QString("Temperature: %1 °C\n").arg(c.temperature);
        if (!c.governor.isEmpty())      text += QString("Governor: %1\n").arg(c.governor);
        if (!c.scalingDriver.isEmpty()) text += QString("Scaling Driver: %1\n").arg(c.scalingDriver);
        if (c.turboEnabled >= 0)  text += QString("Turbo / Boost: %1\n").arg(c.turboEnabled ? "Enabled" : "Disabled");
        if (!c.epp.isEmpty())     text += QString("Energy Pref (EPP): %1\n").arg(c.epp);
        if (c.powerLimitLongW > 0)  text += QString("Power Limit (PL1): %1 W\n").arg(c.powerLimitLongW, 0, 'f', 0);
        if (c.powerLimitShortW > 0) text += QString("Max Power (PL2): %1 W\n").arg(c.powerLimitShortW, 0, 'f', 0);
        if (c.cpuUtilization >= 0)  text += QString("CPU Usage: %1 %%\n").arg(c.cpuUtilization, 0, 'f', 0);
        text += QString("Load Average (1m): %1\n").arg(c.loadAvg1, 0, 'f', 2);
        for (const QPair<QString, int>& s : c.tempSensors)
            text += QString("  %1: %2 °C\n").arg(s.first).arg(s.second);
        text += "\n";
    }

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
        if (gpu.memoryTotalMB > 0 && gpu.telemetryAvailable) {
            text += QString("VRAM Used: %1 MB\n").arg(gpu.memoryUsedMB);
            text += QString("VRAM Free: %1 MB\n").arg(gpu.memoryFreeMB);
        }
        if (gpu.memoryBusWidth > 0)
            text += QString("Memory Bus Width: %1-bit\n").arg(gpu.memoryBusWidth);

        const DriverInfo& driver = gpu.driverInfo;
        const QString driverVer = !driver.version.isEmpty() ? driver.version : gpu.driverVersion;
        if (!driverVer.isEmpty())
            text += QString("Driver Version: %1\n").arg(driverVer);
        if (!driver.branch.isEmpty())
            text += QString("Driver Branch: %1\n").arg(driver.branch);
        if (!driver.releaseDate.isEmpty())
            text += QString("Release Date: %1\n").arg(driver.releaseDate);
        if (!driver.moduleType.isEmpty())
            text += QString("Kernel Module: %1\n").arg(driver.moduleType);
        if (!driver.moduleName.isEmpty())
            text += QString("Module Name: %1\n").arg(driver.moduleName);
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

        if (!gpu.telemetryAvailable) {
            text += QString("Status: Power-suspended (D3cold) — live telemetry unavailable\n");
        } else {
            if (gpu.currentGraphicsClock > 0)
                text += QString("GPU Clock: %1 MHz\n").arg(gpu.currentGraphicsClock);
            if (gpu.currentMemoryClock > 0)
                text += QString("Memory Clock: %1 MHz\n").arg(gpu.currentMemoryClock);
            if (gpu.currentPowerDraw > 0)
                text += QString("Power Draw: %1 W\n").arg(gpu.currentPowerDraw);
            if (gpu.powerLimit > 0)
                text += QString("Power Limit: %1 W\n").arg(gpu.powerLimit);
            if (gpu.powerDefaultLimit > 0)
                text += QString("Default Power Limit: %1 W\n").arg(gpu.powerDefaultLimit);
            if (gpu.powerLimitMin > 0 && gpu.powerLimitMax > 0)
                text += QString("Power Limit Range: %1 – %2 W\n")
                    .arg(gpu.powerLimitMin).arg(gpu.powerLimitMax);
            if (!gpu.powerSource.isEmpty())
                text += QString("Power Source: %1\n").arg(gpu.powerSource);
            if (gpu.temperature > 0)
                text += QString("Temperature: %1 °C\n").arg(gpu.temperature);
            if (gpu.tempSlowdown > 0)
                text += QString("Slowdown Temp: %1 °C\n").arg(gpu.tempSlowdown);
            if (gpu.tempShutdown > 0)
                text += QString("Shutdown Temp: %1 °C\n").arg(gpu.tempShutdown);
            if (gpu.fanSpeed > 0)
                text += QString("Fan Speed: %1 %%\n").arg(gpu.fanSpeed);
            if (!gpu.performanceState.isEmpty())
                text += QString("Performance State: %1\n").arg(gpu.performanceState);
            if (gpu.throttleReasons >= 0)
                text += QString("Throttle Reason: %1\n").arg(throttleReasonsToString(gpu.throttleReasons));

            // Utilization
            text += QString("\nUtilization:\n");
            text += QString("  GPU: %1 %%\n").arg(gpu.gpuUtilization);
            text += QString("  Memory: %1 %%\n").arg(gpu.memoryUtilization);
            text += QString("  Encoder: %1 %%\n").arg(gpu.encoderUtilization);
            text += QString("  Decoder: %1 %%\n").arg(gpu.decoderUtilization);
            text += QString("  JPEG: %1 %%\n").arg(gpu.jpegUtilization);
            text += QString("  OFA: %1 %%\n").arg(gpu.ofaUtilization);
        }

        if (i < m_gpus.size() - 1) {
            text += "\n";
        }
    }

    // ── Monitors ──
    for (int i = 0; i < m_displays.size(); ++i) {
        const DisplayInfo& d = m_displays[i];
        text += "\n";
        const QString header = d.name.isEmpty() ? QString("Monitor %1").arg(i + 1) : d.name;
        text += QString("=== %1%2 ===\n").arg(header,
                    (d.primary && m_displays.size() > 1) ? " (Primary)" : "");

        if (!d.manufacturer.isEmpty())
            text += QString("Manufacturer: %1\n").arg(d.manufacturer);
        if (!d.model.isEmpty())
            text += QString("Model: %1\n").arg(d.model);
        if (d.width > 0 && d.height > 0)
            text += QString("Resolution: %1 × %2\n").arg(d.width).arg(d.height);
        if (d.refreshRate > 0.0)
            text += QString("Refresh Rate: %1 Hz\n").arg(d.refreshRate, 0, 'f', 2);
        if (d.bitsPerColor > 0)
            text += QString("Color Depth: %1 bit per channel\n").arg(d.bitsPerColor);
        else if (d.depthBpp > 0)
            text += QString("Color Depth: %1-bit\n").arg(d.depthBpp);
        if (d.scaleFactor > 0.0)
            text += QString("Scale: %1×\n").arg(d.scaleFactor, 0, 'f', 2);
        if (d.physWidthMM > 0.0 && d.physHeightMM > 0.0) {
            text += QString("Physical Size: %1 × %2 mm").arg(qRound(d.physWidthMM)).arg(qRound(d.physHeightMM));
            if (d.diagonalInch > 0.0)
                text += QString(" (≈ %1\")").arg(d.diagonalInch, 0, 'f', 1);
            text += "\n";
        }
        text += QString("HDR: %1\n").arg(
            d.hdrSupported ? (d.hdrEnabled ? "Supported, enabled" : "Supported, disabled")
                           : "Not supported");
        QString vrr = d.vrr == DisplayInfo::Vrr::Supported ? "Supported"
                    : d.vrr == DisplayInfo::Vrr::Unsupported ? "Not supported" : "Unknown";
        if (!d.vrrRaw.isEmpty() && d.vrr != DisplayInfo::Vrr::Unknown)
            vrr += QString(" (%1)").arg(d.vrrRaw);
        text += QString("G-Sync / FreeSync: %1\n").arg(vrr);
    }

    QApplication::clipboard()->setText(text);
}
