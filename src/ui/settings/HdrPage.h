#ifndef HDRPAGE_H
#define HDRPAGE_H

#include "ui/settings/SettingsPage.h"

// HDR tab: the three switches HDR needs, their quick toggle, and the
// Proton-CachyOS auto-HDR opt-out.
class HdrPage : public SettingsPage {
    Q_OBJECT

public:
    explicit HdrPage(QWidget* parent = nullptr);

    void load(const DLSSSettings& settings) override;
    void store(DLSSSettings& settings) const override;

private slots:
    void onEnableAllToggled(bool checked);
    void onOptionToggled();

private:
    // Asks before HDR is switched on while the desktop has it off.
    bool confirmHdrStatus();

    QCheckBox* m_enableAllHDR;   // quick toggle for the three below
    QCheckBox* m_enableProtonWayland;
    QCheckBox* m_enableProtonHDR;
    QCheckBox* m_enableHDRWSI;
    bool m_loading = false;       // load() is writing the checkboxes
};

#endif // HDRPAGE_H
