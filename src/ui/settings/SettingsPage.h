#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QBoxLayout>
#include <QStringList>
#include <optional>
#include <vector>

#include "core/DLSSSettings.h"
#include "core/FeatureGate.h"
#include "core/Game.h"

// One tab of the settings panel. DLSSSettingsWidget owns the pages and asks
// each of them to show a game's settings, hand them back, grey out what
// cannot reach the game and report what the detected driver, GPU or Proton
// cannot do.
//
// A plain on/off option needs nothing but one bindFlag() call: it creates the
// checkbox and the base class then loads, stores, gates and checks it. Pages
// override the four virtuals for their other controls and call the base.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

    virtual void load(const DLSSSettings& settings);
    virtual void store(DLSSSettings& settings) const;
    // Called before load() whenever another game is selected.
    virtual void setGame(const Game& game);
    virtual void collectWarnings(const FeatureGate::Context& ctx, QStringList& lines) const;

    // Every control a user can change, so the panel can block their signals
    // while it writes a game's settings into them.
    QList<QWidget*> inputWidgets() const;

signals:
    void changed();

protected:
    // A checkbox for `field`. `envVar` is the variable it writes: on a native
    // Linux game it is greyed out when only Proton reads that variable.
    // With a `feature`, it is checked against FeatureGate while ticked.
    QCheckBox* bindFlag(QBoxLayout* layout, bool DLSSSettings::* field,
                        const QString& text, const QString& tooltip,
                        const char* envVar = nullptr,
                        std::optional<FeatureGate::Feature> feature = std::nullopt,
                        const QString& warnLabel = QString());

    // Greys `widget` out on a native Linux game when `envVar` is Proton-only.
    // The value is untouched — setEnabled() does not change isChecked() — so
    // the setting survives, and still applies if Steam is told to use Proton.
    void gate(QWidget* widget, const char* envVar);

    // The FeatureGate check every warning goes through: only for a control
    // that is switched on and live.
    static void check(QWidget* control, bool active, FeatureGate::Feature feature,
                      const QString& label, const FeatureGate::Context& ctx,
                      QStringList& lines);

    // Groups stacked vertically, the way every tab is laid out.
    QVBoxLayout* pageLayout() const { return m_layout; }

private:
    struct Flag {
        QCheckBox* box;
        bool DLSSSettings::* field;
        std::optional<FeatureGate::Feature> feature;
        QString warnLabel;
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
