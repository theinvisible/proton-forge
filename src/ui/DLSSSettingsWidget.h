#ifndef DLSSSETTINGSWIDGET_H
#define DLSSSETTINGSWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFutureWatcher>
#include <atomic>
#include <memory>
#include "core/Game.h"
#include "core/DLSSSettings.h"
#include "utils/CompatToolResolver.h"
#include "network/ProtonDBClient.h"

class SettingsPage;
class AdvancedPage;

class DLSSSettingsWidget : public QWidget {
    Q_OBJECT

public:
    explicit DLSSSettingsWidget(QWidget* parent = nullptr);

    void setGame(const Game& game);
    void setSettings(const DLSSSettings& settings);
    DLSSSettings settings() const;

    void updateLaunchCommand(const QString& command);
    void setGameRunning(bool running);
    // A different game is running; Play stays disabled until it exits.
    void setOtherGameRunning(const QString& gameName);
    // Launch accepted but still waiting for the Steam client to come up.
    void setLaunchPending(bool pending);
    void updateGameStatus(const Game& game);

signals:
    void settingsChanged(const DLSSSettings& settings);
    void playClicked();
    void copyClicked();
    void writeToLauncherClicked();
    void importFromLauncherClicked();

private slots:
    void onSettingChanged();
    void onRecommendClicked();

private:
    void setupUI();
    QWidget* createActionsSection();
    // Wraps a page in a scroll area to form one tab.
    QWidget* createScrollTab(SettingsPage* page);

    void updateProtonDbBadge(const ProtonDBClient::Summary& summary);

    void styleComboBoxPopups();
    void updateFeatureWarnings();
    void populateExecutableSelector(const Game& game);
    void updateExecutableSelectorWithResults(const QStringList& executables);
    void populateProtonVersionSelector();
    void refreshResolvedProton();

    // Header
    QLabel* m_gameNameLabel;
    QLabel* m_gameImageLabel;
    QLabel* m_platformBadge;
    QPushButton* m_protonDbBadge;  // clickable ProtonDB tier badge in the header
    QLabel* m_updateAvailableLabel;
    QWidget* m_protonSelectorContainer;
    QComboBox* m_protonVersionSelector;
    QComboBox* m_executableSelector;

    // Feature gating warnings (driver/Proton requirements not met)
    QLabel* m_featureWarnings;
    QLabel* m_resolvedProtonLabel;

    // The tabs, in order. Each shows part of DLSSSettings; together they show
    // all of it that has a control.
    QList<SettingsPage*> m_pages;
    AdvancedPage* m_advancedPage;   // also takes ProtonDB suggestions
    // What setSettings() was given — settings() starts from it, so fields
    // without a control survive an edit.
    DLSSSettings m_loaded;

    // ProtonDB recommendations
    ProtonDBClient::Summary m_protonDbSummary;
    QString m_protonDbSummaryAppId;

    // Launch command preview
    QTextEdit* m_launchCommandPreview;

    // Action buttons
    QPushButton* m_playButton;
    QPushButton* m_copyButton;
    QPushButton* m_writeToSteamButton;
    QPushButton* m_importButton;

    Game m_currentGame;
    QFutureWatcher<QStringList>* m_executableWatcher;
    QString m_savedExecutablePath;  // Store saved selection during async search
    // The Proton a launch would use for the current game and selection.
    CompatToolResolver::ToolInfo m_resolvedProton;
    bool m_loading = false;         // setSettings() is writing the widgets
    unsigned int m_searchGeneration = 0;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
};

#endif // DLSSSETTINGSWIDGET_H
