#include "EnvBuilder.h"
#include <QHash>
#include <QRegularExpression>

namespace {

// Which layer reads each variable ProtonForge emits — see EnvBuilder::VarScope
// for what the two values mean and why the distinction matters.
//
// The judgement calls are the four Any entries that are not the PRIME trio:
//   NVPRESENT_ENABLE_SMOOTH_MOTION  the driver's own present-time interpolation,
//                                   below the API a game uses, so not Wine-bound
//   ENABLE_HDR_WSI                  a Vulkan implicit layer, loaded by the loader
//                                   for any Vulkan client, native included
//   MANGOHUD                        likewise an implicit layer (plus a wrapper)
// Everything else names a component that only exists inside a Proton prefix:
// the Proton script itself (PROTON_*), DXVK (DXVK_*), vkd3d-proton (VKD3D_*)
// and DXVK-NVAPI (DXVK_NVAPI_*), which is what carries the DLSS overrides.
const QHash<QString, EnvBuilder::VarScope>& varScopeTable()
{
    static const QHash<QString, EnvBuilder::VarScope> table = {
        // Driver / Vulkan layer / wrapper — reaches a native game too.
        {"__NV_PRIME_RENDER_OFFLOAD",      EnvBuilder::VarScope::Any},
        {"__GLX_VENDOR_LIBRARY_NAME",      EnvBuilder::VarScope::Any},
        {"__VK_LAYER_NV_optimus",          EnvBuilder::VarScope::Any},
        {"NVPRESENT_ENABLE_SMOOTH_MOTION", EnvBuilder::VarScope::Any},
        {"ENABLE_HDR_WSI",                 EnvBuilder::VarScope::Any},
        {"MANGOHUD",                       EnvBuilder::VarScope::Any},

        // Proton wrapper script.
        {"PROTON_ENABLE_NVAPI",            EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_ENABLE_NGX_UPDATER",      EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_VKD3D_LOWLATENCY",        EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_DLSS_UPGRADE",            EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_DLSS_INDICATOR",          EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_ENABLE_WAYLAND",          EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_ENABLE_HDR",              EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_PRIORITY_HIGH",           EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_USE_NTSYNC",              EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_USE_D7VK",                EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_LOG",                     EnvBuilder::VarScope::ProtonOnly},

        // DXVK-NVAPI — the NVAPI stand-in that exists only inside Wine. Every
        // DLSS SR/RR/FG override travels in DXVK_NVAPI_DRS_SETTINGS.
        {"DXVK_NVAPI_DRS_SETTINGS",        EnvBuilder::VarScope::ProtonOnly},
        {"DXVK_NVAPI_VKREFLEX",            EnvBuilder::VarScope::ProtonOnly},

        // DXVK / vkd3d-proton.
        {"DXVK_NO_HDR",                    EnvBuilder::VarScope::ProtonOnly},
        {"DXVK_FRAME_RATE",                EnvBuilder::VarScope::ProtonOnly},
        {"VKD3D_FRAME_RATE",               EnvBuilder::VarScope::ProtonOnly},
        {"VKD3D_CONFIG",                   EnvBuilder::VarScope::ProtonOnly},
    };
    return table;
}

// Splits a launch-options string into whitespace-separated tokens. The
// DXVK_NVAPI_DRS_SETTINGS value is comma-joined (no spaces), so plain
// whitespace splitting is safe. Quoted values containing spaces are not
// handled (rare in Steam launch options) — a known v1 limitation.
QStringList tokenize(const QString& s)
{
    static const QRegularExpression ws("\\s+");
    return s.split(ws, Qt::SkipEmptyParts);
}

// Removes one pair of surrounding quotes from an env-var value. Steam launch
// options are often written shell-style (KEY="a,b,c"); the quotes are not part
// of the value and would break sub-key parsing (e.g. DXVK_NVAPI_DRS_SETTINGS).
QString stripQuotes(const QString& v)
{
    if (v.length() >= 2 && ((v.startsWith('"') && v.endsWith('"')) ||
                            (v.startsWith('\'') && v.endsWith('\'')))) {
        return v.mid(1, v.length() - 2);
    }
    return v;
}

// Joins the VKD3D_CONFIG flags ProtonForge manages (descriptor_heap) with any
// foreign flags round-tripped through vkd3dConfigExtra, so both survive.
QString vkd3dConfigValue(const DLSSSettings& s)
{
    QStringList flags;
    if (s.enableVkd3dDescriptorHeap) {
        flags << "descriptor_heap";
    }
    if (!s.vkd3dConfigExtra.isEmpty()) {
        flags << s.vkd3dConfigExtra;
    }
    return flags.join(',');
}

// Returns the canonical-cased entry from `options` matching `value`
// case-insensitively, or `value` unchanged if there is no match (so custom
// values survive). buildDRSSettings lowercases mode values, so the reverse
// path needs this to restore the combo-box data values (e.g. "quality" ->
// "QUALITY").
QString canonicalize(const QString& value, const QStringList& options)
{
    for (const QString& opt : options) {
        if (opt.compare(value, Qt::CaseInsensitive) == 0)
            return opt;
    }
    return value;
}

// Applies one NGX_* sub-key from a DXVK_NVAPI_DRS_SETTINGS value to settings.
void applyDrsSubKey(DLSSSettings& s, const QString& key, const QString& value)
{
    if (key == "NGX_DLSS_SR_OVERRIDE") {
        s.srOverride = (value.compare("on", Qt::CaseInsensitive) == 0);
    } else if (key == "NGX_DLSS_SR_MODE") {
        s.srMode = canonicalize(value, DLSSSettings::availableSRModes());
    } else if (key == "NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION") {
        s.srPreset = canonicalize(value, DLSSSettings::availablePresets());
    } else if (key == "NGX_DLSS_SR_OVERRIDE_SCALING_RATIO") {
        s.srScalingRatio = value.toInt();
    } else if (key == "NGX_DLSS_RR_OVERRIDE") {
        s.rrOverride = (value.compare("on", Qt::CaseInsensitive) == 0);
    } else if (key == "NGX_DLSS_RR_MODE") {
        s.rrMode = canonicalize(value, DLSSSettings::availableRRModes());
    } else if (key == "NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION") {
        s.rrPreset = canonicalize(value, DLSSSettings::availablePresets());
    } else if (key == "NGX_DLSS_RR_OVERRIDE_SCALING_RATIO") {
        s.rrScalingRatio = value.toInt();
    } else if (key == "NGX_DLSS_FG_OVERRIDE") {
        s.fgOverride = (value.compare("on", Qt::CaseInsensitive) == 0);
    } else if (key == "NGX_DLSSG_MULTI_FRAME_COUNT") {
        s.fgMultiFrameCount = value.toInt();
    } else if (key == "NGX_DLSSG_MODE") {
        s.fgMode = canonicalize(value, DLSSSettings::availableFGModes());
    } else if (key == "NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION") {
        s.fgPreset = canonicalize(value, DLSSSettings::availableFGPresets());
    }
    // Unknown NGX_* sub-keys are dropped: there is no field for them and they
    // cannot be re-emitted via the DRS settings builder. This is acceptable
    // because every key buildDRSSettings emits is handled above.
}

// Applies a single KEY=VALUE token. Returns true if the key is one ProtonForge
// manages (and thus should NOT be preserved as a custom param). Keep this in
// sync with buildLaunchOptions / buildEnvironment.
bool applyKnownEnvVar(DLSSSettings& s, const QString& key, const QString& value)
{
    if (key == "PROTON_ENABLE_NVAPI") { s.enableNVAPI = (value != "0"); return true; }
    if (key == "__NV_PRIME_RENDER_OFFLOAD") { s.enablePrimeRenderOffload = (value != "0"); return true; }
    if (key == "PROTON_ENABLE_NGX_UPDATER") { s.enableNGXUpdater = (value != "0"); return true; }
    if (key == "DXVK_NVAPI_VKREFLEX") { s.enableReflex = (value != "0"); return true; }
    if (key == "PROTON_VKD3D_LOWLATENCY") { s.enableVkd3dLowLatency = (value != "0"); return true; }
    if (key == "PROTON_DLSS_UPGRADE") { s.dlssUpgrade = (value != "0"); return true; }
    if (key == "PROTON_DLSS_INDICATOR") { s.showIndicator = (value != "0"); return true; }
    if (key == "NVPRESENT_ENABLE_SMOOTH_MOTION") { s.enableSmoothMotion = (value != "0"); return true; }
    if (key == "PROTON_ENABLE_WAYLAND") { s.enableProtonWayland = (value != "0"); return true; }
    if (key == "PROTON_ENABLE_HDR") { s.enableProtonHDR = (value != "0"); return true; }
    if (key == "ENABLE_HDR_WSI") { s.enableHDRWSI = (value != "0"); return true; }
    if (key == "DXVK_NO_HDR") { s.disableAutoHDR = (value != "0"); return true; }
    if (key == "PROTON_PRIORITY_HIGH") { s.protonPriorityHigh = (value != "0"); return true; }
    if (key == "PROTON_USE_NTSYNC") { s.protonUseNTSync = (value != "0"); return true; }
    if (key == "PROTON_USE_D7VK") { s.protonUseD7VK = (value != "0"); return true; }
    if (key == "PROTON_LOG") { s.protonLog = (value != "0"); return true; }
    if (key == "MANGOHUD") { s.enableMangoHud = (value != "0"); return true; }
    // DXVK covers D3D9/10/11, VKD3D covers D3D12 — both map onto the same
    // frame-rate-limit setting and are emitted together.
    if (key == "DXVK_FRAME_RATE" || key == "VKD3D_FRAME_RATE") {
        s.enableFrameRateLimit = true;
        s.targetFrameRate = value.toInt();
        return true;
    }
    if (key == "VKD3D_CONFIG") {
        QStringList rest;
        const QStringList flags = value.split(',', Qt::SkipEmptyParts);
        for (const QString& flag : flags) {
            const QString f = flag.trimmed();
            if (f.compare("descriptor_heap", Qt::CaseInsensitive) == 0) {
                s.enableVkd3dDescriptorHeap = true;
            } else if (!f.isEmpty()) {
                rest << f;
            }
        }
        s.vkd3dConfigExtra = rest.join(',');
        return true;
    }
    if (key == "DXVK_NVAPI_DRS_SETTINGS") {
        const QStringList parts = value.split(',', Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            int eq = part.indexOf('=');
            if (eq > 0)
                applyDrsSubKey(s, part.left(eq), part.mid(eq + 1));
        }
        return true;
    }
    return false;
}
} // namespace

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

EnvBuilder::VarScope EnvBuilder::scopeOf(const QString& key)
{
    return varScopeTable().value(key, VarScope::Any);
}

QStringList EnvBuilder::managedVars()
{
    QStringList keys = varScopeTable().keys();
    keys.sort();
    return keys;
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

        if (!settings.fgMode.isEmpty()) {
            drsSettings << QString("NGX_DLSSG_MODE=%1").arg(settings.fgMode.toLower());
        }

        if (!settings.fgPreset.isEmpty()) {
            drsSettings << QString("NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION=%1").arg(settings.fgPreset);
        }
    }

    return drsSettings.join(",");
}

QString EnvBuilder::buildLaunchOptions(const DLSSSettings& settings)
{
    QStringList envVars;

    // PRIME render offload for hybrid iGPU+dGPU systems. Deliberately the
    // portable NVIDIA-recommended trio — no distro-specific ICD path.
    if (settings.enablePrimeRenderOffload) {
        envVars << "__NV_PRIME_RENDER_OFFLOAD=1"
                << "__GLX_VENDOR_LIBRARY_NAME=nvidia"
                << "__VK_LAYER_NV_optimus=NVIDIA_only";
    }

    // General settings
    if (settings.enableNVAPI) {
        envVars << "PROTON_ENABLE_NVAPI=1";
    }

    if (settings.enableNGXUpdater) {
        envVars << "PROTON_ENABLE_NGX_UPDATER=1";
    }

    if (settings.enableReflex) {
        envVars << "DXVK_NVAPI_VKREFLEX=1";
    }

    if (settings.enableVkd3dLowLatency) {
        envVars << "PROTON_VKD3D_LOWLATENCY=1";
    }

    const QString vkd3dConfig = vkd3dConfigValue(settings);
    if (!vkd3dConfig.isEmpty()) {
        envVars << QString("VKD3D_CONFIG=%1").arg(vkd3dConfig);
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

    // Frame Rate Limit — DXVK for D3D9/10/11, VKD3D for D3D12
    if (settings.enableFrameRateLimit && settings.targetFrameRate > 0) {
        envVars << QString("DXVK_FRAME_RATE=%1").arg(settings.targetFrameRate);
        envVars << QString("VKD3D_FRAME_RATE=%1").arg(settings.targetFrameRate);
    }

    // HDR
    if (settings.enableProtonWayland) {
        envVars << "PROTON_ENABLE_WAYLAND=1";
    }
    if (settings.enableProtonHDR) {
        envVars << "PROTON_ENABLE_HDR=1";
    }
    if (settings.enableHDRWSI) {
        envVars << "ENABLE_HDR_WSI=1";
    }
    if (settings.disableAutoHDR) {
        envVars << "DXVK_NO_HDR=1";
    }

    // Proton Tweaks
    if (settings.protonPriorityHigh) {
        envVars << "PROTON_PRIORITY_HIGH=1";
    }
    if (settings.protonUseNTSync) {
        envVars << "PROTON_USE_NTSYNC=1";
    }
    if (settings.protonUseD7VK) {
        envVars << "PROTON_USE_D7VK=1";
    }
    if (settings.protonLog) {
        envVars << "PROTON_LOG=1";
    }

    // Everything from here on is the command part, which has to follow every
    // KEY=VALUE token: `mangohud FOO=1 game` would hand FOO=1 to mangohud as an
    // argument instead of setting it in the environment.
    QStringList tail;

    // Overlay. MANGOHUD=1 alone only switches on the Vulkan implicit layer;
    // OpenGL games need libMangoHud preloaded, and the mangohud wrapper script
    // is what arranges that.
    if (settings.enableMangoHud) {
        tail << "mangohud";
    }

    // Custom launch parameters. If they contain "%command%", the custom text
    // controls placement of %command% and any trailing game arguments;
    // otherwise it is treated as extra env vars before an appended %command%.
    const QString custom = settings.customLaunchParams.trimmed();
    if (custom.contains("%command%")) {
        tail << custom;
    } else {
        if (!custom.isEmpty()) {
            envVars << custom;   // pure env vars — they belong in front
        }
        tail << "%command%";
    }

    return (envVars + tail).join(" ");
}

QProcessEnvironment EnvBuilder::buildEnvironment(const DLSSSettings& settings)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // PRIME render offload for hybrid iGPU+dGPU systems
    if (settings.enablePrimeRenderOffload) {
        env.insert("__NV_PRIME_RENDER_OFFLOAD", "1");
        env.insert("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
        env.insert("__VK_LAYER_NV_optimus", "NVIDIA_only");
    }

    // General settings
    if (settings.enableNVAPI) {
        env.insert("PROTON_ENABLE_NVAPI", "1");
    }

    if (settings.enableNGXUpdater) {
        env.insert("PROTON_ENABLE_NGX_UPDATER", "1");
    }

    if (settings.enableReflex) {
        env.insert("DXVK_NVAPI_VKREFLEX", "1");
    }

    if (settings.enableVkd3dLowLatency) {
        env.insert("PROTON_VKD3D_LOWLATENCY", "1");
    }

    const QString vkd3dConfig = vkd3dConfigValue(settings);
    if (!vkd3dConfig.isEmpty()) {
        env.insert("VKD3D_CONFIG", vkd3dConfig);
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
        env.insert("NVPRESENT_ENABLE_SMOOTH_MOTION", "1");
    }

    // Frame Rate Limit — DXVK for D3D9/10/11, VKD3D for D3D12
    if (settings.enableFrameRateLimit && settings.targetFrameRate > 0) {
        env.insert("DXVK_FRAME_RATE", QString::number(settings.targetFrameRate));
        env.insert("VKD3D_FRAME_RATE", QString::number(settings.targetFrameRate));
    }

    // HDR
    if (settings.enableProtonWayland) {
        env.insert("PROTON_ENABLE_WAYLAND", "1");
    }
    if (settings.enableProtonHDR) {
        env.insert("PROTON_ENABLE_HDR", "1");
    }
    if (settings.enableHDRWSI) {
        env.insert("ENABLE_HDR_WSI", "1");
    }
    if (settings.disableAutoHDR) {
        env.insert("DXVK_NO_HDR", "1");
    }

    // Proton Tweaks
    if (settings.protonPriorityHigh) {
        env.insert("PROTON_PRIORITY_HIGH", "1");
    }
    if (settings.protonUseNTSync) {
        env.insert("PROTON_USE_NTSYNC", "1");
    }
    if (settings.protonUseD7VK) {
        env.insert("PROTON_USE_D7VK", "1");
    }
    if (settings.protonLog) {
        env.insert("PROTON_LOG", "1");
    }

    // Overlay
    if (settings.enableMangoHud) {
        env.insert("MANGOHUD", "1");
    }

    // Custom launch parameters: apply the env-var (KEY=VALUE) portion that
    // precedes %command% to the process environment. Anything after %command%
    // is a game argument, handled separately via customGameArgs().
    QString custom = settings.customLaunchParams;
    int cmdIdx = custom.indexOf("%command%");
    const QString envPart = (cmdIdx >= 0) ? custom.left(cmdIdx) : custom;
    const QStringList tokens = tokenize(envPart);
    for (const QString& token : tokens) {
        int eq = token.indexOf('=');
        if (eq > 0) {
            env.insert(token.left(eq), stripQuotes(token.mid(eq + 1)));
        }
    }

    return env;
}

EnvBuilder::ParsedLaunchOptions EnvBuilder::parseLaunchOptions(const QString& raw, const DLSSSettings& base)
{
    ParsedLaunchOptions result;
    result.settings = base;

    // Empty launch options: nothing to import, keep the base settings as-is.
    if (raw.trimmed().isEmpty()) {
        result.customParams = base.customLaunchParams;
        return result;
    }

    // Reset every field that buildLaunchOptions can emit so the import faithfully
    // reflects the Steam string (an absent token means the option is off). Fields
    // that are never written to launch options are left untouched from `base`.
    DLSSSettings& s = result.settings;
    s.enableNVAPI = false;
    s.enableNGXUpdater = false;
    s.enableReflex = false;
    s.enableVkd3dLowLatency = false;
    s.enablePrimeRenderOffload = false;
    s.enableVkd3dDescriptorHeap = false; s.vkd3dConfigExtra.clear();
    s.dlssUpgrade = false;
    s.showIndicator = false;
    s.srOverride = false; s.srMode.clear(); s.srPreset.clear(); s.srScalingRatio = 0;
    s.rrOverride = false; s.rrMode.clear(); s.rrPreset.clear(); s.rrScalingRatio = 0;
    s.fgOverride = false; s.fgMultiFrameCount = 0; s.fgMode.clear(); s.fgPreset.clear();
    s.enableSmoothMotion = false;
    s.enableFrameRateLimit = false; s.targetFrameRate = 60;
    s.enableProtonWayland = false; s.enableProtonHDR = false; s.enableHDRWSI = false;
    s.disableAutoHDR = false;
    s.protonPriorityHigh = false; s.protonUseNTSync = false; s.protonUseD7VK = false; s.protonLog = false;
    s.enableMangoHud = false;

    const QStringList tokens = tokenize(raw);

    // PRIME render offload is emitted as a trio; its two companion variables
    // are only consumed when the main flag is present, so a lone companion
    // set for other reasons survives as a custom param.
    bool primeActive = false;
    for (const QString& token : tokens) {
        if (token == "%command%") break;
        if (token.startsWith("__NV_PRIME_RENDER_OFFLOAD=")) {
            primeActive = (stripQuotes(token.section('=', 1)) != "0");
            break;
        }
    }

    QStringList leftover;
    bool seenCommand = false;
    for (const QString& token : tokens) {
        if (token == "%command%") {
            seenCommand = true;
            leftover << token;
            continue;
        }
        if (seenCommand) {
            // Everything after %command% is a game argument — preserve verbatim.
            leftover << token;
            continue;
        }
        // The MangoHud wrapper, as buildLaunchOptions writes it. Consumed rather
        // than kept as a custom param, or the next rebuild would emit it twice.
        // Any other wrapper (gamemoderun, …) falls through to `leftover` and is
        // re-applied from there — see customWrapper().
        if (token == "mangohud") {
            s.enableMangoHud = true;
            continue;
        }
        int eq = token.indexOf('=');
        if (eq > 0) {
            const QString key = token.left(eq);
            if (applyKnownEnvVar(s, key, stripQuotes(token.mid(eq + 1)))) {
                continue;
            }
            if (primeActive && (key == "__GLX_VENDOR_LIBRARY_NAME" ||
                                key == "__VK_LAYER_NV_optimus")) {
                continue;  // part of the PRIME trio, re-emitted on rebuild
            }
        }
        // Unrecognised pre-command token (custom env var or flag).
        leftover << token;
    }

    result.customParams = leftover.join(" ");
    return result;
}

QStringList EnvBuilder::customGameArgs(const DLSSSettings& settings)
{
    const QString custom = settings.customLaunchParams;
    int cmdIdx = custom.indexOf("%command%");
    if (cmdIdx < 0) {
        return {};
    }
    const QString argsPart = custom.mid(cmdIdx + QStringLiteral("%command%").length());
    return tokenize(argsPart);
}

QStringList EnvBuilder::customWrapper(const DLSSSettings& settings)
{
    const QString custom = settings.customLaunchParams;
    int cmdIdx = custom.indexOf("%command%");
    if (cmdIdx < 0) {
        // No %command%: the whole string is env vars, nothing wraps the game.
        return {};
    }

    QStringList wrapper;
    for (const QString& token : tokenize(custom.left(cmdIdx))) {
        // KEY=VALUE is an environment assignment, applied by buildEnvironment().
        // Anything else in front of %command% is the wrapper command or one of
        // its arguments.
        if (token.indexOf('=') > 0) {
            continue;
        }
        wrapper << token;
    }
    return wrapper;
}
