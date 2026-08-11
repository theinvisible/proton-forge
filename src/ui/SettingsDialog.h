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
};

#endif // SETTINGSDIALOG_H
