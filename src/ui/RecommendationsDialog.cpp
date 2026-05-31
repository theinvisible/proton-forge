#include "RecommendationsDialog.h"
#include "AppStyle.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QStringList>
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

// Builds the full comment/context block for a suggestion from its source reports.
QString buildContextText(const QList<ProtonDBClient::Report>& sources)
{
    QStringList blocks;
    for (const ProtonDBClient::Report& r : sources) {
        if (r.notes.trimmed().isEmpty()) {
            continue;
        }
        QStringList head;
        if (!r.protonVersion.isEmpty()) head << "Proton " + r.protonVersion;
        if (!r.gpuDriver.isEmpty()) head << r.gpuDriver;

        QString block;
        if (!head.isEmpty()) block += head.join("  ·  ") + "\n";
        block += r.notes.trimmed();
        if (!r.launchOptions.trimmed().isEmpty()) {
            block += "\n↳ " + r.launchOptions.trimmed();
        }
        blocks << block;
    }
    return blocks.join("\n\n────────\n\n");
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
            auto* cardOuter = new QVBoxLayout(card);
            cardOuter->setContentsMargins(10, 8, 10, 8);
            cardOuter->setSpacing(6);

            auto* topRow = new QHBoxLayout();
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
            topRow->addLayout(textCol, 1);

            auto* btnCol = new QVBoxLayout();
            auto* applyBtn = new QPushButton("Apply", card);
            applyBtn->setStyleSheet(AppStyle::secondaryButtonStyle());
            const QString snippet = s.snippet;
            connect(applyBtn, &QPushButton::clicked, this, [this, snippet]() {
                emit applySnippet(snippet);
                accept();
            });
            btnCol->addWidget(applyBtn);

            // Expandable full comment(s) the snippet came from — lets the user see
            // the context the command was given in.
            const QString context = buildContextText(s.sources);
            if (!context.isEmpty()) {
                auto* commentBtn = new QPushButton("Kommentar ▾", card);
                commentBtn->setStyleSheet(AppStyle::dialogButtonStyle());
                btnCol->addWidget(commentBtn);

                auto* commentLabel = new QLabel(context, card);
                commentLabel->setWordWrap(true);
                commentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
                commentLabel->setStyleSheet(QString(
                    "background-color: %1; border: 1px solid %2; border-radius: 4px; "
                    "padding: 6px; color: %3; font-size: 12px;")
                    .arg(AppStyle::ColorBgInput, AppStyle::ColorBorder, AppStyle::ColorTextSecondary));
                commentLabel->setVisible(false);
                cardOuter->addWidget(commentLabel);  // added after topRow below

                connect(commentBtn, &QPushButton::clicked, card, [commentBtn, commentLabel]() {
                    const bool show = !commentLabel->isVisible();
                    commentLabel->setVisible(show);
                    commentBtn->setText(show ? "Kommentar ▴" : "Kommentar ▾");
                });
            }
            btnCol->addStretch();
            topRow->addLayout(btnCol, 0);

            cardOuter->insertLayout(0, topRow);  // ensure topRow sits above the comment
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
