#ifndef STORELIBRARYDIALOG_H
#define STORELIBRARYDIALOG_H

#include <QDialog>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>

#include "core/Game.h"
#include "launchers/IStoreService.h"

// Browsing what you own, across every store ProtonForge can talk to.
//
// Three panels, matching the Proton-Manager: pick a store on the left, its games
// in the middle, the selected game's details on the right. Nothing in here names
// a store — it iterates the launchers that expose an IStoreService, so a new
// adapter appears with no change to this file.
class StoreLibraryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StoreLibraryDialog(QWidget* parent = nullptr);

private slots:
    void onStoreSelected();
    void onEntrySelected();
    void onSearchChanged(const QString& text);
    void onSignInClicked();
    void onSignOutClicked();
    void onInstallClicked();
    void onPauseClicked();
    void onUninstallClicked();
    void onRefreshClicked();

private:
    // What the launchers found on disk for one store. Held rather than
    // recomputed per row: it comes from a full discovery pass, which is far too
    // expensive to run once per painted item.
    struct InstalledState {
        QSet<QString> ids;
        QSet<QString> needUpdate;
    };

    void setupUI();
    void populateStores();
    void showStoreState();
    void refreshInstalledState();
    void rebuildEntryList();
    void showDetails(const StoreEntry& entry);
    void clearDetails();
    void refreshDetails();
    void showProgress(const QString& id, const StoreInstallProgress& progress);

    IStoreService* currentService() const;
    StoreEntry entryById(const QString& id) const;
    Game gameFor(const QString& id) const;

    QListWidget*    m_storeList;
    QStackedWidget* m_middleStack;   // 0 = games, 1 = a message
    QListWidget*    m_entryList;
    QLineEdit*      m_searchBox;
    QLabel*         m_middleMessage;
    QPushButton*    m_middleAction;  // "Sign in…" / "Open Settings…"

    QLabel*      m_detailImage;
    QLabel*      m_detailTitle;
    QLabel*      m_detailBody;
    QPushButton* m_storePageButton;
    QPushButton* m_installButton;
    QPushButton* m_uninstallButton;

    QFrame*       m_progressFrame;
    QLabel*       m_progressLabel;
    QPushButton*  m_pauseButton;
    QProgressBar* m_progress;
    QProgressBar* m_libraryBusy;
    QPushButton*  m_refreshButton;
    QPushButton*  m_signOutButton;

    QList<IStoreService*> m_services;
    QHash<QString, QList<StoreEntry>> m_entriesByLauncher;
    InstalledState m_installed;
    QString m_installingId;
    bool m_installPaused = false;
    QString m_filterText;
};

#endif // STORELIBRARYDIALOG_H
