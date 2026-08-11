#include "SettingsDialog.h"
#include "AppStyle.h"
#include "StoreVisuals.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QFrame>
#include <QSpacerItem>
#include <QSettings>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include "gog/GogAuth.h"
#include "gog/GogInstallRegistry.h"
#include "launchers/SteamStoreService.h"
#include <QTimer>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumSize(600, 400);
    resize(840, 560);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI()
{
    setStyleSheet(AppStyle::dialogButtonStyle());

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

    // The list and the stack are indexed in lockstep — onCategoryChanged is a
    // plain setCurrentIndex(currentRow()) — so these two blocks must stay in
    // the same order.
    // Not a store and so not in the StoreVisuals table: this page is the Proton
    // download from GitHub Releases, which is what package.svg already means in
    // the Tools menu.
    auto* githubItem = new QListWidgetItem(
        StoreVisuals::circleIcon(QColor(AppStyle::ColorGitHub),
                                 QIcon(":/icons/package.svg")), "GitHub");
    githubItem->setSizeHint(QSize(0, 56));
    m_categoryList->addItem(githubItem);

    auto* steamItem = new QListWidgetItem(
        StoreVisuals::icon(QStringLiteral("Steam")), "Steam");
    steamItem->setSizeHint(QSize(0, 56));
    m_categoryList->addItem(steamItem);

    auto* gogItem = new QListWidgetItem(
        StoreVisuals::icon(QStringLiteral("GOG")), "GOG");
    gogItem->setSizeHint(QSize(0, 56));
    m_categoryList->addItem(gogItem);

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
    m_stack->addWidget(buildSteamPage());
    m_stack->addWidget(buildGogPage());
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

    m_saveButton = new QPushButton("Save");
    m_saveButton->setDefault(true);
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsDialog::saveSettings);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addSpacing(8);
    btnLayout->addWidget(m_saveButton);
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
        "QFrame { border: 1px solid #444; border-radius: 6px; background: #1e1e1e; }");
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(16, 16, 16, 16);
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
    tokenRow->setSpacing(8);
    m_tokenEdit = new QLineEdit;
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    m_tokenEdit->setPlaceholderText("ghp_...");
    m_tokenEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_tokenEdit->setStyleSheet(
        "QLineEdit { background: #2a2a2a; border: 1px solid #555; border-radius: 4px;"
        " padding: 4px 8px; color: #e0e0e0; }");

    m_toggleTokenBtn = new QPushButton("Show");
    m_toggleTokenBtn->setFlat(true);
    m_toggleTokenBtn->setMinimumWidth(60);
    m_toggleTokenBtn->setStyleSheet(
        QString("QPushButton { color: %1; border: none; background: transparent; }"
        "QPushButton:hover { color: %2; }")
        .arg(AppStyle::ColorAccent, AppStyle::ColorAccentHover));

    connect(m_toggleTokenBtn, &QPushButton::clicked, this, [this]() {
        bool hidden = m_tokenEdit->echoMode() == QLineEdit::Password;
        m_tokenEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        m_toggleTokenBtn->setText(hidden ? "Hide" : "Show");
    });

    tokenRow->addWidget(m_tokenEdit, 1);
    tokenRow->addWidget(m_toggleTokenBtn, 0);

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

QWidget* SettingsDialog::buildSteamPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* header = new QLabel("<b>Steam Web API</b>");
    header->setStyleSheet("color: #e0e0e0; font-size: 14px;");

    auto* description = new QLabel(
        "Lets ProtonForge list the Steam games you own but have not installed. "
        "Everything else about Steam works without this.");
    description->setWordWrap(true);
    description->setStyleSheet("color: #999; font-size: 12px;");

    auto* keyLabel = new QLabel("API key");
    keyLabel->setStyleSheet("color: #ccc; font-size: 12px;");

    m_steamApiKeyEdit = new QLineEdit;
    m_steamApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_steamApiKeyEdit->setPlaceholderText("32 hex characters");

    auto* toggle = new QPushButton("Show");
    toggle->setFlat(true);
    connect(toggle, &QPushButton::clicked, this, [this, toggle]() {
        const bool hidden = m_steamApiKeyEdit->echoMode() == QLineEdit::Password;
        m_steamApiKeyEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        toggle->setText(hidden ? "Hide" : "Show");
    });

    auto* keyRow = new QHBoxLayout();
    keyRow->addWidget(m_steamApiKeyEdit, 1);
    keyRow->addWidget(toggle);

    auto* link = new QLabel(
        "<a href='https://steamcommunity.com/dev/apikey' style='color:#58a6ff;'>"
        "Get a key from Steam ↗</a>");
    link->setOpenExternalLinks(true);

    auto* idLabel = new QLabel("SteamID64 (detected automatically)");
    idLabel->setStyleSheet("color: #ccc; font-size: 12px;");

    m_steamIdEdit = new QLineEdit;
    m_steamIdEdit->setPlaceholderText(SteamStoreService::resolveSteamId().isEmpty()
        ? QStringLiteral("no local Steam account found — enter it here")
        : SteamStoreService::resolveSteamId());

    auto* idHint = new QLabel(
        "Read from Steam's own loginusers.vdf. Only fill this in if the detected "
        "account is the wrong one, or Steam is not installed here.");
    idHint->setWordWrap(true);
    idHint->setStyleSheet("color: #777; font-size: 11px;");

    layout->addWidget(header);
    layout->addWidget(description);
    layout->addSpacing(8);
    layout->addWidget(keyLabel);
    layout->addLayout(keyRow);
    layout->addWidget(link);
    layout->addSpacing(12);
    layout->addWidget(idLabel);
    layout->addWidget(m_steamIdEdit);
    layout->addWidget(idHint);
    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::buildGogPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto* header = new QLabel("<b>GOG</b>");
    header->setStyleSheet("color: #e0e0e0; font-size: 14px;");

    const bool signedIn = GogAuth::instance().isLoggedIn();
    auto* status = new QLabel(signedIn
        ? QStringLiteral("Signed in. Credentials are held in the %1.")
              .arg(SecretStore::instance().backendName() == QLatin1String("keyring")
                       ? QStringLiteral("system keyring")
                       : QStringLiteral("ProtonForge credential file"))
        : QStringLiteral("Not signed in. Use Library → Game Stores to sign in."));
    status->setWordWrap(true);
    status->setStyleSheet("color: #999; font-size: 12px;");

    auto* rootLabel = new QLabel("Install location");
    rootLabel->setStyleSheet("color: #ccc; font-size: 12px;");

    m_gogInstallRootEdit = new QLineEdit;
    m_gogInstallRootEdit->setPlaceholderText(QDir::homePath() + "/Games/ProtonForge");

    auto* browseButton = new QPushButton("Browse…");
    auto* rootRow = new QHBoxLayout;
    rootRow->setContentsMargins(0, 0, 0, 0);
    rootRow->addWidget(m_gogInstallRootEdit, 1);
    rootRow->addWidget(browseButton);

    // Shown only when the chosen directory would fail, and not as a modal: the
    // user may well be about to plug the drive in or fix the permissions, and
    // an install this would have broken is otherwise several gigabytes away
    // from the message.
    auto* rootWarning = new QLabel;
    rootWarning->setWordWrap(true);
    rootWarning->setStyleSheet("color: #f0a30a; font-size: 11px;");
    rootWarning->hide();

    connect(browseButton, &QPushButton::clicked, this, [this, rootWarning]() {
        // Start where the setting points, or where it would default to. That
        // directory need not exist yet — nothing has been installed on a fresh
        // profile — so walk up to the nearest one that does, or the picker
        // opens on nothing.
        const QString configured = m_gogInstallRootEdit->text().trimmed();
        QDir start(configured.isEmpty() ? GogInstallRegistry::installRoot() : configured);
        while (!start.exists() && !start.isRoot() && start.cdUp()) {
        }

        const QString chosen = QFileDialog::getExistingDirectory(
            this, "Choose where GOG games are installed",
            start.exists() ? start.absolutePath() : QDir::homePath());
        if (chosen.isEmpty()) {
            return;   // cancelled — leave whatever was typed alone
        }

        m_gogInstallRootEdit->setText(QDir::toNativeSeparators(chosen));

        const QFileInfo info(chosen);
        if (!info.isWritable()) {
            rootWarning->setText(QStringLiteral(
                "ProtonForge cannot write to %1. Installing there will fail until "
                "the permissions allow it.").arg(chosen));
            rootWarning->show();
        } else {
            rootWarning->hide();
        }
    });

    auto* rootHint = new QLabel(
        "Games go under &lt;location&gt;/GOG, and their Proton prefixes under "
        "&lt;location&gt;/prefixes/GOG. A path on another drive works, but in the "
        "Flatpak build it has to be somewhere the sandbox can reach.");
    rootHint->setWordWrap(true);
    rootHint->setStyleSheet("color: #777; font-size: 11px;");

    auto* languageLabel = new QLabel("Preferred game language");
    languageLabel->setStyleSheet("color: #ccc; font-size: 12px;");

    // GOG's own language tags. A build that does not publish the chosen one
    // still installs — GogInstallPlan always takes the shared depot, so the
    // game arrives, just without that language's audio.
    m_gogLanguageBox = new QComboBox;
    m_gogLanguageBox->addItem("English", "en-US");
    m_gogLanguageBox->addItem("Deutsch", "de-DE");
    m_gogLanguageBox->addItem("Français", "fr-FR");
    m_gogLanguageBox->addItem("Español", "es-ES");
    m_gogLanguageBox->addItem("Italiano", "it-IT");
    m_gogLanguageBox->addItem("Polski", "pl-PL");
    m_gogLanguageBox->addItem("Português do Brasil", "pt-BR");
    m_gogLanguageBox->addItem("Русский", "ru-RU");
    m_gogLanguageBox->addItem("中文", "zh-Hans");
    m_gogLanguageBox->addItem("日本語", "ja-JP");

    auto* languageHint = new QLabel(
        "Used for new installs. Games that do not offer this language install "
        "anyway, without its audio.");
    languageHint->setWordWrap(true);
    languageHint->setStyleSheet("color: #777; font-size: 11px;");

    layout->addWidget(header);
    layout->addWidget(status);
    layout->addSpacing(12);
    layout->addWidget(rootLabel);
    layout->addLayout(rootRow);
    layout->addWidget(rootWarning);
    layout->addWidget(rootHint);
    layout->addSpacing(12);
    layout->addWidget(languageLabel);
    layout->addWidget(m_gogLanguageBox);
    layout->addWidget(languageHint);
    layout->addStretch();
    return page;
}

void SettingsDialog::loadSettings()
{
    SecretStore& store = SecretStore::instance();
    m_tokenEdit->setText(store.value(SecretStore::Key::GitHubToken));
    m_steamApiKeyEdit->setText(store.value(SecretStore::Key::SteamWebApiKey));

    QSettings settings;
    m_steamIdEdit->setText(settings.value("steam/steamId64").toString());
    m_gogInstallRootEdit->setText(settings.value("gog/installRoot").toString());
    const int languageIndex =
        m_gogLanguageBox->findData(settings.value("gog/language", "en-US").toString());
    m_gogLanguageBox->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
}

void SettingsDialog::saveSettings()
{
    // The keychain write is asynchronous, so the dialog must not close on top of
    // it. Closing first is how "it didn't save my token" happens: the write
    // fails, the failure has nowhere to go, and the field looks like it took.
    m_saveButton->setEnabled(false);
    m_saveButton->setText("Saving…");

    SecretStore& store = SecretStore::instance();
    connect(&store, &SecretStore::writeFailed, this, &SettingsDialog::onSecretWriteFailed,
            Qt::UniqueConnection);

    store.setValue(SecretStore::Key::GitHubToken, m_tokenEdit->text().trimmed());
    store.setValue(SecretStore::Key::SteamWebApiKey, m_steamApiKeyEdit->text().trimmed());

    // Not credentials, so plain QSettings is the right home for these.
    QSettings settings;
    const QString steamId = m_steamIdEdit->text().trimmed();
    const QString installRoot = m_gogInstallRootEdit->text().trimmed();
    steamId.isEmpty() ? settings.remove("steam/steamId64")
                      : settings.setValue("steam/steamId64", steamId);
    installRoot.isEmpty() ? settings.remove("gog/installRoot")
                          : settings.setValue("gog/installRoot", installRoot);
    settings.setValue("gog/language", m_gogLanguageBox->currentData().toString());

    // Nothing reports success, only failure — so give the write a moment to fail
    // and accept if it did not. A keychain round trip is milliseconds; this is
    // long enough to catch a refusal and short enough not to be noticed.
    QTimer::singleShot(400, this, [this]() {
        if (m_saveButton->isEnabled()) {
            return;   // a failure already re-enabled it and explained itself
        }
        accept();
    });
}

void SettingsDialog::onSecretWriteFailed(SecretStore::Key key, const QString& reason)
{
    Q_UNUSED(key);
    m_saveButton->setEnabled(true);
    m_saveButton->setText("Save");
    QMessageBox::warning(this, "Could not save credentials",
        QString("ProtonForge could not store the token in the system keyring.\n\n%1")
            .arg(reason));
}

void SettingsDialog::onCategoryChanged()
{
    m_stack->setCurrentIndex(m_categoryList->currentRow());
}
