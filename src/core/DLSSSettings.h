#ifndef DLSSSETTINGS_H
#define DLSSSETTINGS_H

#include <QString>
#include <QJsonObject>
#include <QStringList>

// Every field must also have a row in fields() in DLSSSettings.cpp — that one
// table drives toJson, fromJson and operator==.
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
    // DXVK_NVAPI_D3D12_NV_SHADER_EXTN=1: NVIDIA shader extensions in D3D12
    // (Shader Execution Reordering). Experimental, DXVK-NVAPI 0.9.2 + vkd3d-proton 3.0.1.
    bool enableD3D12ShaderExtensions = false;
    // Other VKD3D_CONFIG flags (e.g. "dxr") round-tripped from imported launch
    // options so enabling descriptor_heap doesn't clobber them. Not in the UI.
    QString vkd3dConfigExtra;

    // Super Resolution (SR)
    bool srOverride = false;
    QString srMode;
    QString srPreset;
    int srScalingRatio = 0;  // 33-100, 0 = default; only written with srMode CUSTOM

    // Ray Reconstruction (RR)
    bool rrOverride = false;
    QString rrMode;
    QString rrPreset;
    int rrScalingRatio = 0;  // same, with rrMode CUSTOM

    // Frame Generation (FG)
    bool fgOverride = false;
    int fgMultiFrameCount = 0;  // generated frames: 0 = app default, 1-5 = 2x-6x
    QString fgMode;    // NGX_DLSSG_MODE: "", ON, OFF, AUTO, DYNAMIC
    QString fgPreset;  // NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION
    // Dynamic Multi-Frame Generation; only written when fgMode is DYNAMIC.
    int fgDynamicTargetFps = 0;  // NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE, 0 = driver default
    int fgDynamicMaxCount = 0;   // NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX, same scale as fgMultiFrameCount

    // DLSS Upgrade (PROTON_DLSS_UPGRADE, Proton-CachyOS / GE-Proton)
    bool dlssUpgrade = false;
    QString dlssVersion;  // e.g. "310.9.1"; empty = newest (PROTON_DLSS_UPGRADE=1)

    // Indicators
    bool showIndicator = false;

    // Smooth Motion / Frame Rate
    bool enableSmoothMotion = false;
    // NVPRESENT_QUEUE_FAMILY=1: present on the graphics queue, which third-party
    // overlays need. Only written together with Smooth Motion.
    bool smoothMotionGraphicsQueue = false;
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
    // Controller input (GE-Proton / Proton-CachyOS). One choice in the UI;
    // SDL implies no Steam Input, so the two are never both offered.
    bool protonNoSteamInput = false;   // PROTON_NO_STEAMINPUT=1 — hide Steam's virtual pad
    bool protonPreferSdl = false;      // PROTON_PREFER_SDL=1 — all controllers through SDL
    bool protonUseD7VK = false;  // PROTON_USE_D7VK=1 (GE) + PROTON_D7VK_DDRAW=1 (CachyOS) — d7vk
    bool protonLog = false;

    // Overlay
    bool enableSteamOverlay = true;
    bool enableMangoHud = false;

    // Executable Selection (user preference)
    QString executablePath;

    // Proton Version Selection
    // Empty/"auto" = Steam's per-game choice, else ProtonForge's default;
    // "latest-cachyos" / "latest-ge" / "steam-proton" = newest of that kind;
    // otherwise a folder name in compatibilitytools.d or an absolute path.
    // A choice that no longer resolves falls back to "auto".
    QString protonVersion;

    // Custom launch parameters — free-form extra options appended to the Steam
    // launch string. Holds everything ProtonForge doesn't model (e.g. game args
    // like "/WineDetectionEnabled:False"). If this contains the literal
    // "%command%", it controls where %command% and any trailing game arguments
    // are placed; otherwise it is treated as extra env vars before %command%.
    QString customLaunchParams;

    // A scaling ratio only takes effect with the mode set to CUSTOM, which older
    // versions did not write. Gives a ratio without any mode that mode, so it
    // does what it was set for, and keeps it inside the 33-100 the driver takes.
    // Called wherever settings come in: fromJson() and parseLaunchOptions().
    void adoptCustomScaling();
    // The one mode that reads a scaling ratio. Case-insensitive: the CLI takes
    // what the user typed.
    static bool isCustomMode(const QString& mode)
    {
        return mode.compare(QLatin1String("CUSTOM"), Qt::CaseInsensitive) == 0;
    }

    // Serialization
    QJsonObject toJson() const;
    static DLSSSettings fromJson(const QJsonObject& json);

    // Available options
    static QStringList availableSRModes();
    static QStringList availableRRModes();
    static QStringList availablePresets();     // Super Resolution
    static QStringList availableRRPresets();
    static QStringList availableFGModes();
    static QStringList availableFGPresets();

    bool operator==(const DLSSSettings& other) const;
};

#endif // DLSSSETTINGS_H
