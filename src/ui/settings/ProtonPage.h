#ifndef PROTONPAGE_H
#define PROTONPAGE_H

#include <QComboBox>
#include <QGroupBox>

#include "ui/settings/SettingsPage.h"

// Proton tab: NTSync, d7vk, logging.
class ProtonPage : public SettingsPage {
    Q_OBJECT

public:
    explicit ProtonPage(QWidget* parent = nullptr);

    void load(const DLSSSettings& settings) override;
    void store(DLSSSettings& settings) const override;

private:
    // NTSync: Proton default / force on (opt-in up to Proton 10) / off (11+).
    // One combo for the two DLSSSettings booleans, so they cannot both be set.
    QComboBox* m_ntsyncMode;
    // Controller input: Proton default / without Steam Input / SDL. One combo
    // for the two DLSSSettings booleans, as with NTSync.
    QComboBox* m_controllerInput;
};

#endif // PROTONPAGE_H
