#include "RecommendationsDialog.h"
#include "AppStyle.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>

namespace {
// Maps a ProtonDB tier to a representative colour for the badge.
QString tierColor(const QString& tier)
{
    const QString t = tier.toLower();
    if (t == "platinum") return "#b4c7dc";
    if (t == "gold")     return "#cfb53b";
    if (t == "silver")   return "#a8a8a8";
    if (t == "bronze")   return "#cd7f32";
    if (t == "borked")   return AppStyle::ColorDanger;
    return AppStyle::ColorTextMuted;  // pending / unknown
}

QLabel* makeTierBadge(const ProtonDBClient::Summary& summary, QWidget* parent)
{
    QString text = summary.valid && !summary.tier.isEmpty()
        ? QString("ProtonDB: %1").arg(summary.tier.toUpper())
        : QString("ProtonDB: unknown");
    auto* badge = new QLabel(text, parent);
    badge->setStyleSheet(QString(
        "font-size: 12px; font-weight: bold; padding: 4px 10px; border-radius: 4px; "
        "background-color: %1; color: #1a1a1a;")
        .arg(tierColor(summary.tier)));
    badge->setMaximumWidth(220);
    if (summary.valid && summary.total > 0) {
        badge->setText(QString("ProtonDB: %1  (%2 reports)")
                           .arg(summary.tier.toUpper())
                           .arg(summary.total));
    }
    return badge;
}
} // namespace

RecommendationsDialog::RecommendationsDialog(
    const QString& appId,
    const QString& gameName,
    const ProtonDBClient::Summary& summary,
    const QList<LaunchOptionExtractor::Suggestion>& suggestions,
    const QString& message,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Recommended Launch Options");
    setMinimumWidth(560);
    setStyleSheet(QString("QDialog { background-color: %1; } QLabel { color: %2; }")
                      .arg(AppStyle::ColorBgBase, AppStyle::ColorTextPrimary));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Header: game name + tier badge + ProtonDB link
    auto* titleLabel = new QLabel(gameName.isEmpty() ? "Recommendations" : gameName, this);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    titleLabel->setWordWrap(true);
    mainLayout->addWidget(titleLabel);

    auto* headerRow = new QHBoxLayout();
    headerRow->addWidget(makeTierBadge(summary, this));
    headerRow->addStretch();
    auto* linkBtn = new QPushButton("View on ProtonDB", this);
    linkBtn->setStyleSheet(AppStyle::dialogButtonStyle());
    connect(linkBtn, &QPushButton::clicked, this, [appId]() {
        QDesktopServices::openUrl(QUrl(ProtonDBClient::appUrl(appId)));
    });
    headerRow->addWidget(linkBtn);
    mainLayout->addLayout(headerRow);

    if (!message.isEmpty()) {
        auto* msg = new QLabel(message, this);
        msg->setWordWrap(true);
        msg->setStyleSheet(QString("color: %1; font-size: 12px;").arg(AppStyle::ColorTextMuted));
        mainLayout->addWidget(msg);
    }

    if (!suggestions.isEmpty()) {
        auto* hint = new QLabel(
            "Mined from community reports. Click Apply to add a suggestion to this "
            "game's custom launch parameters — review it before launching.", this);
        hint->setWordWrap(true);
        hint->setStyleSheet(QString("color: %1; font-size: 12px;").arg(AppStyle::ColorTextMuted));
        mainLayout->addWidget(hint);

        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* content = new QWidget();
        content->setStyleSheet("background: transparent;");
        auto* listLayout = new QVBoxLayout(content);
        listLayout->setSpacing(8);

        for (const LaunchOptionExtractor::Suggestion& s : suggestions) {
            auto* card = new QFrame(content);
            card->setStyleSheet(QString(
                "QFrame { background-color: %1; border: 1px solid %2; border-radius: 6px; }")
                .arg(AppStyle::ColorBgCard, AppStyle::ColorBorder));
            auto* cardLayout = new QHBoxLayout(card);
            cardLayout->setContentsMargins(10, 8, 10, 8);

            auto* textCol = new QVBoxLayout();
            auto* snippetLabel = new QLabel(s.snippet, card);
            snippetLabel->setWordWrap(true);
            snippetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            snippetLabel->setStyleSheet(QString(
                "font-family: monospace; color: %1; background: transparent; border: none;")
                .arg(AppStyle::ColorTextPrimary));
            textCol->addWidget(snippetLabel);

            QStringList metaParts;
            metaParts << QString("%1 report%2").arg(s.occurrences).arg(s.occurrences == 1 ? "" : "s");
            if (!s.sampleSource.isEmpty()) {
                metaParts << s.sampleSource;
            }
            auto* metaLabel = new QLabel(metaParts.join("  ·  "), card);
            metaLabel->setStyleSheet(QString(
                "color: %1; font-size: 11px; background: transparent; border: none;")
                .arg(AppStyle::ColorTextMuted));
            textCol->addWidget(metaLabel);
            cardLayout->addLayout(textCol, 1);

            auto* applyBtn = new QPushButton("Apply", card);
            applyBtn->setStyleSheet(AppStyle::secondaryButtonStyle());
            const QString snippet = s.snippet;
            connect(applyBtn, &QPushButton::clicked, this, [this, snippet]() {
                emit applySnippet(snippet);
                accept();
            });
            cardLayout->addWidget(applyBtn, 0, Qt::AlignTop);

            listLayout->addWidget(card);
        }
        listLayout->addStretch();
        scroll->setWidget(content);
        mainLayout->addWidget(scroll, 1);
    }

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    auto* closeBtn = new QPushButton("Close", this);
    closeBtn->setStyleSheet(AppStyle::dialogButtonStyle());
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(closeBtn);
    mainLayout->addLayout(buttonRow);
}
