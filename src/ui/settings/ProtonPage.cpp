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

    QHBoxLayout* controllerRow = new QHBoxLayout();
    controllerRow->addWidget(new QLabel("Controller input:", this));
    m_controllerInput = new QComboBox(this);
    m_controllerInput->addItem("Proton default", QString());
    m_controllerInput->addItem("Without Steam Input (PROTON_NO_STEAMINPUT)",
                               QStringLiteral("nosteaminput"));
    m_controllerInput->addItem("SDL for all controllers (PROTON_PREFER_SDL)",
                               QStringLiteral("sdl"));
    m_controllerInput->setToolTip(
        "How the game sees your controller.\n\n"
        "• Proton default: Steam Input's virtual controller and direct HID access, "
        "as Proton decides.\n"
        "• Without Steam Input: hides Steam's virtual controller, so the game only "
        "sees the real one — for doubled inputs or wrong button prompts. Direct HID "
        "access (DualSense haptics, adaptive triggers) stays. Sets "
        "PROTON_NO_STEAMINPUT=1.\n"
        "• SDL for all controllers: routes every controller through SDL instead of "
        "HID and Steam Input — for controllers that are not detected or misbehave. "
        "Sets PROTON_PREFER_SDL=1.\n\n"
        "Requires: Proton-CachyOS, or GE-Proton (SDL from 10-7; without Steam Input "
        "from 10-8, and in GE-Proton only together with Wayland). Valve's Proton "
        "has neither.");
    controllerRow->addWidget(m_controllerInput, 1);
    layout->addLayout(controllerRow);

    bindFlag(layout, &DLSSSettings::protonUseD7VK,
             "Use D7VK (PROTON_USE_D7VK)",
             "Translate Direct3D 7 to Vulkan via d7vk.\n\n"
             "Improves compatibility and performance for very old D3D7-era games.\n\n"
             "Requires: GE-Proton 11-1+ or Proton-CachyOS 11.0+.\n\n"
             "Sets PROTON_USE_D7VK=1 (GE-Proton) and PROTON_D7VK_DDRAW=1 (Proton-CachyOS).",
             "PROTON_USE_D7VK");

    bindFlag(layout, &DLSSSettings::protonLog,
             "Enable Logging (PROTON_LOG)",
             "Enable Proton debug logging.\n\n"
             "Writes detailed log output to help diagnose issues with games running under Proton. "
             "Log files are typically written to the game's prefix directory.\n\n"
             "Note: May impact performance. Only enable for troubleshooting.",
             "PROTON_LOG");

    connect(m_ntsyncMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::changed);
    connect(m_controllerInput, QOverload<int>::of(&QComboBox::currentIndexChanged),
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

    // SDL implies no Steam Input, so it wins if a string ever has both.
    const QString controller = settings.protonPreferSdl    ? QStringLiteral("sdl")
                             : settings.protonNoSteamInput ? QStringLiteral("nosteaminput")
                                                           : QString();
    m_controllerInput->setCurrentIndex(std::max(0, m_controllerInput->findData(controller)));
}

void ProtonPage::store(DLSSSettings& settings) const
{
    SettingsPage::store(settings);
    const QString ntsync = m_ntsyncMode->currentData().toString();
    settings.protonUseNTSync = (ntsync == QLatin1String("on"));
    settings.protonDisableNTSync = (ntsync == QLatin1String("off"));

    const QString controller = m_controllerInput->currentData().toString();
    settings.protonNoSteamInput = (controller == QLatin1String("nosteaminput"));
    settings.protonPreferSdl = (controller == QLatin1String("sdl"));
}
