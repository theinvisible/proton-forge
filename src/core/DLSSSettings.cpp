#include "DLSSSettings.h"

QJsonObject DLSSSettings::toJson() const
{
    QJsonObject json;

    // General
    json["enableNVAPI"] = enableNVAPI;
    json["enableNGXUpdater"] = enableNGXUpdater;

    // Super Resolution
    json["srOverride"] = srOverride;
    json["srMode"] = srMode;
    json["srPreset"] = srPreset;
    json["srScalingRatio"] = srScalingRatio;

    // Ray Reconstruction
    json["rrOverride"] = rrOverride;
    json["rrMode"] = rrMode;
    json["rrPreset"] = rrPreset;
    json["rrScalingRatio"] = rrScalingRatio;

    // Frame Generation
    json["fgOverride"] = fgOverride;
    json["fgMultiFrameCount"] = fgMultiFrameCount;

    // DLSS Upgrade
    json["dlssUpgrade"] = dlssUpgrade;
    json["dlssVersion"] = dlssVersion;

    // Indicators
    json["showIndicator"] = showIndicator;

    // Smooth Motion
    json["enableSmoothMotion"] = enableSmoothMotion;
    json["enableFrameRateLimit"] = enableFrameRateLimit;
    json["targetFrameRate"] = targetFrameRate;

    // Executable Selection
    if (!executablePath.isEmpty()) {
        json["executablePath"] = executablePath;
    }

    return json;
}

DLSSSettings DLSSSettings::fromJson(const QJsonObject& json)
{
    DLSSSettings settings;

    // General
    settings.enableNVAPI = json["enableNVAPI"].toBool(true);
    settings.enableNGXUpdater = json["enableNGXUpdater"].toBool(false);

    // Super Resolution
    settings.srOverride = json["srOverride"].toBool(false);
    settings.srMode = json["srMode"].toString();
    settings.srPreset = json["srPreset"].toString();
    settings.srScalingRatio = json["srScalingRatio"].toInt(0);

    // Ray Reconstruction
    settings.rrOverride = json["rrOverride"].toBool(false);
    settings.rrMode = json["rrMode"].toString();
    settings.rrPreset = json["rrPreset"].toString();
    settings.rrScalingRatio = json["rrScalingRatio"].toInt(0);

    // Frame Generation
    settings.fgOverride = json["fgOverride"].toBool(false);
    settings.fgMultiFrameCount = json["fgMultiFrameCount"].toInt(0);

    // DLSS Upgrade
    settings.dlssUpgrade = json["dlssUpgrade"].toBool(false);
    settings.dlssVersion = json["dlssVersion"].toString();

    // Indicators
    settings.showIndicator = json["showIndicator"].toBool(false);

    // Smooth Motion
    settings.enableSmoothMotion = json["enableSmoothMotion"].toBool(false);
    settings.enableFrameRateLimit = json["enableFrameRateLimit"].toBool(false);
    settings.targetFrameRate = json["targetFrameRate"].toInt(60);

    // Executable Selection
    settings.executablePath = json["executablePath"].toString();

    return settings;
}

QStringList DLSSSettings::availableSRModes()
{
    return {
        "",  // Default/App controlled
        "PERFORMANCE",
        "BALANCED",
        "QUALITY",
        "DLAA",
        "ULTRA_PERFORMANCE",
        "CUSTOM"
    };
}

QStringList DLSSSettings::availableRRModes()
{
    return {
        "",  // Default/App controlled
        "PERFORMANCE",
        "BALANCED",
        "QUALITY",
        "DLAA",
        "ULTRA_PERFORMANCE"
    };
}

QStringList DLSSSettings::availablePresets()
{
    return {
        "",  // Default/App controlled
        "RENDER_PRESET_A",
        "RENDER_PRESET_B",
        "RENDER_PRESET_C",
        "RENDER_PRESET_D",
        "RENDER_PRESET_E",
        "RENDER_PRESET_F",
        "RENDER_PRESET_G",
        "RENDER_PRESET_H",
        "RENDER_PRESET_I",
        "RENDER_PRESET_J",
        "RENDER_PRESET_K",
        "RENDER_PRESET_L",
        "RENDER_PRESET_M",
        "RENDER_PRESET_N",
        "RENDER_PRESET_O",
        "RENDER_PRESET_LATEST"
    };
}

bool DLSSSettings::operator==(const DLSSSettings& other) const
{
    return enableNVAPI == other.enableNVAPI &&
           enableNGXUpdater == other.enableNGXUpdater &&
           srOverride == other.srOverride &&
           srMode == other.srMode &&
           srPreset == other.srPreset &&
           srScalingRatio == other.srScalingRatio &&
           rrOverride == other.rrOverride &&
           rrMode == other.rrMode &&
           rrPreset == other.rrPreset &&
           rrScalingRatio == other.rrScalingRatio &&
           fgOverride == other.fgOverride &&
           fgMultiFrameCount == other.fgMultiFrameCount &&
           dlssUpgrade == other.dlssUpgrade &&
           dlssVersion == other.dlssVersion &&
           showIndicator == other.showIndicator &&
           enableSmoothMotion == other.enableSmoothMotion &&
           enableFrameRateLimit == other.enableFrameRateLimit &&
           targetFrameRate == other.targetFrameRate;
}
