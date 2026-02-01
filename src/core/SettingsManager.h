#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include "DLSSSettings.h"

class SettingsManager : public QObject {
    Q_OBJECT

public:
    static SettingsManager& instance();

    // Per-game settings
    DLSSSettings getSettings(const QString& gameKey) const;
    void setSettings(const QString& gameKey, const DLSSSettings& settings);
    bool hasSettings(const QString& gameKey) const;
    void removeSettings(const QString& gameKey);

    // Default settings
    DLSSSettings defaultSettings() const;
    void setDefaultSettings(const DLSSSettings& settings);

    // Persistence
    void save();
    void load();

    // Config paths
    static QString configDir();
    static QString configFilePath();

signals:
    void settingsChanged(const QString& gameKey);

private:
    SettingsManager();
    ~SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    QMap<QString, DLSSSettings> m_gameSettings;
    DLSSSettings m_defaultSettings;
};

#endif // SETTINGSMANAGER_H
