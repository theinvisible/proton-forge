#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QFrame>
#include <QSpacerItem>
#include <QSettings>
#include <QDialogButtonBox>
#include <QPixmap>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumSize(600, 400);
    setupUI();
    loadSettings();
}

QIcon SettingsDialog::makeCategoryIcon(const QColor& color, const QString& letter)
{
    QPixmap pixmap(36, 36);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, 36, 36);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(16);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRect(0, 0, 36, 36), Qt::AlignCenter, letter);

    return QIcon(pixmap);
}

void SettingsDialog::setupUI()
{
    setStyleSheet(
        "QPushButton {"
        "    background-color: #333333;"
        "    color: #cccccc;"
        "    border: 1px solid #555555;"
        "    padding: 6px 16px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #404040;"
        "    border: 1px solid #76B900;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #2a2a2a;"
        "}");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 12);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // --- Left panel ---
    auto* leftWidget = new QWidget;
    leftWidget->setMinimumWidth(180);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(8, 12, 8, 8);
    leftLayout->setSpacing(6);

    auto* titleLabel = new QLabel("Settings");
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPixelSize(13);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet("color: #e0e0e0; padding: 4px 4px 8px 4px;");

    m_categoryList = new QListWidget;
    m_categoryList->setIconSize(QSize(36, 36));
    m_categoryList->setStyleSheet(
        "QListWidget {"
        "    background: transparent;"
        "    border: none;"
        "    outline: none;"
        "}"
        "QListWidget::item {"
        "    border-radius: 6px;"
        "    padding: 4px 8px;"
        "    margin: 2px 0;"
        "    color: #ccc;"
        "}"
        "QListWidget::item:hover {"
        "    background: rgba(255,255,255,0.06);"
        "    color: #fff;"
        "}"
        "QListWidget::item:selected {"
        "    background: rgba(31,111,235,0.25);"
        "    color: #fff;"
        "}"
    );

    auto* githubItem = new QListWidgetItem(
        makeCategoryIcon(QColor("#1f6feb"), "G"), "GitHub");
    githubItem->setSizeHint(QSize(0, 56));
    m_categoryList->addItem(githubItem);

    leftLayout->addWidget(titleLabel);
    leftLayout->addWidget(m_categoryList);

    // --- Right panel ---
    auto* rightWidget = new QWidget;
    rightWidget->setMinimumWidth(380);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildGithubPage());
    rightLayout->addWidget(m_stack);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({180, 420});

    mainLayout->addWidget(splitter, 1);

    // --- Button bar ---
    auto* btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(12, 0, 12, 0);
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* saveBtn = new QPushButton("Save");
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::saveSettings);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_categoryList, &QListWidget::currentRowChanged,
            this, &SettingsDialog::onCategoryChanged);

    m_categoryList->setCurrentRow(0);
}

QWidget* SettingsDialog::buildGithubPage()
{
    auto* page = new QWidget;
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(16, 16, 16, 16);
    pageLayout->setSpacing(0);

    // Card frame
    auto* frame = new QFrame;
    frame->setStyleSheet(
        "QFrame { border: 1px solid #444; border-radius: 6px; background: #1e1e1e; padding: 12px; }");
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setSpacing(6);

    auto* headerLabel = new QLabel("GitHub API Token");
    QFont headerFont = headerLabel->font();
    headerFont.setBold(true);
    headerFont.setPixelSize(14);
    headerLabel->setFont(headerFont);
    headerLabel->setStyleSheet("color: #e0e0e0; border: none; background: transparent;");

    auto* descLabel = new QLabel(
        "Increases the rate limit from 60 to 5,000 requests/hour.\n"
        "Required when fetching Proton versions hits the API limit.");
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: #888; font-size: 11px; border: none; background: transparent;");

    auto* spacer8 = new QSpacerItem(0, 8, QSizePolicy::Minimum, QSizePolicy::Fixed);

    auto* tokenLabel = new QLabel("Personal Access Token");
    tokenLabel->setStyleSheet("color: #ccc; border: none; background: transparent;");

    auto* tokenRow = new QHBoxLayout;
    m_tokenEdit = new QLineEdit;
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    m_tokenEdit->setPlaceholderText("ghp_...");
    m_tokenEdit->setStyleSheet(
        "QLineEdit { background: #2a2a2a; border: 1px solid #555; border-radius: 4px;"
        " padding: 4px 8px; color: #e0e0e0; }");

    m_toggleTokenBtn = new QPushButton("Show");
    m_toggleTokenBtn->setFlat(true);
    m_toggleTokenBtn->setFixedWidth(50);
    m_toggleTokenBtn->setStyleSheet(
        "QPushButton { color: #76B900; border: none; background: transparent; }"
        "QPushButton:hover { color: #9fe02a; }");

    connect(m_toggleTokenBtn, &QPushButton::clicked, this, [this]() {
        bool hidden = m_tokenEdit->echoMode() == QLineEdit::Password;
        m_tokenEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        m_toggleTokenBtn->setText(hidden ? "Hide" : "Show");
    });

    tokenRow->addWidget(m_tokenEdit);
    tokenRow->addWidget(m_toggleTokenBtn);

    auto* spacer12 = new QSpacerItem(0, 12, QSizePolicy::Minimum, QSizePolicy::Fixed);

    auto* linkLabel = new QLabel(
        "<a href='https://github.com/settings/tokens/new?scopes=public_repo' "
        "style='color: #58a6ff;'>Create token on GitHub ↗</a>");
    linkLabel->setOpenExternalLinks(true);
    linkLabel->setStyleSheet("border: none; background: transparent;");

    frameLayout->addWidget(headerLabel);
    frameLayout->addWidget(descLabel);
    frameLayout->addSpacerItem(spacer8);
    frameLayout->addWidget(tokenLabel);
    frameLayout->addLayout(tokenRow);
    frameLayout->addSpacerItem(spacer12);
    frameLayout->addWidget(linkLabel);

    pageLayout->addWidget(frame);
    pageLayout->addStretch();

    return page;
}

void SettingsDialog::loadSettings()
{
    QSettings s;
    m_tokenEdit->setText(s.value("github/apiToken").toString());
}

void SettingsDialog::saveSettings()
{
    QSettings s;
    QString token = m_tokenEdit->text().trimmed();
    if (token.isEmpty())
        s.remove("github/apiToken");
    else
        s.setValue("github/apiToken", token);
    accept();
}

void SettingsDialog::onCategoryChanged()
{
    m_stack->setCurrentIndex(m_categoryList->currentRow());
}
