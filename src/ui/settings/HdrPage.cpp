#include "ui/settings/HdrPage.h"

#include <QGroupBox>
#include <QMessageBox>

#include "core/FeatureGate.h"
#include "utils/GpuInfoCache.h"
#include "utils/HDRChecker.h"

HdrPage::HdrPage(QWidget* parent)
    : SettingsPage(parent)
{
    QGroupBox* group = new QGroupBox("HDR", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_enableAllHDR = new QCheckBox("Enable All HDR Options (Quick Toggle)", this);
    // An inline stylesheet beats the one in style.qss, so this has to carry its
    // own disabled colour — otherwise the quick toggle keeps its accent green
    // while the options it drives are greyed out (native Linux games).
    m_enableAllHDR->setStyleSheet(
        "QCheckBox { font-weight: bold; color: #76B900; }"
        "QCheckBox:disabled { color: #4a5f28; }");
    m_enableAllHDR->setToolTip(
        "Quick toggle to enable/disable all HDR options at once.\n\n"
        "This is a convenience checkbox that controls the HDR settings below. "
        "From NVIDIA driver 595 it leaves ENABLE_HDR_WSI off, because the driver "
        "no longer needs it. You can also toggle individual options if needed.");
    layout->addWidget(m_enableAllHDR);

    // Indent individual HDR options
    QWidget* hdrOptionsWidget = new QWidget(this);
    QVBoxLayout* hdrOptionsLayout = new QVBoxLayout(hdrOptionsWidget);
    hdrOptionsLayout->setContentsMargins(20, 0, 0, 0);
    hdrOptionsLayout->setSpacing(5);

    m_enableProtonWayland = new QCheckBox("PROTON_ENABLE_WAYLAND=1", this);
    m_enableProtonWayland->setToolTip(
        "Enable Wayland support in Proton.\n\n"
        "Required for HDR to work. Enables Wayland backend instead of XWayland.");
    hdrOptionsLayout->addWidget(m_enableProtonWayland);

    m_enableProtonHDR = new QCheckBox("PROTON_ENABLE_HDR=1 + DXVK_HDR=1", this);
    m_enableProtonHDR->setToolTip(
        "Enable HDR support in Proton.\n\n"
        "GE-Proton reads PROTON_ENABLE_HDR; Proton-CachyOS retired it in "
        "11.0-20260506 and reads DXVK_HDR instead. Both are set, so the option "
        "works whichever Proton runs the game.");
    hdrOptionsLayout->addWidget(m_enableProtonHDR);

    m_enableHDRWSI = new QCheckBox("ENABLE_HDR_WSI=1", this);
    m_enableHDRWSI->setToolTip(
        "Enable HDR Window System Integration.\n\n"
        "Enables HDR support in the Vulkan WSI (Window System Integration) layer.\n\n"
        "Only needed with NVIDIA drivers older than 595 — newer drivers expose HDR "
        "to Vulkan themselves, so the quick toggle leaves it off there.");
    hdrOptionsLayout->addWidget(m_enableHDRWSI);

    // NOT part of the quick toggle: a disable switch, kept outside the master
    // sync and the HDR status dialog.
    bindFlag(hdrOptionsLayout, &DLSSSettings::disableAutoHDR,
             "Disable auto-HDR (DXVK_NO_HDR)",
             "Opt out of Proton-CachyOS's automatic HDR.\n\n"
             "Since Proton-CachyOS 11.0-20260601, HDR is enabled automatically when "
             "the display supports it. Enable this to force SDR for games where "
             "auto-HDR misbehaves.\n\n"
             "Not affected by the 'Enable All HDR Options' quick toggle.\n\n"
             "Sets DXVK_NO_HDR=1.",
             "DXVK_NO_HDR");

    layout->addWidget(hdrOptionsWidget);
    pageLayout()->addWidget(group);

    connect(m_enableAllHDR, &QCheckBox::toggled, this, &HdrPage::onEnableAllToggled);
    connect(m_enableProtonWayland, &QCheckBox::toggled, this, &HdrPage::onOptionToggled);
    connect(m_enableProtonHDR, &QCheckBox::toggled, this, &HdrPage::onOptionToggled);
    connect(m_enableHDRWSI, &QCheckBox::toggled, this, &HdrPage::onOptionToggled);

    // HDR_WSI is a Vulkan implicit layer and works natively; the PROTON_*
    // switches do not. The quick toggle drives two of those, so it goes with them.
    gate(m_enableAllHDR, "PROTON_ENABLE_HDR");
    gate(m_enableProtonWayland, "PROTON_ENABLE_WAYLAND");
    gate(m_enableProtonHDR, "PROTON_ENABLE_HDR");

    // The driver version arrives after startup and decides whether WSI is
    // part of "all on".
    connect(&GpuInfoCache::instance(), &GpuInfoCache::updated, this, &HdrPage::syncEnableAll);
}

void HdrPage::load(const DLSSSettings& settings)
{
    m_loading = true;
    SettingsPage::load(settings);
    m_enableProtonWayland->setChecked(settings.enableProtonWayland);
    m_enableProtonHDR->setChecked(settings.enableProtonHDR);
    m_enableHDRWSI->setChecked(settings.enableHDRWSI);
    syncEnableAll();
    m_loading = false;
}

void HdrPage::store(DLSSSettings& settings) const
{
    SettingsPage::store(settings);
    settings.enableProtonWayland = m_enableProtonWayland->isChecked();
    settings.enableProtonHDR = m_enableProtonHDR->isChecked();
    settings.enableHDRWSI = m_enableHDRWSI->isChecked();
}

void HdrPage::onEnableAllToggled(bool checked)
{
    if (m_loading) {
        return;   // loading a game's settings, not the user asking for HDR
    }
    if (checked && !confirmHdrStatus()) {
        const QSignalBlocker blocker(m_enableAllHDR);
        m_enableAllHDR->setChecked(false);
        return;
    }

    {
        const QSignalBlocker b1(m_enableProtonWayland);
        const QSignalBlocker b2(m_enableProtonHDR);
        const QSignalBlocker b3(m_enableHDRWSI);
        m_enableProtonWayland->setChecked(checked);
        m_enableProtonHDR->setChecked(checked);
        m_enableHDRWSI->setChecked(checked && wsiNeeded());
    }
    emit changed();
}

void HdrPage::onOptionToggled()
{
    if (m_loading) {
        return;
    }

    // Switching one on while the desktop has HDR off asks first.
    auto* box = qobject_cast<QCheckBox*>(sender());
    if (box && box->isChecked() && !confirmHdrStatus()) {
        const QSignalBlocker blocker(box);
        box->setChecked(false);
        return;
    }

    syncEnableAll();
    emit changed();
}

bool HdrPage::wsiNeeded()
{
    FeatureGate::Context ctx;
    ctx.driver = GpuInfoCache::instance().facts().driver;
    const FeatureGate::Result r = FeatureGate::evaluate(
        FeatureGate::requirementFor(FeatureGate::Feature::HdrWsiLayer), ctx);
    return r.status != FeatureGate::Status::Unneeded;
}

void HdrPage::syncEnableAll()
{
    const QSignalBlocker blocker(m_enableAllHDR);
    m_enableAllHDR->setChecked(m_enableProtonWayland->isChecked() &&
                               m_enableProtonHDR->isChecked() &&
                               (m_enableHDRWSI->isChecked() || !wsiNeeded()));
}

bool HdrPage::confirmHdrStatus()
{
    const HDRChecker::HDRStatus status = HDRChecker::checkHDRStatus();
    if (status.isEnabled) {
        return true;
    }
    const QString warningMessage = HDRChecker::getWarningMessage(status);
    if (warningMessage.isEmpty()) {
        return true;
    }

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle("HDR Not Enabled");
    msgBox.setText(warningMessage);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    // Yes: the user wants it anyway.
    return msgBox.exec() == QMessageBox::Yes;
}
