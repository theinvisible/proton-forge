#ifndef PROTONVERSIONDIALOG_H
#define PROTONVERSIONDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QTextBrowser>
#include <QSplitter>
#include <QProgressBar>
#include <QFrame>
#include <QPainter>
#include "utils/ProtonManager.h"

class ProtonVersionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProtonVersionDialog(const QList<ProtonManager::ProtonRelease>& releases,
                                 const QString& currentVersion,
                                 QWidget* parent = nullptr);

    ProtonManager::ProtonRelease selectedRelease() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void startInstallation();
    void onDownloadProgress(qint64 received, qint64 total, const QString& name);
    void onExtractionStarted();
    void onInstallationComplete(bool success, const QString& message);

private:
    void setupUI();
    void populateVariants();
    void onVariantSelected();
    void updateVersionList();
    void updateChangelog();
    void deleteSelectedVersion();
    void updateButtonStates();
    QStringList getInstalledVersions() const;
    bool isVersionInstalled(const ProtonManager::ProtonRelease& release) const;

    static QIcon makeVariantIcon(const QColor& color, const QString& letter);

    QList<ProtonManager::ProtonRelease> m_releases;
    QString m_currentVersion;
    QStringList m_installedVersions;
    ProtonManager::ProtonType m_selectedType = ProtonManager::ProtonCachyOS;
    bool m_installing = false;

    QListWidget*  m_variantList;
    QListWidget*  m_versionList;
    QTextBrowser* m_changelogView;
    QPushButton*  m_installButton;
    QPushButton*  m_deleteButton;
    QPushButton*  m_cancelButton;

    // Progress panel
    QFrame*       m_progressFrame;
    QLabel*       m_progressPhaseLabel;
    QLabel*       m_progressDetailLabel;
    QProgressBar* m_progressBar;
};

#endif // PROTONVERSIONDIALOG_H
