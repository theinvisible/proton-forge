#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

#include "core/SecretStore.h"

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onCategoryChanged();
    void saveSettings();
    void onSecretWriteFailed(SecretStore::Key key, const QString& reason);

private:
    void setupUI();
    void loadSettings();
    // The credential fields, filled only once SecretStore has loaded — before
    // that they would read as empty, and saving empty is a delete.
    void loadSecrets();
    QWidget* buildGithubPage();
    QWidget* buildSteamPage();
    QWidget* buildGogPage();

    QListWidget*    m_categoryList;
    QStackedWidget* m_stack;
    QLineEdit*      m_tokenEdit;
    QPushButton*    m_toggleTokenBtn;
    QLineEdit*      m_steamApiKeyEdit = nullptr;
    QLineEdit*      m_steamIdEdit = nullptr;
    QLineEdit*      m_gogInstallRootEdit = nullptr;
    QComboBox*      m_gogLanguageBox = nullptr;
    QPushButton*    m_saveButton = nullptr;

    // What the fields held when loaded, so Save writes only what the user changed.
    QString         m_loadedGitHubToken;
    QString         m_loadedSteamApiKey;
};

#endif // SETTINGSDIALOG_H
