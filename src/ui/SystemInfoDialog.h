#ifndef SYSTEMINFODIALOG_H
#define SYSTEMINFODIALOG_H

#include <QDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QTimer>
#include "../utils/GPUDetector.h"
#include "../utils/CPUDetector.h"

class QTabWidget;
class QVBoxLayout;
class QGroupBox;
class QCheckBox;

struct DynamicLabels {
    QLabel* gpuClock = nullptr;
    QLabel* memoryClock = nullptr;
    QLabel* powerDraw = nullptr;
    QLabel* temperature = nullptr;
    QLabel* fanSpeed = nullptr;
    QLabel* performanceState = nullptr;

    QLabel* gpuUtilization = nullptr;
    QLabel* memoryUtilization = nullptr;
    QLabel* encoderUtilization = nullptr;
    QLabel* decoderUtilization = nullptr;
    QLabel* jpegUtilization = nullptr;
    QLabel* ofaUtilization = nullptr;
};

struct CPUDynamicLabels {
    QLabel* currentFreq = nullptr;
    QLabel* temperature = nullptr;
};

class SystemInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SystemInfoDialog(const QList<GPUInfo>& gpus, QWidget* parent = nullptr);
    ~SystemInfoDialog();

private:
    void setupUI();

    // CPU tab
    QWidget*    createCPUTab();
    QGroupBox*  createCPUProcessorGroup();
    QGroupBox*  createCPUFreqGroup();
    QGroupBox*  createCPUCacheGroup();

    // GPU tabs
    QWidget*    createGPUTab(const GPUInfo& gpu, int gpuIndex);
    QGroupBox*  createGraphicsCardGroup(const GPUInfo& gpu);
    QGroupBox*  createMemoryGroup(const GPUInfo& gpu);
    QGroupBox*  createDriverBiosGroup(const GPUInfo& gpu);
    QGroupBox*  createPCIeGroup(const GPUInfo& gpu);
    QGroupBox*  createClocksPowerGroup(const GPUInfo& gpu, int gpuIndex);
    QGroupBox*  createUtilizationGroup(const GPUInfo& gpu, int gpuIndex);

    QLabel*     addInfoRow(QVBoxLayout* layout, const QString& label, const QString& value);
    QString     vendorToString(GPUInfo::Vendor vendor);
    static QString formatCacheSize(int kib);

    // Called on the main thread once the background refresh completes
    void applyRefreshResult(const CPUInfo& freshCpu, const QList<GPUInfo>& freshGpus);

private slots:
    void copyToClipboard();
    void refreshDynamicValues();
    void toggleAutoRefresh(bool enabled);

private:
    CPUInfo              m_cpuInfo;
    CPUDynamicLabels     m_cpuDynamic;

    QList<GPUInfo>       m_gpus;
    QTabWidget*          m_tabWidget;
    QTimer*              m_refreshTimer;
    QCheckBox*           m_autoRefreshCheckbox;
    QList<DynamicLabels> m_dynamicLabels;
    bool                 m_refreshInProgress = false;
};

#endif // SYSTEMINFODIALOG_H
