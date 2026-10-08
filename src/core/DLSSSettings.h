#ifndef DLSSSETTINGS_H
#define DLSSSETTINGS_H

#include <QString>
#include <QJsonObject>
#include <QStringList>

class DLSSSettings {
public:
    DLSSSettings() = default;

    // General
    bool enableNVAPI = true;  // false -> PROTON_DISABLE_NVAPI=1; on by default in Proton
    bool enableNGXUpdater = false;
    bool enableReflex = false;  // DXVK_NVAPI_VKREFLEX=1
    bool enableVkd3dLowLatency = false;  // PROTON_VKD3D_LOWLATENCY=1 (CachyOS ≥ 11.0-20260703)
    bool enablePrimeRenderOffload = false;  // __NV_PRIME_RENDER_OFFLOAD trio for hybrid iGPU+dGPU systems
    bool enableVkd3dDescriptorHeap = false;  // VKD3D_CONFIG=descriptor_heap (experimental, driver ≥ 595.44.02)
    // Other VKD3D_CONFIG flags (e.g. "dxr") round-tripped from imported launch
    // options so enabling descriptor_heap doesn't clobber them. Not in the UI.
    QString vkd3dConfigExtra;

    // Super Resolution (SR)
    bool srOverride = false;
    QString srMode;
    QString srPreset;
    int srScalingRatio = 0;  // 33-100, 0 = default

    // Ray Reconstruction (RR)
    bool rrOverride = false;
    QString rrMode;
    QString rrPreset;
    int rrScalingRatio = 0;

    // Frame Generation (FG)
    bool fgOverride = false;
    int fgMultiFrameCount = 0;  // 0-3
    QString fgMode;    // NGX_DLSSG_MODE: "", ON, OFF, AUTO, DYNAMIC
    QString fgPreset;  // NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION

    // DLSS Upgrade (PROTON_DLSS_UPGRADE, Proton-CachyOS / GE-Proton)
    bool dlssUpgrade = false;
    QString dlssVersion;  // e.g. "310.9.1"; empty = newest (PROTON_DLSS_UPGRADE=1)

    // Indicators
    bool showIndicator = false;

    // Smooth Motion / Frame Rate
    bool enableSmoothMotion = false;
    bool enableFrameRateLimit = false;
    int targetFrameRate = 60;  // FPS limit: DXVK_CONFIG dxvk.maxFrameRate + VKD3D_FRAME_RATE
    // Other DXVK_CONFIG options (';'-separated) round-tripped from imported
    // launch options so the frame cap doesn't clobber them. Not in the UI.
    QString dxvkConfigExtra;

    // HDR Settings
    bool enableProtonWayland = false;
    bool enableProtonHDR = false;  // PROTON_ENABLE_HDR=1 (GE) + DXVK_HDR=1 (CachyOS)
    bool enableHDRWSI = false;
    bool disableAutoHDR = false;  // DXVK_NO_HDR=1 — opt out of CachyOS auto-HDR

    // Proton Tweaks
    bool protonUseNTSync = false;      // PROTON_USE_NTSYNC=1 — opt-in up to Proton 10
    bool protonDisableNTSync = false;  // PROTON_NO_NTSYNC=1 — opt-out from Proton 11
    bool protonUseD7VK = false;  // PROTON_USE_D7VK=1 (GE) + PROTON_D7VK_DDRAW=1 (CachyOS) — d7vk
    bool protonLog = false;

    // Overlay
    bool enableSteamOverlay = true;
    bool enableMangoHud = false;

    // Executable Selection (user preference)
    QString executablePath;

    // Proton Version Selection
    QString protonVersion;  // Empty/"auto" = latest CachyOS, "latest-ge" = latest GE, or specific version folder name

    // Custom launch parameters — free-form extra options appended to the Steam
    // launch string. Holds everything ProtonForge doesn't model (e.g. game args
    // like "/WineDetectionEnabled:False"). If this contains the literal
    // "%command%", it controls where %command% and any trailing game arguments
    // are placed; otherwise it is treated as extra env vars before %command%.
    QString customLaunchParams;

    // Serialization
    QJsonObject toJson() const;
    static DLSSSettings fromJson(const QJsonObject& json);

    // Available options
    static QStringList availableSRModes();
    static QStringList availableRRModes();
    static QStringList availablePresets();
    static QStringList availableFGModes();
    static QStringList availableFGPresets();

    bool operator==(const DLSSSettings& other) const;
};

#endif // DLSSSETTINGS_H
