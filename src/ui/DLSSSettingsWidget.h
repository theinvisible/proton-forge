#ifndef DLSSSETTINGSWIDGET_H
#define DLSSSETTINGSWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include "core/Game.h"
#include "core/DLSSSettings.h"

class DLSSSettingsWidget : public QWidget {
    Q_OBJECT

public:
    explicit DLSSSettingsWidget(QWidget* parent = nullptr);

    void setGame(const Game& game);
    void setSettings(const DLSSSettings& settings);
    DLSSSettings settings() const;

    void updateLaunchCommand(const QString& command);

signals:
    void settingsChanged(const DLSSSettings& settings);
    void playClicked();
    void copyClicked();
    void writeToSteamClicked();

private slots:
    void onSettingChanged();

private:
    void setupUI();
    QGroupBox* createGeneralGroup();
    QGroupBox* createSuperResolutionGroup();
    QGroupBox* createRayReconstructionGroup();
    QGroupBox* createFrameGenerationGroup();
    QGroupBox* createUpgradeGroup();
    QGroupBox* createSmoothMotionGroup();
    QWidget* createActionsSection();

    void blockSignalsForAll(bool block);

    // Header
    QLabel* m_gameNameLabel;
    QLabel* m_gameImageLabel;
    QLabel* m_protonVersionLabel;

    // General settings
    QCheckBox* m_enableNVAPI;
    QCheckBox* m_enableNGXUpdater;
    QCheckBox* m_showIndicator;

    // Super Resolution
    QCheckBox* m_srOverride;
    QComboBox* m_srMode;
    QComboBox* m_srPreset;
    QSpinBox* m_srScalingRatio;

    // Ray Reconstruction
    QCheckBox* m_rrOverride;
    QComboBox* m_rrMode;
    QComboBox* m_rrPreset;
    QSpinBox* m_rrScalingRatio;

    // Frame Generation
    QCheckBox* m_fgOverride;
    QComboBox* m_fgMultiFrameCount;

    // DLSS Upgrade
    QCheckBox* m_dlssUpgrade;

    // Smooth Motion
    QCheckBox* m_enableFrameRateLimit;
    QSpinBox* m_targetFrameRate;

    // Launch command preview
    QTextEdit* m_launchCommandPreview;

    // Action buttons
    QPushButton* m_playButton;
    QPushButton* m_copyButton;
    QPushButton* m_writeToSteamButton;

    Game m_currentGame;
};

#endif // DLSSSETTINGSWIDGET_H
