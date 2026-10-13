#ifndef DLSSPAGE_H
#define DLSSPAGE_H

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>

#include "ui/settings/SettingsPage.h"

// DLSS tab: Super Resolution, Ray Reconstruction, Frame Generation, the DLSS
// upgrade, Smooth Motion and the frame cap.
class DlssPage : public SettingsPage {
    Q_OBJECT

public:
    explicit DlssPage(QWidget* parent = nullptr);

    void load(const DLSSSettings& settings) override;
    void store(DLSSSettings& settings) const override;
    void setGame(const Game& game) override;

private:
    QGroupBox* createSuperResolutionGroup();
    QGroupBox* createRayReconstructionGroup();
    QGroupBox* createFrameGenerationGroup();
    QGroupBox* createUpgradeGroup();
    QGroupBox* createSmoothMotionGroup();
    // Enables the FG controls from the override, and the Dynamic MFG fields
    // only in FG mode "Dynamic".
    void updateFrameGenEnabled();
    // Mode and preset follow the override; the scaling ratio additionally
    // needs mode CUSTOM, the only mode that reads it.
    void updateSuperResolutionEnabled();
    void updateRayReconstructionEnabled();
    // The "Scaling Ratio" row: a 33-100 % slider with its value beside it.
    QWidget* createScalingRow(QSlider*& slider, QLabel*& value, const QString& tooltip);

    // Group boxes whose contents are Proton-only in their entirety, so the
    // whole box (title included) greys out on a native game.
    QGroupBox* m_srGroup;
    QGroupBox* m_rrGroup;
    QGroupBox* m_fgGroup;
    QGroupBox* m_upgradeGroup;

    // Super Resolution
    QCheckBox* m_srOverride;
    QComboBox* m_srMode;
    QComboBox* m_srPreset;
    QSlider* m_srScalingRatio;
    QLabel* m_srScalingValue;

    // Ray Reconstruction
    QCheckBox* m_rrOverride;
    QComboBox* m_rrMode;
    QComboBox* m_rrPreset;
    QSlider* m_rrScalingRatio;
    QLabel* m_rrScalingValue;

    // Frame Generation
    QCheckBox* m_fgOverride;
    QComboBox* m_fgMultiFrameCount;
    QComboBox* m_fgMode;
    QComboBox* m_fgPreset;
    QSpinBox* m_fgDynamicTargetFps;
    QLabel* m_fgDynamicTargetFpsLabel;
    QComboBox* m_fgDynamicMaxCount;
    QLabel* m_fgDynamicMaxCountLabel;

    // DLSS Upgrade
    QCheckBox* m_dlssUpgrade;
    QLineEdit* m_dlssVersion;

    // Smooth Motion / frame cap
    QCheckBox* m_enableSmoothMotion;
    QCheckBox* m_smoothMotionGraphicsQueue;
    QCheckBox* m_enableFrameRateLimit;
    QSpinBox* m_targetFrameRate;
    // Its row label: a plain layout does not pass the enabled state on, so the
    // label has to be switched together with the spin box or it stays lit next
    // to a greyed-out field.
    QLabel* m_targetFrameRateLabel;
};

#endif // DLSSPAGE_H
