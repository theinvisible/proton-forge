#ifndef ENVBUILDER_H
#define ENVBUILDER_H

#include <QString>
#include <QProcessEnvironment>
#include "core/DLSSSettings.h"

class EnvBuilder {
public:
    // Build launch options string for Steam (e.g., "PROTON_ENABLE_NVAPI=1 %command%")
    static QString buildLaunchOptions(const DLSSSettings& settings);

    // Build environment variables for direct process launch
    static QProcessEnvironment buildEnvironment(const DLSSSettings& settings);

    // Build DXVK_NVAPI_DRS_SETTINGS value
    static QString buildDRSSettings(const DLSSSettings& settings);

private:
    static void addEnvVar(QStringList& vars, const QString& key, const QString& value);
    static void addEnvVar(QStringList& vars, const QString& key, int value);
    static void addEnvVar(QStringList& vars, const QString& key, bool value);
};

#endif // ENVBUILDER_H
