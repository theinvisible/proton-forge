#ifndef MANGOHUDDIALOG_H
#define MANGOHUDDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMap>
#include <QStringList>

class MangoHudDialog : public QDialog {
    Q_OBJECT
public:
    explicit MangoHudDialog(QWidget* parent = nullptr);

    static bool isMangoHudInstalled();

private slots:
    void saveConfig();

private:
    void setupUI();
    void loadConfig();

    QString configFilePath() const;
    void parseConfigFile(const QString& path);
    void writeConfigFile(const QString& path);

    QGroupBox* createPerformanceGroup();
    QGroupBox* createCpuGroup();
    QGroupBox* createGpuGroup();
    QGroupBox* createMetricsGroup();
    QGroupBox* createSystemGroup();
    QGroupBox* createAppearanceGroup();
    QGroupBox* createLoggingGroup();

    // Performance
    QCheckBox*      m_fpsLimitEnabled;
    QLineEdit*      m_fpsLimit;
    QCheckBox*      m_vsyncEnabled;
    QComboBox*      m_vsync;

    // CPU
    QCheckBox*      m_cpuStats;
    QCheckBox*      m_cpuTemp;
    QCheckBox*      m_cpuPower;
    QCheckBox*      m_cpuMhz;
    QLineEdit*      m_cpuText;

    // GPU
    QCheckBox*      m_gpuStats;
    QCheckBox*      m_gpuTemp;
    QCheckBox*      m_gpuCoreClock;
    QCheckBox*      m_gpuMemClock;
    QCheckBox*      m_gpuPower;
    QCheckBox*      m_gpuName;
    QCheckBox*      m_vulkanDriver;
    QLineEdit*      m_gpuText;

    // Metrics
    QCheckBox*      m_fps;
    QCheckBox*      m_frametime;
    QCheckBox*      m_frameTiming;
    QCheckBox*      m_histogram;
    QCheckBox*      m_showFpsLimit;

    // System
    QCheckBox*      m_ram;
    QCheckBox*      m_swap;
    QCheckBox*      m_vram;
    QCheckBox*      m_resolution;
    QCheckBox*      m_time;
    QCheckBox*      m_arch;
    QCheckBox*      m_version;
    QCheckBox*      m_engineVersion;
    QCheckBox*      m_wine;

    // Appearance
    QComboBox*      m_position;
    QSpinBox*       m_fontSize;
    QDoubleSpinBox* m_backgroundAlpha;
    QLineEdit*      m_customTextCenter;

    // Logging
    QCheckBox*      m_autostartLogEnabled;
    QSpinBox*       m_autostartLog;
    QCheckBox*      m_logDurationEnabled;
    QSpinBox*       m_logDuration;
    QCheckBox*      m_outputFolderEnabled;
    QLineEdit*      m_outputFolder;

    // Raw file lines for preserving structure on save
    QStringList     m_originalLines;
    // Parsed key→value map (value empty for toggle-only keys)
    QMap<QString, QString> m_parsedValues;
    // Set of keys that are active (not commented out)
    QSet<QString>   m_activeKeys;
};

#endif // MANGOHUDDIALOG_H
