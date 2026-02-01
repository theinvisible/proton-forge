#include "SettingsManager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

SettingsManager& SettingsManager::instance()
{
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager()
{
    load();
}

QString SettingsManager::configDir()
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return configPath + "/NvidiaAppLinux";
}

QString SettingsManager::configFilePath()
{
    return configDir() + "/settings.json";
}

DLSSSettings SettingsManager::getSettings(const QString& gameKey) const
{
    if (m_gameSettings.contains(gameKey)) {
        return m_gameSettings[gameKey];
    }
    return m_defaultSettings;
}

void SettingsManager::setSettings(const QString& gameKey, const DLSSSettings& settings)
{
    m_gameSettings[gameKey] = settings;
    save();
    emit settingsChanged(gameKey);
}

bool SettingsManager::hasSettings(const QString& gameKey) const
{
    return m_gameSettings.contains(gameKey);
}

void SettingsManager::removeSettings(const QString& gameKey)
{
    m_gameSettings.remove(gameKey);
    save();
    emit settingsChanged(gameKey);
}

DLSSSettings SettingsManager::defaultSettings() const
{
    return m_defaultSettings;
}

void SettingsManager::setDefaultSettings(const DLSSSettings& settings)
{
    m_defaultSettings = settings;
    save();
}

void SettingsManager::save()
{
    QDir dir;
    dir.mkpath(configDir());

    QJsonObject root;

    // Save default settings
    root["defaults"] = m_defaultSettings.toJson();

    // Save per-game settings
    QJsonObject gamesObj;
    for (auto it = m_gameSettings.constBegin(); it != m_gameSettings.constEnd(); ++it) {
        gamesObj[it.key()] = it.value().toJson();
    }
    root["games"] = gamesObj;

    QJsonDocument doc(root);
    QFile file(configFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void SettingsManager::load()
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();

    // Load default settings
    if (root.contains("defaults")) {
        m_defaultSettings = DLSSSettings::fromJson(root["defaults"].toObject());
    }

    // Load per-game settings
    if (root.contains("games")) {
        QJsonObject gamesObj = root["games"].toObject();
        for (auto it = gamesObj.constBegin(); it != gamesObj.constEnd(); ++it) {
            m_gameSettings[it.key()] = DLSSSettings::fromJson(it.value().toObject());
        }
    }
}
