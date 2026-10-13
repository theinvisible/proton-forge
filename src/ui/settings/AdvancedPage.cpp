#include "ui/settings/AdvancedPage.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>

#include "ui/AppStyle.h"
#include "ui/MangoHudDialog.h"

AdvancedPage::AdvancedPage(QWidget* parent)
    : SettingsPage(parent)
{
    QGroupBox* group = new QGroupBox("Overlay", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    // Not an environment variable: GameRunner preloads it, for Steam games only.
    m_enableSteamOverlay =
        bindFlag(layout, &DLSSSettings::enableSteamOverlay,
                 "Steam Overlay (gameoverlayrenderer.so)",
                 "Enable the Steam Performance Overlay.\n\n"
                 "When enabled, gameoverlayrenderer.so is injected into the game via LD_PRELOAD. "
                 "It provides the Shift+Tab interface, notifications, and FPS counter.\n\n"
                 "When disabled, the .so is not loaded at all, which can improve performance "
                 "and fix compatibility issues in some games.");

    m_enableMangoHud = new QCheckBox("MangoHud", this);
    m_enableMangoHud->setToolTip(
        "Enable MangoHud performance overlay.\n\n"
        "MangoHud displays real-time performance metrics including FPS, CPU/GPU usage, "
        "temperatures, frame times, and more. It is a Vulkan/OpenGL overlay similar to "
        "MSI Afterburner.\n\n"
        "The game is started through the 'mangohud' command. That is what OpenGL "
        "titles need — the MANGOHUD=1 variable on its own only enables the Vulkan "
        "overlay.\n\n"
        "Requires: MangoHud must be installed on your system (mangohud package).");

    if (!MangoHudDialog::isMangoHudInstalled()) {
        m_enableMangoHud->setEnabled(false);
        m_enableMangoHud->setToolTip("MangoHud is not installed.\n\n"
            "Install the mangohud package to enable this feature.");
    }

    m_mangoHudConfigBtn = new QPushButton("Configure", this);
    m_mangoHudConfigBtn->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %2; padding: 4px 12px; "
                "border: 1px solid %3; border-radius: 4px; font-size: 11px; }"
                "QPushButton:hover { background-color: %4; border: 1px solid %5; }")
            .arg(AppStyle::ColorBgButton, AppStyle::ColorTextPrimary,
                 AppStyle::ColorBorder, AppStyle::ColorBgButtonHover, AppStyle::ColorAccent));
    m_mangoHudConfigBtn->setVisible(false);

    auto* mangoRow = new QHBoxLayout;
    mangoRow->setSpacing(8);
    mangoRow->addWidget(m_enableMangoHud);
    mangoRow->addWidget(m_mangoHudConfigBtn);
    mangoRow->addStretch();
    layout->addLayout(mangoRow);

    connect(m_enableMangoHud, &QCheckBox::toggled, m_mangoHudConfigBtn, &QPushButton::setVisible);
    connect(m_mangoHudConfigBtn, &QPushButton::clicked, this, [this]() {
        MangoHudDialog dialog(this);
        dialog.exec();
    });

    connect(m_enableMangoHud, &QCheckBox::toggled, this, &SettingsPage::changed);
    pageLayout()->addWidget(group);

    QGroupBox* customGroup = new QGroupBox("Custom Launch Parameters", this);
    layout = new QVBoxLayout(customGroup);
    QLabel* label = new QLabel(
        "Extra options appended to the Steam launch string. Use this for anything "
        "not covered above (e.g. game arguments like /WineDetectionEnabled:False). "
        "Include %command% to control where it and any trailing game arguments go; "
        "otherwise the text is added as env vars before %command%.", this);
    label->setWordWrap(true);
    label->setStyleSheet(QString("font-size: 11px; color: %1;").arg(AppStyle::ColorTextMuted));
    layout->addWidget(label);

    m_customLaunchParams = new QPlainTextEdit(this);
    m_customLaunchParams->setPlaceholderText(
        "e.g.  WINEDLLOVERRIDES=\"d3d12=n\" %command% /WineDetectionEnabled:False");
    m_customLaunchParams->setMaximumHeight(90);
    m_customLaunchParams->setStyleSheet(
        "font-family: monospace; background-color: #1e1e1e; border: 1px solid #4a4a4a; "
        "border-radius: 4px; color: #c0c0c0; padding: 6px;");
    connect(m_customLaunchParams, &QPlainTextEdit::textChanged, this, &SettingsPage::changed);
    layout->addWidget(m_customLaunchParams);
    pageLayout()->addWidget(customGroup);
}

void AdvancedPage::load(const DLSSSettings& settings)
{
    SettingsPage::load(settings);
    const bool mangoAvailable = MangoHudDialog::isMangoHudInstalled();
    m_enableMangoHud->setChecked(mangoAvailable && settings.enableMangoHud);
    m_mangoHudConfigBtn->setVisible(mangoAvailable && settings.enableMangoHud);
    m_customLaunchParams->setPlainText(settings.customLaunchParams);
}

void AdvancedPage::store(DLSSSettings& settings) const
{
    SettingsPage::store(settings);
    settings.enableMangoHud = m_enableMangoHud->isChecked();
    settings.customLaunchParams = m_customLaunchParams->toPlainText().trimmed();
}

void AdvancedPage::setGame(const Game& game)
{
    SettingsPage::setGame(game);
    // The overlay is Steam's own, and GameRunner will not inject it elsewhere —
    // so a checkbox for it would be a control with no effect.
    m_enableSteamOverlay->setVisible(game.traits().usesSteamEnv);
}

void AdvancedPage::appendCustomParams(const QString& snippet)
{
    const QString existing = m_customLaunchParams->toPlainText().trimmed();
    m_customLaunchParams->setPlainText(existing.isEmpty() ? snippet : existing + "\n" + snippet);
}
