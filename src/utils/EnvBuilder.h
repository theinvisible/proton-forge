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

    // Whether a variable ProtonForge emits can reach a *native* Linux game.
    //
    //   ProtonOnly — read by the Proton script, Wine, DXVK, vkd3d-proton or
    //                DXVK-NVAPI. A native ELF binary loads none of those, so
    //                the variable is inert no matter what it is set to. This
    //                is where all the DLSS overrides live: they travel in
    //                DXVK_NVAPI_DRS_SETTINGS, which only DXVK-NVAPI reads, and
    //                NVIDIA state outright that overriding presets is not
    //                supported on Linux outside Proton — there is no NVAPI and
    //                hence no driver-settings layer for a native title.
    //   Any        — read by the NVIDIA driver, a Vulkan implicit layer or a
    //                wrapper, so it reaches the process whether or not Proton
    //                is in the picture.
    //
    // This is the one place that answers the question; the UI greys its
    // controls out from it (DLSSSettingsWidget::applyPlatformGating). An
    // unknown key is reported as Any — the lenient answer, matching
    // FeatureGate's policy of never warning about something we do not know.
    enum class VarScope { Any, ProtonOnly };
    static VarScope scopeOf(const QString& key);

    // Every variable buildLaunchOptions()/buildEnvironment() may emit, i.e.
    // exactly the keys scopeOf() classifies. Exists so a test can assert the
    // two never drift apart when a new option is added.
    static QStringList managedVars();

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

    // The mirror image: tokens *before* "%command%" that are not KEY=VALUE, i.e.
    // a wrapper command and its arguments ("gamemoderun", "strangle 60", …).
    // Steam runs these; a direct launch has to prepend them itself or the user's
    // wrapper silently does nothing. Empty when there is no "%command%" — by
    // convention the custom params are then env vars only.
    static QStringList customWrapper(const DLSSSettings& settings);

private:
    static void addEnvVar(QStringList& vars, const QString& key, const QString& value);
    static void addEnvVar(QStringList& vars, const QString& key, int value);
    static void addEnvVar(QStringList& vars, const QString& key, bool value);
};

#endif // ENVBUILDER_H
