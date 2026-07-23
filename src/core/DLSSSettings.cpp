#include "DLSSSettings.h"

QJsonObject DLSSSettings::toJson() const
{
    QJsonObject json;

    // General
    json["enableNVAPI"] = enableNVAPI;
    json["enableNGXUpdater"] = enableNGXUpdater;
    json["enableReflex"] = enableReflex;
    json["enableVkd3dLowLatency"] = enableVkd3dLowLatency;
    json["enablePrimeRenderOffload"] = enablePrimeRenderOffload;
    json["enableVkd3dDescriptorHeap"] = enableVkd3dDescriptorHeap;
    if (!vkd3dConfigExtra.isEmpty()) {
        json["vkd3dConfigExtra"] = vkd3dConfigExtra;
    }

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
    json["fgMode"] = fgMode;
    json["fgPreset"] = fgPreset;

    // DLSS Upgrade
    json["dlssUpgrade"] = dlssUpgrade;
    json["dlssVersion"] = dlssVersion;

    // Indicators
    json["showIndicator"] = showIndicator;

    // Smooth Motion
    json["enableSmoothMotion"] = enableSmoothMotion;
    json["enableFrameRateLimit"] = enableFrameRateLimit;
    json["targetFrameRate"] = targetFrameRate;

    // HDR
    json["enableProtonWayland"] = enableProtonWayland;
    json["enableProtonHDR"] = enableProtonHDR;
    json["enableHDRWSI"] = enableHDRWSI;
    json["disableAutoHDR"] = disableAutoHDR;

    // Proton Tweaks
    json["protonPriorityHigh"] = protonPriorityHigh;
    json["protonUseNTSync"] = protonUseNTSync;
    json["protonUseD7VK"] = protonUseD7VK;
    json["protonLog"] = protonLog;

    // Overlay
    json["enableSteamOverlay"] = enableSteamOverlay;
    json["enableMangoHud"] = enableMangoHud;

    // Executable Selection
    if (!executablePath.isEmpty()) {
        json["executablePath"] = executablePath;
    }

    // Proton Version
    if (!protonVersion.isEmpty()) {
        json["protonVersion"] = protonVersion;
    }

    // Custom launch parameters
    if (!customLaunchParams.isEmpty()) {
        json["customLaunchParams"] = customLaunchParams;
    }

    return json;
}

DLSSSettings DLSSSettings::fromJson(const QJsonObject& json)
{
    DLSSSettings settings;

    // General
    settings.enableNVAPI = json["enableNVAPI"].toBool(true);
    settings.enableNGXUpdater = json["enableNGXUpdater"].toBool(false);
    settings.enableReflex = json["enableReflex"].toBool(false);
    settings.enableVkd3dLowLatency = json["enableVkd3dLowLatency"].toBool(false);
    settings.enablePrimeRenderOffload = json["enablePrimeRenderOffload"].toBool(false);
    settings.enableVkd3dDescriptorHeap = json["enableVkd3dDescriptorHeap"].toBool(false);
    settings.vkd3dConfigExtra = json["vkd3dConfigExtra"].toString();

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
    settings.fgMode = json["fgMode"].toString();
    settings.fgPreset = json["fgPreset"].toString();

    // DLSS Upgrade
    settings.dlssUpgrade = json["dlssUpgrade"].toBool(false);
    settings.dlssVersion = json["dlssVersion"].toString();

    // Indicators
    settings.showIndicator = json["showIndicator"].toBool(false);

    // Smooth Motion
    settings.enableSmoothMotion = json["enableSmoothMotion"].toBool(false);
    settings.enableFrameRateLimit = json["enableFrameRateLimit"].toBool(false);
    settings.targetFrameRate = json["targetFrameRate"].toInt(60);

    // HDR
    settings.enableProtonWayland = json["enableProtonWayland"].toBool(false);
    settings.enableProtonHDR = json["enableProtonHDR"].toBool(false);
    settings.enableHDRWSI = json["enableHDRWSI"].toBool(false);
    settings.disableAutoHDR = json["disableAutoHDR"].toBool(false);

    // Proton Tweaks
    settings.protonPriorityHigh = json["protonPriorityHigh"].toBool(false);
    settings.protonUseNTSync = json["protonUseNTSync"].toBool(false);
    settings.protonUseD7VK = json["protonUseD7VK"].toBool(false);
    settings.protonLog = json["protonLog"].toBool(false);

    // Overlay
    settings.enableSteamOverlay = json["enableSteamOverlay"].toBool(true);
    settings.enableMangoHud = json["enableMangoHud"].toBool(false);

    // Executable Selection
    settings.executablePath = json["executablePath"].toString();

    // Proton Version
    settings.protonVersion = json["protonVersion"].toString();

    // Custom launch parameters
    settings.customLaunchParams = json["customLaunchParams"].toString();

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

QStringList DLSSSettings::availableFGModes()
{
    return {
        "",  // Default/App controlled
        "ON",
        "OFF",
        "AUTO",
        "DYNAMIC"
    };
}

QStringList DLSSSettings::availableFGPresets()
{
    // Frame Generation exposes far fewer real presets than Super Resolution:
    // Preset A (original) and Preset B (Enhanced FG, DLSS 4), plus "latest".
    return {
        "",  // Default/App controlled
        "RENDER_PRESET_A",
        "RENDER_PRESET_B",
        "RENDER_PRESET_LATEST"
    };
}

bool DLSSSettings::operator==(const DLSSSettings& other) const
{
    return enableNVAPI == other.enableNVAPI &&
           enableNGXUpdater == other.enableNGXUpdater &&
           enableReflex == other.enableReflex &&
           enableVkd3dLowLatency == other.enableVkd3dLowLatency &&
           enablePrimeRenderOffload == other.enablePrimeRenderOffload &&
           enableVkd3dDescriptorHeap == other.enableVkd3dDescriptorHeap &&
           vkd3dConfigExtra == other.vkd3dConfigExtra &&
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
           fgMode == other.fgMode &&
           fgPreset == other.fgPreset &&
           dlssUpgrade == other.dlssUpgrade &&
           dlssVersion == other.dlssVersion &&
           showIndicator == other.showIndicator &&
           enableSmoothMotion == other.enableSmoothMotion &&
           enableFrameRateLimit == other.enableFrameRateLimit &&
           targetFrameRate == other.targetFrameRate &&
           enableProtonWayland == other.enableProtonWayland &&
           enableProtonHDR == other.enableProtonHDR &&
           enableHDRWSI == other.enableHDRWSI &&
           disableAutoHDR == other.disableAutoHDR &&
           protonPriorityHigh == other.protonPriorityHigh &&
           protonUseNTSync == other.protonUseNTSync &&
           protonUseD7VK == other.protonUseD7VK &&
           protonLog == other.protonLog &&
           enableSteamOverlay == other.enableSteamOverlay &&
           enableMangoHud == other.enableMangoHud &&
           executablePath == other.executablePath &&
           protonVersion == other.protonVersion &&
           customLaunchParams == other.customLaunchParams;
}
