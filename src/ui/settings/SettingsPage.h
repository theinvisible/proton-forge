#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QBoxLayout>
#include <vector>

#include "core/DLSSSettings.h"
#include "core/Game.h"

// One tab of the settings panel. DLSSSettingsWidget owns the pages and asks
// each of them to show a game's settings, hand them back and grey out what
// cannot reach the game. What the detected driver, GPU or Proton cannot do is
// not a page's business: FeatureWarnings works that out from the settings.
//
// A plain on/off option needs nothing but one bindFlag() call: it creates the
// checkbox and the base class then loads, stores and gates it. Pages override
// the three virtuals for their other controls and call the base.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

    virtual void load(const DLSSSettings& settings);
    virtual void store(DLSSSettings& settings) const;
    // Called before load() whenever another game is selected.
    virtual void setGame(const Game& game);

    // Every control a user can change, so the panel can block their signals
    // while it writes a game's settings into them.
    QList<QWidget*> inputWidgets() const;

signals:
    void changed();

protected:
    // A checkbox for `field`. `envVar` is the variable it writes: on a native
    // Linux game it is greyed out when only Proton reads that variable.
    QCheckBox* bindFlag(QBoxLayout* layout, bool DLSSSettings::* field,
                        const QString& text, const QString& tooltip,
                        const char* envVar = nullptr);

    // Greys `widget` out on a native Linux game when `envVar` is Proton-only.
    // The value is untouched — setEnabled() does not change isChecked() — so
    // the setting survives, and still applies if Steam is told to use Proton.
    void gate(QWidget* widget, const char* envVar);

    // Groups stacked vertically, the way every tab is laid out.
    QVBoxLayout* pageLayout() const { return m_layout; }

private:
    struct Flag {
        QCheckBox* box;
        bool DLSSSettings::* field;
    };
    struct Gated {
        QWidget* widget;
        const char* envVar;
    };

    QVBoxLayout* m_layout;
    std::vector<Flag> m_flags;
    std::vector<Gated> m_gated;
};

#endif // SETTINGSPAGE_H
