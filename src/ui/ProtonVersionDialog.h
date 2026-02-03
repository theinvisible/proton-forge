#ifndef PROTONVERSIONDIALOG_H
#define PROTONVERSIONDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include "utils/ProtonManager.h"

class ProtonVersionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProtonVersionDialog(const QList<ProtonManager::ProtonRelease>& releases,
                                 const QString& currentVersion,
                                 QWidget* parent = nullptr);

    ProtonManager::ProtonRelease selectedRelease() const;

private:
    void setupUI();
    void populateList();
    void addReleaseItem(const ProtonManager::ProtonRelease& release, bool isLatest);
    QStringList getInstalledVersions() const;
    bool isVersionInstalled(const ProtonManager::ProtonRelease& release) const;

    QList<ProtonManager::ProtonRelease> m_releases;
    QString m_currentVersion;
    QStringList m_installedVersions;

    QLabel* m_headerLabel;
    QListWidget* m_versionList;
    QPushButton* m_installButton;
    QPushButton* m_cancelButton;
};

#endif // PROTONVERSIONDIALOG_H
