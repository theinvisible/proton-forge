#ifndef ENVBUILDER_H
#define ENVBUILDER_H

#include <QString>
#include <QStringList>
#include <QProcessEnvironment>
#include "core/DLSSSettings.h"

class EnvBuilder {
public:
    // Result of reverse-parsing a raw Steam launch-options string.
    struct ParsedLaunchOptions {
        DLSSSettings settings;   // recognised options mapped onto fields
        QString customParams;    // leftover (unknown env vars, %command% + trailing args)
    };

    // Build launch options string for Steam (e.g., "PROTON_ENABLE_NVAPI=1 %command%")
    static QString buildLaunchOptions(const DLSSSettings& settings);

    // Build environment variables for direct process launch
    static QProcessEnvironment buildEnvironment(const DLSSSettings& settings);

    // Build DXVK_NVAPI_DRS_SETTINGS value
    static QString buildDRSSettings(const DLSSSettings& settings);

    // Inverse of buildLaunchOptions: map a raw Steam launch-options string back
    // onto DLSSSettings. Recognised KEY=VALUE env vars set the matching field;
    // the "%command%" token and everything after it, plus any unrecognised
    // token, are collected into customParams so the string round-trips. Fields
    // that are never emitted to launch options (executablePath, protonVersion,
    // dlssVersion, enableSteamOverlay) are preserved from `base`.
    static ParsedLaunchOptions parseLaunchOptions(const QString& raw, const DLSSSettings& base);

    // Extra game arguments from customLaunchParams (tokens after "%command%"),
    // for passing to the game process on a direct launch.
    static QStringList customGameArgs(const DLSSSettings& settings);

private:
    static void addEnvVar(QStringList& vars, const QString& key, const QString& value);
    static void addEnvVar(QStringList& vars, const QString& key, int value);
    static void addEnvVar(QStringList& vars, const QString& key, bool value);
};

#endif // ENVBUILDER_H
