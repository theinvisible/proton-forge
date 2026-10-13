#ifndef GENERALPAGE_H
#define GENERALPAGE_H

#include "ui/settings/SettingsPage.h"

// General tab: NVAPI, PRIME offload, latency, the vkd3d/DXVK-NVAPI switches.
class GeneralPage : public SettingsPage {
    Q_OBJECT

public:
    explicit GeneralPage(QWidget* parent = nullptr);

    void collectWarnings(const FeatureGate::Context& ctx, QStringList& lines) const override;

private:
    QCheckBox* m_enablePrimeOffload;
};

#endif // GENERALPAGE_H
