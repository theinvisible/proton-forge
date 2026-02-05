#ifndef GPUINFODIALOG_H
#define GPUINFODIALOG_H

#include <QDialog>
#include <QLabel>
#include <QTimer>
#include "../utils/GPUDetector.h"

class QTabWidget;
class QVBoxLayout;
class QGroupBox;
class QCheckBox;

// Structure to hold dynamic value labels for easy updating
struct DynamicLabels {
    QLabel* gpuClock = nullptr;
    QLabel* memoryClock = nullptr;
    QLabel* powerDraw = nullptr;
    QLabel* temperature = nullptr;
    QLabel* fanSpeed = nullptr;
    QLabel* performanceState = nullptr;

    // Utilization labels
    QLabel* gpuUtilization = nullptr;
    QLabel* memoryUtilization = nullptr;
    QLabel* encoderUtilization = nullptr;
    QLabel* decoderUtilization = nullptr;
    QLabel* jpegUtilization = nullptr;
    QLabel* ofaUtilization = nullptr;
};

class GPUInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GPUInfoDialog(const QList<GPUInfo>& gpus, QWidget* parent = nullptr);
    ~GPUInfoDialog();

private:
    void setupUI();
    QWidget* createGPUTab(const GPUInfo& gpu, int gpuIndex);
    QGroupBox* createGraphicsCardGroup(const GPUInfo& gpu);
    QGroupBox* createMemoryGroup(const GPUInfo& gpu);
    QGroupBox* createDriverBiosGroup(const GPUInfo& gpu);
    QGroupBox* createPCIeGroup(const GPUInfo& gpu);
    QGroupBox* createClocksPowerGroup(const GPUInfo& gpu, int gpuIndex);
    QGroupBox* createUtilizationGroup(const GPUInfo& gpu, int gpuIndex);
    QLabel* addInfoRow(QVBoxLayout* layout, const QString& label, const QString& value);
    QString vendorToString(GPUInfo::Vendor vendor);

private slots:
    void copyToClipboard();
    void refreshDynamicValues();
    void toggleAutoRefresh(bool enabled);

private:
    QList<GPUInfo> m_gpus;
    QTabWidget* m_tabWidget;
    QTimer* m_refreshTimer;
    QCheckBox* m_autoRefreshCheckbox;
    QList<DynamicLabels> m_dynamicLabels; // One per GPU
};

#endif // GPUINFODIALOG_H
