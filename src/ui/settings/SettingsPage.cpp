#include "ui/settings/SettingsPage.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>

#include "utils/EnvBuilder.h"

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
    , m_layout(new QVBoxLayout(this))
{
    // What the scroll area's content widget always carried; the global
    // stylesheet would otherwise paint the page over the tab background.
    setStyleSheet("background: transparent;");
    m_layout->setSpacing(10);
}

void SettingsPage::load(const DLSSSettings& settings)
{
    for (const Flag& f : m_flags)
        f.box->setChecked(settings.*f.field);
}

void SettingsPage::store(DLSSSettings& settings) const
{
    for (const Flag& f : m_flags)
        settings.*f.field = f.box->isChecked();
}

void SettingsPage::setGame(const Game& game)
{
    const bool native = game.isNativeLinux();
    for (const Gated& g : m_gated) {
        const bool inert = native &&
            EnvBuilder::scopeOf(QLatin1String(g.envVar)) == EnvBuilder::VarScope::ProtonOnly;
        g.widget->setEnabled(!inert);
    }
}

void SettingsPage::collectWarnings(const FeatureGate::Context& ctx, QStringList& lines) const
{
    for (const Flag& f : m_flags) {
        if (f.feature)
            check(f.box, f.box->isChecked(), *f.feature, f.warnLabel, ctx, lines);
    }
}

QList<QWidget*> SettingsPage::inputWidgets() const
{
    QList<QWidget*> out;
    for (QWidget* w : findChildren<QWidget*>()) {
        if (qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w) ||
            qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QLineEdit*>(w) ||
            qobject_cast<QPlainTextEdit*>(w)) {
            out << w;
        }
    }
    return out;
}

QCheckBox* SettingsPage::bindFlag(QBoxLayout* layout, bool DLSSSettings::* field,
                                  const QString& text, const QString& tooltip,
                                  const char* envVar,
                                  std::optional<FeatureGate::Feature> feature,
                                  const QString& warnLabel)
{
    auto* box = new QCheckBox(text, this);
    box->setToolTip(tooltip);
    layout->addWidget(box);
    connect(box, &QCheckBox::toggled, this, &SettingsPage::changed);

    m_flags.push_back({box, field, feature, warnLabel.isEmpty() ? text : warnLabel});
    if (envVar) gate(box, envVar);
    return box;
}

void SettingsPage::gate(QWidget* widget, const char* envVar)
{
    m_gated.push_back({widget, envVar});
}

void SettingsPage::check(QWidget* control, bool active, FeatureGate::Feature feature,
                         const QString& label, const FeatureGate::Context& ctx,
                         QStringList& lines)
{
    using namespace FeatureGate;
    if (!active || !control->isEnabled())
        return;
    const Result r = evaluate(requirementFor(feature), ctx);
    if (r.status != Status::Supported)
        lines << QString("⚠ %1: %2").arg(label, r.message);
}
