#include "EnvBuilder.h"

void EnvBuilder::addEnvVar(QStringList& vars, const QString& key, const QString& value)
{
    if (!value.isEmpty()) {
        vars << QString("%1=%2").arg(key, value);
    }
}

void EnvBuilder::addEnvVar(QStringList& vars, const QString& key, int value)
{
    vars << QString("%1=%2").arg(key).arg(value);
}

void EnvBuilder::addEnvVar(QStringList& vars, const QString& key, bool value)
{
    vars << QString("%1=%2").arg(key).arg(value ? "1" : "0");
}

QString EnvBuilder::buildDRSSettings(const DLSSSettings& settings)
{
    QStringList drsSettings;

    // Super Resolution settings
    if (settings.srOverride) {
        drsSettings << "NGX_DLSS_SR_OVERRIDE=on";

        if (!settings.srMode.isEmpty()) {
            drsSettings << QString("NGX_DLSS_SR_MODE=%1").arg(settings.srMode.toLower());
        }

        if (!settings.srPreset.isEmpty()) {
            drsSettings << QString("NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION=%1").arg(settings.srPreset);
        }

        if (settings.srScalingRatio > 0 && settings.srScalingRatio <= 100) {
            drsSettings << QString("NGX_DLSS_SR_OVERRIDE_SCALING_RATIO=%1").arg(settings.srScalingRatio);
        }
    }

    // Ray Reconstruction settings
    if (settings.rrOverride) {
        drsSettings << "NGX_DLSS_RR_OVERRIDE=on";

        if (!settings.rrMode.isEmpty()) {
            drsSettings << QString("NGX_DLSS_RR_MODE=%1").arg(settings.rrMode.toLower());
        }

        if (!settings.rrPreset.isEmpty()) {
            drsSettings << QString("NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION=%1").arg(settings.rrPreset);
        }

        if (settings.rrScalingRatio > 0 && settings.rrScalingRatio <= 100) {
            drsSettings << QString("NGX_DLSS_RR_OVERRIDE_SCALING_RATIO=%1").arg(settings.rrScalingRatio);
        }
    }

    // Frame Generation settings
    if (settings.fgOverride) {
        drsSettings << "NGX_DLSS_FG_OVERRIDE=on";

        if (settings.fgMultiFrameCount > 0) {
            drsSettings << QString("NGX_DLSSG_MULTI_FRAME_COUNT=%1").arg(settings.fgMultiFrameCount);
        }
    }

    return drsSettings.join(",");
}

QString EnvBuilder::buildLaunchOptions(const DLSSSettings& settings)
{
    QStringList envVars;

    // General settings
    if (settings.enableNVAPI) {
        envVars << "PROTON_ENABLE_NVAPI=1";
    }

    if (settings.enableNGXUpdater) {
        envVars << "PROTON_ENABLE_NGX_UPDATER=1";
    }

    // DLSS Upgrade
    if (settings.dlssUpgrade) {
        envVars << "PROTON_DLSS_UPGRADE=1";
    }

    // Indicator
    if (settings.showIndicator) {
        envVars << "PROTON_DLSS_INDICATOR=1";
    }

    // DRS Settings (all DLSS overrides go in this single variable)
    QString drsSettings = buildDRSSettings(settings);
    if (!drsSettings.isEmpty()) {
        envVars << QString("DXVK_NVAPI_DRS_SETTINGS=%1").arg(drsSettings);
    }

    // Smooth Motion
    if (settings.enableSmoothMotion) {
        envVars << "NVPRESENT_ENABLE_SMOOTH_MOTION=1";
    }

    // Frame Rate Limit
    if (settings.enableFrameRateLimit && settings.targetFrameRate > 0) {
        envVars << QString("DXVK_FRAME_RATE=%1").arg(settings.targetFrameRate);
    }

    // Append %command% for Steam launch options
    envVars << "%command%";

    return envVars.join(" ");
}

QProcessEnvironment EnvBuilder::buildEnvironment(const DLSSSettings& settings)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // General settings
    if (settings.enableNVAPI) {
        env.insert("PROTON_ENABLE_NVAPI", "1");
    }

    if (settings.enableNGXUpdater) {
        env.insert("PROTON_ENABLE_NGX_UPDATER", "1");
    }

    // DLSS Upgrade
    if (settings.dlssUpgrade) {
        env.insert("PROTON_DLSS_UPGRADE", "1");
    }

    // Indicator
    if (settings.showIndicator) {
        env.insert("PROTON_DLSS_INDICATOR", "1");
    }

    // DRS Settings
    QString drsSettings = buildDRSSettings(settings);
    if (!drsSettings.isEmpty()) {
        env.insert("DXVK_NVAPI_DRS_SETTINGS", drsSettings);
    }

    // Smooth Motion
    if (settings.enableSmoothMotion) {
        env.insert("__GL_YIELD", "USLEEP");
    }

    // Frame Rate Limit
    if (settings.enableFrameRateLimit && settings.targetFrameRate > 0) {
        env.insert("DXVK_FRAME_RATE", QString::number(settings.targetFrameRate));
    }

    return env;
}
