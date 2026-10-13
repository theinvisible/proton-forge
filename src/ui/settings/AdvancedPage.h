#ifndef ADVANCEDPAGE_H
#define ADVANCEDPAGE_H

#include <QPlainTextEdit>
#include <QPushButton>

#include "ui/settings/SettingsPage.h"

// Advanced tab: overlays and the free-form custom launch parameters.
class AdvancedPage : public SettingsPage {
    Q_OBJECT

public:
    explicit AdvancedPage(QWidget* parent = nullptr);

    void load(const DLSSSettings& settings) override;
    void store(DLSSSettings& settings) const override;
    void setGame(const Game& game) override;

    // A ProtonDB suggestion the user chose to apply.
    void appendCustomParams(const QString& snippet);

private:
    QCheckBox* m_enableSteamOverlay;
    QCheckBox* m_enableMangoHud;
    QPushButton* m_mangoHudConfigBtn;
    QPlainTextEdit* m_customLaunchParams;
};

#endif // ADVANCEDPAGE_H
