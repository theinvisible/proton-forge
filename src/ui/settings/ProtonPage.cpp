#include "ui/settings/ProtonPage.h"

#include <QHBoxLayout>
#include <QLabel>

#include <algorithm>

ProtonPage::ProtonPage(QWidget* parent)
    : SettingsPage(parent)
{
    // Every switch in here is read by the Proton script alone.
    QGroupBox* group = new QGroupBox("Proton Tweaks", this);
    QVBoxLayout* layout = new QVBoxLayout(group);
    gate(group, "PROTON_NO_NTSYNC");

    QHBoxLayout* ntsyncRow = new QHBoxLayout();
    ntsyncRow->addWidget(new QLabel("NTSync:", this));
    m_ntsyncMode = new QComboBox(this);
    m_ntsyncMode->addItem("Proton default", QStringLiteral("default"));
    m_ntsyncMode->addItem("Force on (PROTON_USE_NTSYNC, Proton ≤ 10)", QStringLiteral("on"));
    m_ntsyncMode->addItem("Off (PROTON_NO_NTSYNC, Proton 11+)", QStringLiteral("off"));
    m_ntsyncMode->setToolTip(
        "NTSync: kernel-level Windows synchronization primitives.\n\n"
        "It replaces wineserver-based synchronization and significantly reduces "
        "CPU overhead in multi-threaded games.\n\n"
        "• Up to Proton 10 it is opt-in: 'Force on' sets PROTON_USE_NTSYNC=1.\n"
        "• From Proton 11 it is on by default: 'Off' sets PROTON_NO_NTSYNC=1 "
        "for a game that misbehaves with it.\n\n"
        "Requires: Linux kernel 6.14+ with NTSync support enabled.");
    ntsyncRow->addWidget(m_ntsyncMode, 1);
    layout->addLayout(ntsyncRow);

    bindFlag(layout, &DLSSSettings::protonUseD7VK,
             "Use D7VK (PROTON_USE_D7VK)",
             "Translate Direct3D 7 to Vulkan via d7vk.\n\n"
             "Improves compatibility and performance for very old D3D7-era games.\n\n"
             "Requires: GE-Proton 11-1+ or Proton-CachyOS 11.0+.\n\n"
             "Sets PROTON_USE_D7VK=1 (GE-Proton) and PROTON_D7VK_DDRAW=1 (Proton-CachyOS).",
             "PROTON_USE_D7VK", FeatureGate::Feature::D7vk, QStringLiteral("D7VK"));

    bindFlag(layout, &DLSSSettings::protonLog,
             "Enable Logging (PROTON_LOG)",
             "Enable Proton debug logging.\n\n"
             "Writes detailed log output to help diagnose issues with games running under Proton. "
             "Log files are typically written to the game's prefix directory.\n\n"
             "Note: May impact performance. Only enable for troubleshooting.",
             "PROTON_LOG");

    connect(m_ntsyncMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::changed);
    pageLayout()->addWidget(group);
}

void ProtonPage::load(const DLSSSettings& settings)
{
    SettingsPage::load(settings);
    const QString ntsync = settings.protonDisableNTSync ? QStringLiteral("off")
                         : settings.protonUseNTSync     ? QStringLiteral("on")
                                                        : QStringLiteral("default");
    m_ntsyncMode->setCurrentIndex(std::max(0, m_ntsyncMode->findData(ntsync)));
}

void ProtonPage::store(DLSSSettings& settings) const
{
    SettingsPage::store(settings);
    const QString ntsync = m_ntsyncMode->currentData().toString();
    settings.protonUseNTSync = (ntsync == QLatin1String("on"));
    settings.protonDisableNTSync = (ntsync == QLatin1String("off"));
}

void ProtonPage::collectWarnings(const FeatureGate::Context& ctx, QStringList& lines) const
{
    SettingsPage::collectWarnings(ctx, lines);
    const QString ntsync = m_ntsyncMode->currentData().toString();
    check(m_ntsyncMode, ntsync == QLatin1String("on"), FeatureGate::Feature::NtsyncOptIn,
          "NTSync", ctx, lines);
    check(m_ntsyncMode, ntsync == QLatin1String("off"), FeatureGate::Feature::NtsyncDisable,
          "NTSync off", ctx, lines);
}
