#include "StoreLibraryDialog.h"
#include "AppStyle.h"
#include "network/ImageCache.h"
#include "launchers/LauncherManager.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QMessageBox>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int RoleEntryId = Qt::UserRole;
constexpr int RoleStoreIndex = Qt::UserRole;

QString listStyle()
{
    return QStringLiteral(
        "QListWidget { border: 1px solid #444; border-radius: 6px; background: #1e1e1e;"
        "  padding: 4px; color: #e0e0e0; }"
        "QListWidget::item { padding: 6px 10px; border-radius: 4px; margin: 2px 4px; }"
        "QListWidget::item:selected { background: #1e3a0a; color: #9dff00;"
        "  border: 1px solid #77c71f; }"
        "QListWidget::item:hover:!selected { background: #2a2a2a; }");
}

QLabel* sectionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet("color: #aaa; font-size: 11px; font-weight: bold;");
    return label;
}

} // namespace

StoreLibraryDialog::StoreLibraryDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Game Stores");
    resize(1000, 620);
    setupUI();

    // getImage() answers with a placeholder and fetches in the background, so
    // the first look at any game showed no cover — and the second one did,
    // because by then it was cached. The panel has to be told when the picture
    // arrives. Connected once, here, and keyed on what is on screen at that
    // moment rather than on a captured entry: a connection made per selection
    // accumulates, and each stale copy would then repaint the panel with the
    // cover of a game the user has already navigated away from.
    connect(&ImageCache::instance(), &ImageCache::imageReady, this,
            [this](const QString& url) {
        const QListWidgetItem* item = m_entryList->currentItem();
        if (!item) {
            return;
        }
        if (entryById(item->data(RoleEntryId).toString()).imageUrl != url) {
            return;
        }
        m_detailImage->setPixmap(ImageCache::instance().getImage(url, QSize(230, 107)));
    });

    populateStores();
}

void StoreLibraryDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // --- left: the stores ---
    auto* leftPanel = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_storeList = new QListWidget(leftPanel);
    m_storeList->setStyleSheet(listStyle());
    leftLayout->addWidget(sectionLabel("STORE", leftPanel));
    leftLayout->addWidget(m_storeList);

    // --- middle: this store's games, or why there are none ---
    auto* midPanel = new QWidget(splitter);
    auto* midLayout = new QVBoxLayout(midPanel);
    midLayout->setContentsMargins(0, 0, 0, 0);

    m_searchBox = new QLineEdit(midPanel);
    m_searchBox->setPlaceholderText("Search your library...");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setStyleSheet(
        "QLineEdit { background: #1e1e1e; border: 1px solid #3a3a3a; border-radius: 6px;"
        "  padding: 7px 10px; color: #e0e0e0; }"
        "QLineEdit:focus { border: 1px solid #76B900; }");

    m_middleStack = new QStackedWidget(midPanel);
    m_entryList = new QListWidget(m_middleStack);
    m_entryList->setStyleSheet(listStyle());

    auto* messagePage = new QWidget(m_middleStack);
    auto* messageLayout = new QVBoxLayout(messagePage);
    messageLayout->setAlignment(Qt::AlignCenter);
    m_middleMessage = new QLabel(messagePage);
    m_middleMessage->setWordWrap(true);
    m_middleMessage->setAlignment(Qt::AlignCenter);
    m_middleMessage->setStyleSheet("color: #aaa;");
    m_middleAction = new QPushButton(messagePage);
    m_middleAction->setStyleSheet(AppStyle::dialogButtonStyle());
    messageLayout->addWidget(m_middleMessage);
    messageLayout->addSpacing(12);
    messageLayout->addWidget(m_middleAction, 0, Qt::AlignCenter);

    m_middleStack->addWidget(m_entryList);    // 0
    m_middleStack->addWidget(messagePage);    // 1

    midLayout->addWidget(sectionLabel("YOUR LIBRARY", midPanel));
    midLayout->addWidget(m_searchBox);
    midLayout->addWidget(m_middleStack, 1);

    // --- right: the selected game ---
    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_detailImage = new QLabel(rightPanel);
    m_detailImage->setFixedHeight(107);
    m_detailImage->setAlignment(Qt::AlignCenter);
    m_detailImage->setStyleSheet("background: #1a1a1a; border-radius: 6px;");

    m_detailTitle = new QLabel(rightPanel);
    m_detailTitle->setWordWrap(true);
    m_detailTitle->setStyleSheet("color: #e0e0e0; font-size: 15px; font-weight: bold;");

    m_detailBody = new QLabel(rightPanel);
    m_detailBody->setWordWrap(true);
    m_detailBody->setTextFormat(Qt::RichText);
    m_detailBody->setAlignment(Qt::AlignTop);
    m_detailBody->setStyleSheet("color: #aaa; font-size: 12px;");

    m_storePageButton = new QPushButton("View store page", rightPanel);
    m_storePageButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    m_installButton = new QPushButton("Install", rightPanel);
    m_installButton->setStyleSheet(AppStyle::dialogButtonStyle());
    m_uninstallButton = new QPushButton("Uninstall", rightPanel);
    m_uninstallButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    m_uninstallButton->hide();

    rightLayout->addWidget(sectionLabel("DETAILS", rightPanel));
    rightLayout->addWidget(m_detailImage);
    rightLayout->addWidget(m_detailTitle);
    rightLayout->addWidget(m_detailBody, 1);
    rightLayout->addWidget(m_storePageButton);
    rightLayout->addWidget(m_installButton);
    rightLayout->addWidget(m_uninstallButton);

    splitter->addWidget(leftPanel);
    splitter->addWidget(midPanel);
    splitter->addWidget(rightPanel);
    splitter->setSizes({180, 480, 320});
    mainLayout->addWidget(splitter, 1);

    // A hairline busy bar while a library is being fetched — no numbers to show,
    // and a full frame for that would be noise.
    m_libraryBusy = new QProgressBar(this);
    m_libraryBusy->setRange(0, 0);
    m_libraryBusy->setTextVisible(false);
    m_libraryBusy->setFixedHeight(4);
    m_libraryBusy->hide();
    mainLayout->addWidget(m_libraryBusy);

    // Install progress, fed by whichever service is doing the installing. The
    // dialog never learns which one that is.
    m_progressFrame = new QFrame(this);
    m_progressFrame->setStyleSheet(
        "QFrame { background: #1e1e1e; border: 1px solid #3a3a3a; border-radius: 6px; }");
    auto* progressLayout = new QVBoxLayout(m_progressFrame);
    progressLayout->setContentsMargins(10, 8, 10, 8);
    progressLayout->setSpacing(6);
    m_progressLabel = new QLabel(m_progressFrame);
    m_progressLabel->setStyleSheet("color: #e0e0e0; font-size: 12px; border: none;");
    m_progress = new QProgressBar(m_progressFrame);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    auto* backgroundNote = new QLabel(
        "Downloads continue in the background if you close this window.", m_progressFrame);
    backgroundNote->setStyleSheet("color: #777; font-size: 11px; border: none;");
    m_pauseButton = new QPushButton("Pause", m_progressFrame);
    m_pauseButton->setStyleSheet(AppStyle::secondaryButtonStyle());

    auto* progressTop = new QHBoxLayout();
    progressTop->addWidget(m_progressLabel, 1);
    progressTop->addWidget(m_pauseButton, 0);

    progressLayout->addLayout(progressTop);
    progressLayout->addWidget(m_progress);
    progressLayout->addWidget(backgroundNote);
    m_progressFrame->hide();
    mainLayout->addWidget(m_progressFrame);

    auto* buttons = new QHBoxLayout();
    m_signOutButton = new QPushButton("Sign out", this);
    m_signOutButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    m_refreshButton = new QPushButton("Refresh", this);
    m_refreshButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    auto* closeButton = new QPushButton("Close", this);
    closeButton->setStyleSheet(AppStyle::dialogButtonStyle());
    buttons->addWidget(m_signOutButton);
    buttons->addWidget(m_refreshButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    mainLayout->addLayout(buttons);

    connect(m_storeList, &QListWidget::itemSelectionChanged, this,
            &StoreLibraryDialog::onStoreSelected);
    connect(m_entryList, &QListWidget::itemSelectionChanged, this,
            &StoreLibraryDialog::onEntrySelected);
    connect(m_searchBox, &QLineEdit::textChanged, this, &StoreLibraryDialog::onSearchChanged);
    connect(m_middleAction, &QPushButton::clicked, this, &StoreLibraryDialog::onSignInClicked);
    connect(m_signOutButton, &QPushButton::clicked, this, &StoreLibraryDialog::onSignOutClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &StoreLibraryDialog::onRefreshClicked);
    connect(m_installButton, &QPushButton::clicked, this, &StoreLibraryDialog::onInstallClicked);
    connect(m_uninstallButton, &QPushButton::clicked, this,
            &StoreLibraryDialog::onUninstallClicked);
    connect(m_pauseButton, &QPushButton::clicked, this, &StoreLibraryDialog::onPauseClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_storePageButton, &QPushButton::clicked, this, [this]() {
        const QListWidgetItem* item = m_entryList->currentItem();
        if (!item) {
            return;
        }
        const QString id = item->data(RoleEntryId).toString();
        for (const StoreEntry& entry : m_entriesByLauncher.value(currentService()->launcherName())) {
            if (entry.id == id && !entry.storeUrl.isEmpty()) {
                QDesktopServices::openUrl(QUrl(entry.storeUrl));
                return;
            }
        }
    });

    clearDetails();
}

void StoreLibraryDialog::populateStores()
{
    // Every launcher that has an account behind it. No store is named here.
    for (const auto& launcher : LauncherManager::instance().launchers()) {
        if (IStoreService* service = launcher->storeService()) {
            m_services.append(service);

            connect(service, &IStoreService::libraryReady, this,
                    [this, service](const QList<StoreEntry>& entries) {
                m_entriesByLauncher.insert(service->launcherName(), entries);
                if (currentService() == service) {
                    m_libraryBusy->hide();
                    refreshInstalledState();
                    rebuildEntryList();
                }
            });
            connect(service, &IStoreService::libraryFailed, this,
                    [this, service](const QString& reason) {
                m_entriesByLauncher.remove(service->launcherName());
                if (currentService() == service) {
                    m_libraryBusy->hide();
                    m_middleMessage->setText(reason);
                    m_middleAction->setVisible(service->canSignIn());
                    m_middleAction->setText("Sign in…");
                    m_middleStack->setCurrentIndex(1);
                }
            });
            connect(service, &IStoreService::authStateChanged, this, [this, service](bool) {
                if (currentService() == service) {
                    m_entriesByLauncher.remove(service->launcherName());
                    showStoreState();
                }
            });

            connect(service, &IStoreService::installProgress, this,
                    [this, service](const QString& id, const StoreInstallProgress& progress) {
                if (currentService() == service) {
                    showProgress(id, progress);
                }
            });
            // The progress frame tracks whatever is downloading, not whatever
            // store happens to be on screen — so it is cleared before the
            // is-this-store-selected guard. Behind it, switching to another
            // store mid-install left the bar sitting there afterwards, still
            // showing an install that had finished.
            connect(service, &IStoreService::installFinished, this,
                    [this, service](const QString& id) {
                clearProgressFor(id);
                if (currentService() != service) {
                    return;   // its rows are rebuilt when it is selected again
                }
                // What is on disk just changed, so the badges and the buttons
                // are both stale.
                refreshInstalledState();
                rebuildEntryList();
                refreshDetails();
            });
            connect(service, &IStoreService::installFailed, this,
                    [this, service](const QString& id, const QString& reason) {
                clearProgressFor(id);
                if (currentService() != service) {
                    return;
                }
                refreshInstalledState();
                rebuildEntryList();
                refreshDetails();
                QMessageBox::warning(this, "Install failed", reason);
            });

            auto* item = new QListWidgetItem(service->displayName(), m_storeList);
            item->setData(RoleStoreIndex, m_services.size() - 1);
        }
    }

    if (m_storeList->count() > 0) {
        m_storeList->setCurrentRow(0);
    }
}

IStoreService* StoreLibraryDialog::currentService() const
{
    const QListWidgetItem* item = m_storeList->currentItem();
    if (!item) {
        return nullptr;
    }
    const int index = item->data(RoleStoreIndex).toInt();
    return index >= 0 && index < m_services.size() ? m_services.at(index) : nullptr;
}

void StoreLibraryDialog::refreshInstalledState()
{
    m_installed = {};

    IStoreService* service = currentService();
    if (!service) {
        return;
    }

    // Computed from what the launchers discovered rather than reported by the
    // service, so both stores answer it the same way. Cached because this is a
    // full discovery pass — running it per painted row, as the first cut did,
    // re-reads every appmanifest on the machine for each item in the list.
    const QString launcherName = service->launcherName();
    for (const Game& game : LauncherManager::instance().discoverAllGames()) {
        if (game.launcher() != launcherName) {
            continue;
        }
        m_installed.ids.insert(game.id());
        if (game.needsUpdate()) {
            m_installed.needUpdate.insert(game.id());
        }
    }
}

StoreEntry StoreLibraryDialog::entryById(const QString& id) const
{
    IStoreService* service = currentService();
    if (!service) {
        return {};
    }
    for (const StoreEntry& entry : m_entriesByLauncher.value(service->launcherName())) {
        if (entry.id == id) {
            return entry;
        }
    }
    return {};
}

void StoreLibraryDialog::onStoreSelected()
{
    clearDetails();
    refreshInstalledState();
    showStoreState();
}

void StoreLibraryDialog::showStoreState()
{
    IStoreService* service = currentService();
    if (!service) {
        return;
    }

    m_signOutButton->setVisible(service->canSignIn() && service->isAuthenticated());
    m_refreshButton->setEnabled(service->isAuthenticated());

    // Nothing to search until a library has actually arrived. Keyed on the
    // entries rather than on the visible page, so a filter that matches nothing
    // still leaves the box on screen — hiding it there would strand the user
    // with a filter they cannot clear.
    m_searchBox->setVisible(m_entriesByLauncher.contains(service->launcherName()));

    if (!service->isAuthenticated()) {
        // A store with no sign-in of its own says what to do instead; one with a
        // sign-in gets a button.
        m_middleMessage->setText(service->canSignIn()
            ? QStringLiteral("Sign in to see the games you own on %1.").arg(service->displayName())
            : service->authenticationHint());
        m_middleAction->setVisible(service->canSignIn());
        m_middleAction->setText("Sign in…");
        m_middleStack->setCurrentIndex(1);
        return;
    }

    if (m_entriesByLauncher.contains(service->launcherName())) {
        rebuildEntryList();
        return;
    }

    m_middleMessage->setText(QStringLiteral("Loading your %1 library…").arg(service->displayName()));
    m_middleAction->setVisible(false);
    m_middleStack->setCurrentIndex(1);
    m_libraryBusy->show();
    service->fetchLibrary();
}

void StoreLibraryDialog::rebuildEntryList()
{
    IStoreService* service = currentService();
    if (!service) {
        return;
    }

    const QList<StoreEntry> entries = m_entriesByLauncher.value(service->launcherName());
    const QString selected = m_entryList->currentItem()
                                 ? m_entryList->currentItem()->data(RoleEntryId).toString()
                                 : QString();
    m_searchBox->setVisible(true);

    m_entryList->clear();
    for (const StoreEntry& entry : entries) {
        if (!m_filterText.isEmpty()
            && !entry.title.contains(m_filterText, Qt::CaseInsensitive)) {
            continue;
        }

        const bool isInstalled = m_installed.ids.contains(entry.id);
        const bool isUpdatable = m_installed.needUpdate.contains(entry.id);

        QString label = entry.title;
        if (service->isInstalling(entry.id)) {
            label += "   ↓ installing";
        } else if (isUpdatable) {
            label += "   ↑ update available";
        } else if (isInstalled) {
            label += "   ✓ installed";
        }

        auto* item = new QListWidgetItem(label, m_entryList);
        item->setData(RoleEntryId, entry.id);
        if (isInstalled) {
            item->setForeground(QColor(isUpdatable ? "#f0a30a" : AppStyle::ColorAccent));
        }
        // Rebuilds happen on every install event, and losing the selection each
        // time would move the details panel out from under the user.
        if (!selected.isEmpty() && entry.id == selected) {
            m_entryList->setCurrentItem(item);
        }
    }

    if (m_entryList->count() == 0) {
        m_middleMessage->setText(entries.isEmpty()
            ? QStringLiteral("Nothing here yet.")
            : QStringLiteral("No game matches “%1”.").arg(m_filterText));
        m_middleAction->setVisible(false);
        m_middleStack->setCurrentIndex(1);
        return;
    }

    m_middleStack->setCurrentIndex(0);
}

void StoreLibraryDialog::onSearchChanged(const QString& text)
{
    m_filterText = text;
    rebuildEntryList();
}

void StoreLibraryDialog::onEntrySelected()
{
    IStoreService* service = currentService();
    const QListWidgetItem* item = m_entryList->currentItem();
    if (!service || !item) {
        clearDetails();
        return;
    }

    const QString id = item->data(RoleEntryId).toString();
    for (const StoreEntry& entry : m_entriesByLauncher.value(service->launcherName())) {
        if (entry.id == id) {
            showDetails(entry);
            return;
        }
    }
    clearDetails();
}

void StoreLibraryDialog::showDetails(const StoreEntry& entry)
{
    IStoreService* service = currentService();
    if (!service) {
        return;
    }

    m_detailTitle->setText(entry.title);

    if (!entry.imageUrl.isEmpty()) {
        const QPixmap art = ImageCache::instance().getImage(entry.imageUrl, QSize(230, 107));
        m_detailImage->setPixmap(art);
    } else {
        m_detailImage->clear();
    }

    const bool installed = m_installed.ids.contains(entry.id);
    const bool updatable = m_installed.needUpdate.contains(entry.id);
    const bool installing = service->isInstalling(entry.id);

    QStringList lines;
    lines << QStringLiteral("<b>ID</b>: %1").arg(entry.id);
    if (entry.supportsWindows || entry.supportsLinux) {
        QStringList platforms;
        if (entry.supportsWindows) platforms << "Windows";
        if (entry.supportsLinux) platforms << "Linux";
        lines << QStringLiteral("<b>Platforms</b>: %1").arg(platforms.join(", "));
    }

    // Where it lives and how big it is — the two questions asked before
    // installing anything, and the two nobody wants to go hunting for.
    const Game game = installed ? gameFor(entry.id) : Game();
    if (installed) {
        if (!game.installPath().isEmpty()) {
            lines << QStringLiteral("<b>Installed at</b>: %1").arg(game.installPath());
        }
        if (!game.version().isEmpty()) {
            lines << QStringLiteral("<b>Version</b>: %1").arg(game.version());
        }
        if (game.sizeOnDisk() > 0) {
            lines << QStringLiteral("<b>Size</b>: %1 GB")
                         .arg(game.sizeOnDisk() / 1073741824.0, 0, 'f', 1);
        }
        lines << (updatable
                      ? QStringLiteral("<span style='color:#f0a30a'>An update is available.</span>")
                      : QStringLiteral("<span style='color:#76B900'>Installed.</span>"));

        // Kept with the install rather than shown once while it ran: "this game
        // wants the 2019 C++ runtime" is the answer to a question asked days
        // later, when it will not start.
        for (const QString& warning : game.installWarnings()) {
            lines << QStringLiteral("<span style='color:#f0a30a'>%1</span>").arg(warning.toHtmlEscaped());
        }
    }

    m_detailBody->setText(lines.join("<br>"));
    m_storePageButton->setEnabled(!entry.storeUrl.isEmpty());

    // The install button carries four different jobs, and each needs its own
    // word: stop what is running, fetch an update, hand off to another client,
    // or install here.
    m_uninstallButton->setVisible(installed && service->canInstall() && !installing);
    m_installButton->setEnabled(true);

    if (installing) {
        m_installButton->setText("Cancel");
    } else if (updatable && service->canInstall()) {
        m_installButton->setText("Update");
    } else if (installed) {
        m_installButton->setEnabled(false);
        m_installButton->setText("Installed");
    } else if (!entry.installUrl.isEmpty()) {
        m_installButton->setText(QStringLiteral("Install via %1").arg(service->displayName()));
    } else if (service->canInstall() && entry.installable) {
        m_installButton->setText("Install");
    } else {
        m_installButton->setEnabled(false);
        m_installButton->setText("Not installable by ProtonForge");
    }
}

Game StoreLibraryDialog::gameFor(const QString& id) const
{
    IStoreService* service = currentService();
    if (!service) {
        return {};
    }
    for (const Game& game : LauncherManager::instance().discoverAllGames()) {
        if (game.launcher() == service->launcherName() && game.id() == id) {
            return game;
        }
    }
    return {};
}

void StoreLibraryDialog::refreshDetails()
{
    const QListWidgetItem* item = m_entryList->currentItem();
    if (!item) {
        clearDetails();
        return;
    }
    const StoreEntry entry = entryById(item->data(RoleEntryId).toString());
    if (entry.id.isEmpty()) {
        clearDetails();
        return;
    }
    showDetails(entry);
}

void StoreLibraryDialog::clearProgressFor(const QString& id)
{
    if (m_installingId != id) {
        return;
    }
    m_installingId.clear();
    m_progressFrame->hide();
}

void StoreLibraryDialog::showProgress(const QString& id, const StoreInstallProgress& progress)
{
    m_installingId = id;
    m_installPaused = progress.paused;
    m_progressFrame->show();
    m_pauseButton->setText(progress.paused ? "Resume" : "Pause");

    const StoreEntry entry = entryById(id);
    const QString title = entry.title.isEmpty() ? id : entry.title;
    m_progressLabel->setText(progress.detail.isEmpty()
                                 ? title
                                 : QStringLiteral("%1 — %2").arg(title, progress.detail));

    // A busy bar until there is a total to divide by, rather than a bar sitting
    // at zero while several hundred megabytes of manifests are read.
    if (progress.bytesTotal > 0) {
        m_progress->setRange(0, 1000);
        m_progress->setValue(static_cast<int>(progress.bytesDone * 1000 / progress.bytesTotal));
    } else {
        m_progress->setRange(0, 0);
    }
}

void StoreLibraryDialog::clearDetails()
{
    m_detailImage->clear();
    m_detailTitle->clear();
    m_detailBody->clear();
    m_storePageButton->setEnabled(false);
    m_installButton->setEnabled(false);
    m_installButton->setText("Install");
    m_uninstallButton->hide();
}

void StoreLibraryDialog::onSignInClicked()
{
    if (IStoreService* service = currentService()) {
        service->beginSignIn(this);
        showStoreState();
    }
}

void StoreLibraryDialog::onSignOutClicked()
{
    if (IStoreService* service = currentService()) {
        service->signOut();
        showStoreState();
    }
}

void StoreLibraryDialog::onRefreshClicked()
{
    IStoreService* service = currentService();
    if (!service || !service->isAuthenticated()) {
        return;
    }
    m_entriesByLauncher.remove(service->launcherName());
    refreshInstalledState();
    showStoreState();
}

void StoreLibraryDialog::onInstallClicked()
{
    IStoreService* service = currentService();
    const QListWidgetItem* item = m_entryList->currentItem();
    if (!service || !item) {
        return;
    }

    const QString id = item->data(RoleEntryId).toString();
    const StoreEntry entry = entryById(id);
    if (entry.id.isEmpty()) {
        return;
    }

    // Same button, four jobs — see showDetails.
    if (service->isInstalling(id)) {
        QMessageBox confirm(this);
        confirm.setWindowTitle("Stop installing");
        confirm.setText(QStringLiteral("Stop installing %1?").arg(entry.title));
        confirm.setInformativeText(
            "What has been downloaded so far is kept, and installing again resumes from there.");
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::No);

        // Offered rather than assumed: a half-downloaded 60 GB game is worth
        // keeping by default, and worth being able to reclaim on demand.
        auto* discard = new QCheckBox("Delete what has been downloaded", &confirm);
        confirm.setCheckBox(discard);

        if (confirm.exec() == QMessageBox::Yes) {
            service->cancelInstall(id, discard->isChecked());
        }
        return;
    }

    if (!entry.installUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(entry.installUrl));
        return;
    }
    if (!service->canInstall()) {
        return;
    }

    service->install(id);
    rebuildEntryList();
    refreshDetails();
}

void StoreLibraryDialog::onPauseClicked()
{
    IStoreService* service = currentService();
    if (!service || m_installingId.isEmpty()) {
        return;
    }
    if (m_installPaused) {
        service->resumeInstall(m_installingId);
    } else {
        service->pauseInstall(m_installingId);
    }
}

void StoreLibraryDialog::onUninstallClicked()
{
    IStoreService* service = currentService();
    const QListWidgetItem* item = m_entryList->currentItem();
    if (!service || !item) {
        return;
    }

    const QString id = item->data(RoleEntryId).toString();
    const StoreEntry entry = entryById(id);
    const Game game = gameFor(id);

    // Naming the directory in the question, because this deletes it.
    const auto answer = QMessageBox::question(
        this, "Uninstall",
        QStringLiteral("Delete %1 and its Proton prefix?\n\n%2\n\nSaved games kept inside the "
                       "prefix go with it.")
            .arg(entry.title.isEmpty() ? id : entry.title,
                 game.installPath().isEmpty() ? QStringLiteral("(install directory unknown)")
                                              : game.installPath()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    service->uninstall(id);
}
