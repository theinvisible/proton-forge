#include "EnvBuilder.h"
#include "utils/HostEnvironment.h"
#include <QHash>
#include <QRegularExpression>

namespace {

// Which layer reads each variable ProtonForge emits — see EnvBuilder::VarScope
// for what the two values mean and why the distinction matters.
//
// The judgement calls are the Any entries that are not the PRIME trio:
//   NVPRESENT_ENABLE_SMOOTH_MOTION  the driver's own present-time interpolation,
//                                   below the API a game uses, so not Wine-bound
//   NVPRESENT_QUEUE_FAMILY          the same driver layer's choice of present
//                                   queue (graphics instead of its own)
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
        {"NVPRESENT_QUEUE_FAMILY",         EnvBuilder::VarScope::Any},
        {"ENABLE_HDR_WSI",                 EnvBuilder::VarScope::Any},
        {"MANGOHUD",                       EnvBuilder::VarScope::Any},

        // Proton wrapper script.
        {"PROTON_DISABLE_NVAPI",           EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_ENABLE_NGX_UPDATER",      EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_VKD3D_LOWLATENCY",        EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_DLSS_UPGRADE",            EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_DLSS_INDICATOR",          EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_ENABLE_WAYLAND",          EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_ENABLE_HDR",              EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_USE_NTSYNC",              EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_NO_NTSYNC",               EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_USE_D7VK",                EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_D7VK_DDRAW",              EnvBuilder::VarScope::ProtonOnly},
        {"PROTON_LOG",                     EnvBuilder::VarScope::ProtonOnly},

        // DXVK-NVAPI — the NVAPI stand-in that exists only inside Wine. Every
        // DLSS SR/RR/FG override travels in DXVK_NVAPI_DRS_SETTINGS.
        {"DXVK_NVAPI_DRS_SETTINGS",        EnvBuilder::VarScope::ProtonOnly},
        {"DXVK_NVAPI_VKREFLEX",            EnvBuilder::VarScope::ProtonOnly},

        // DXVK / vkd3d-proton.
        {"DXVK_HDR",                       EnvBuilder::VarScope::ProtonOnly},
        {"DXVK_NO_HDR",                    EnvBuilder::VarScope::ProtonOnly},
        {"DXVK_CONFIG",                    EnvBuilder::VarScope::ProtonOnly},
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

// The user's custom launch parameters, split the way Steam will run them.
//
// With "%command%": KEY=VALUE tokens in front of it are environment, any other
// token in front is a wrapper command (gamemoderun, strangle 60, …), and
// everything after it is a game argument.
//
// Without it, Steam appends the whole string to the game's command line — so
// "-novid -console" is two game arguments, not something to run. ProtonForge
// has always also accepted a bare "FOO=1" there as an environment variable,
// and keeps doing so for the KEY=VALUE tokens at the start; from the first
// token that is not one, the rest are game arguments.
struct CustomParts {
    QStringList env;
    QStringList wrapper;
    QStringList args;
};

bool isAssignment(const QString& token)
{
    return token.indexOf('=') > 0;
}

CustomParts splitCustom(const QString& custom)
{
    CustomParts parts;
    const QStringList tokens = tokenize(custom);
    const bool hasCommand = tokens.contains(QStringLiteral("%command%"));

    bool afterCommand = false;
    for (const QString& token : tokens) {
        if (hasCommand) {
            if (token == QLatin1String("%command%")) {
                afterCommand = true;
            } else if (afterCommand) {
                parts.args << token;
            } else if (isAssignment(token)) {
                parts.env << token;
            } else {
                parts.wrapper << token;
            }
        } else if (parts.args.isEmpty() && isAssignment(token)) {
            parts.env << token;
        } else {
            parts.args << token;
        }
    }
    return parts;
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

// DXVK_CONFIG, the same way: the frame cap ProtonForge manages, then whatever
// foreign options were round-tripped through dxvkConfigExtra. DXVK 3.0 removed
// DXVK_FRAME_RATE; dxvk.maxFrameRate is what it reads instead, and Proton's
// own script appends to this variable with ';' as well.
QString dxvkConfigValue(const DLSSSettings& s)
{
    QStringList options;
    if (s.enableFrameRateLimit && s.targetFrameRate > 0) {
        options << QString("dxvk.maxFrameRate=%1").arg(s.targetFrameRate);
    }
    if (!s.dxvkConfigExtra.isEmpty()) {
        options << s.dxvkConfigExtra;
    }
    return options.join(';');
}

// PROTON_DLSS_UPGRADE takes "1" (newest) or a DLSS version to pin, e.g.
// "310.9.1". Anything that does not look like a version falls back to "1".
bool isDlssVersion(const QString& v)
{
    static const QRegularExpression re(R"(^\d+(\.\d+){1,3}$)");
    return re.match(v).hasMatch();
}

QString joinNonEmpty(const QString& a, const QString& b, QChar sep)
{
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    return a + sep + b;
}

// Every variable ProtonForge sets for `s`, in emission order — the single list
// both buildLaunchOptions() and buildEnvironment() write out, so the Steam
// string and a direct launch cannot drift apart.
//
// `customEnv` is the KEY=VALUE part of the user's custom parameters. A
// DXVK_CONFIG or VKD3D_CONFIG in it is merged into ours and removed from it:
// both builders put custom variables last, and the later assignment wins, so
// left alone it would silently replace the frame cap or descriptor_heap.
QList<QPair<QString, QString>> managedEnv(const DLSSSettings& s, QStringList& customEnv)
{
    QString customDxvk, customVkd3d;
    for (auto it = customEnv.begin(); it != customEnv.end();) {
        const int eq = it->indexOf('=');
        const QString key = it->left(eq);
        if (key == QLatin1String("DXVK_CONFIG") || key == QLatin1String("VKD3D_CONFIG")) {
            QString& into = key == QLatin1String("DXVK_CONFIG") ? customDxvk : customVkd3d;
            into = joinNonEmpty(into, stripQuotes(it->mid(eq + 1)),
                                key == QLatin1String("DXVK_CONFIG") ? ';' : ',');
            it = customEnv.erase(it);
        } else {
            ++it;
        }
    }

    QList<QPair<QString, QString>> env;
    const auto set = [&env](const char* key, const QString& value) {
        env.append({QString::fromLatin1(key), value});
    };
    const QString one = QStringLiteral("1");

    // PRIME render offload for hybrid iGPU+dGPU systems. Deliberately the
    // portable NVIDIA-recommended trio — no distro-specific ICD path.
    if (s.enablePrimeRenderOffload) {
        set("__NV_PRIME_RENDER_OFFLOAD", one);
        set("__GLX_VENDOR_LIBRARY_NAME", QStringLiteral("nvidia"));
        set("__VK_LAYER_NV_optimus", QStringLiteral("NVIDIA_only"));
    }

    // NVAPI is on by default in every current Proton; PROTON_ENABLE_NVAPI is
    // read by none of them any more. Only turning it off needs saying, and
    // PROTON_DISABLE_NVAPI is read by Valve's Proton 11, Proton-CachyOS and
    // GE-Proton (10-25 onward).
    if (!s.enableNVAPI) set("PROTON_DISABLE_NVAPI", one);
    if (s.enableNGXUpdater) set("PROTON_ENABLE_NGX_UPDATER", one);
    if (s.enableReflex) set("DXVK_NVAPI_VKREFLEX", one);
    if (s.enableVkd3dLowLatency) set("PROTON_VKD3D_LOWLATENCY", one);

    const QString vkd3dConfig = joinNonEmpty(vkd3dConfigValue(s), customVkd3d, ',');
    if (!vkd3dConfig.isEmpty()) set("VKD3D_CONFIG", vkd3dConfig);

    if (s.dlssUpgrade)
        set("PROTON_DLSS_UPGRADE", isDlssVersion(s.dlssVersion) ? s.dlssVersion : one);
    if (s.showIndicator) set("PROTON_DLSS_INDICATOR", one);

    // All DLSS overrides go in this single variable.
    const QString drs = EnvBuilder::buildDRSSettings(s);
    if (!drs.isEmpty()) set("DXVK_NVAPI_DRS_SETTINGS", drs);

    if (s.enableSmoothMotion) set("NVPRESENT_ENABLE_SMOOTH_MOTION", one);
    // Present on the graphics queue so third-party overlays see the frames;
    // meaningless without Smooth Motion, so only written alongside it.
    if (s.enableSmoothMotion && s.smoothMotionGraphicsQueue) set("NVPRESENT_QUEUE_FAMILY", one);

    // Frame rate limit: DXVK (D3D8-11) through DXVK_CONFIG, vkd3d-proton
    // (D3D12) through its own variable, which it still reads.
    const QString dxvkConfig = joinNonEmpty(dxvkConfigValue(s), customDxvk, ';');
    if (!dxvkConfig.isEmpty()) set("DXVK_CONFIG", dxvkConfig);
    if (s.enableFrameRateLimit && s.targetFrameRate > 0)
        set("VKD3D_FRAME_RATE", QString::number(s.targetFrameRate));

    // HDR. GE-Proton reads PROTON_ENABLE_HDR (and sets DXVK_HDR from it);
    // Proton-CachyOS retired that variable and wants DXVK_HDR itself. Both are
    // harmless where unread, and the string has to work with whichever Proton
    // Steam ends up running.
    if (s.enableProtonWayland) set("PROTON_ENABLE_WAYLAND", one);
    if (s.enableProtonHDR) {
        set("PROTON_ENABLE_HDR", one);
        set("DXVK_HDR", one);
    }
    if (s.enableHDRWSI) set("ENABLE_HDR_WSI", one);
    if (s.disableAutoHDR) set("DXVK_NO_HDR", one);

    // Proton tweaks. NTSync is opt-in up to Proton 10 and on by default from
    // 11, where only switching it off is possible.
    if (s.protonUseNTSync) set("PROTON_USE_NTSYNC", one);
    if (s.protonDisableNTSync) set("PROTON_NO_NTSYNC", one);
    // d7vk: GE-Proton's name and Proton-CachyOS's name for the same switch.
    if (s.protonUseD7VK) {
        set("PROTON_USE_D7VK", one);
        set("PROTON_D7VK_DDRAW", one);
    }
    if (s.protonLog) set("PROTON_LOG", one);

    if (s.enableMangoHud) set("MANGOHUD", one);

    return env;
}

// A value as it has to appear in a launch-options string, which Steam hands
// to a shell: ';' would end the command there. Whitespace is deliberately not
// handled — tokenize() is not quote-aware, so a quoted value with spaces would
// not survive the way back anyway.
QString shellValue(const QString& value)
{
    static const QRegularExpression needsQuotes(R"([;&|<>()$`])");
    return value.contains(needsQuotes) ? '"' + value + '"' : value;
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
        s.rrPreset = canonicalize(value, DLSSSettings::availableRRPresets());
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
    } else if (key == "NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE") {
        s.fgDynamicTargetFps = value.toInt();
    } else if (key == "NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX") {
        s.fgDynamicMaxCount = value.toInt();
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
    // NVAPI is on unless something turns it off. PROTON_ENABLE_NVAPI=1 is a
    // relic: consumed (so the next write drops it) but it changes nothing,
    // and it cannot override a PROTON_DISABLE_NVAPI=1 elsewhere in the string.
    if (key == "PROTON_ENABLE_NVAPI") { if (value == "0") s.enableNVAPI = false; return true; }
    if (key == "PROTON_DISABLE_NVAPI") { if (value != "0") s.enableNVAPI = false; return true; }
    if (key == "__NV_PRIME_RENDER_OFFLOAD") { s.enablePrimeRenderOffload = (value != "0"); return true; }
    if (key == "PROTON_ENABLE_NGX_UPDATER") { s.enableNGXUpdater = (value != "0"); return true; }
    if (key == "DXVK_NVAPI_VKREFLEX") { s.enableReflex = (value != "0"); return true; }
    if (key == "PROTON_VKD3D_LOWLATENCY") { s.enableVkd3dLowLatency = (value != "0"); return true; }
    if (key == "PROTON_DLSS_UPGRADE") {
        s.dlssUpgrade = (value != "0");
        s.dlssVersion = isDlssVersion(value) ? value : QString();
        return true;
    }
    if (key == "PROTON_DLSS_INDICATOR") { s.showIndicator = (value != "0"); return true; }
    if (key == "NVPRESENT_ENABLE_SMOOTH_MOTION") { s.enableSmoothMotion = (value != "0"); return true; }
    // Only 1 (graphics queue) is documented; anything else is the user's own
    // and stays in the custom params.
    if (key == "NVPRESENT_QUEUE_FAMILY" && value == "1") { s.smoothMotionGraphicsQueue = true; return true; }
    if (key == "PROTON_ENABLE_WAYLAND") { s.enableProtonWayland = (value != "0"); return true; }
    if (key == "PROTON_ENABLE_HDR") { s.enableProtonHDR = (value != "0"); return true; }
    // DXVK_HDR=1 alone is how Proton-CachyOS users switch HDR on. An explicit
    // DXVK_HDR=0 is not ours to model and stays a custom parameter.
    if (key == "DXVK_HDR") {
        if (value == "0") return false;
        s.enableProtonHDR = true;
        return true;
    }
    if (key == "ENABLE_HDR_WSI") { s.enableHDRWSI = (value != "0"); return true; }
    if (key == "DXVK_NO_HDR") { s.disableAutoHDR = (value != "0"); return true; }
    if (key == "PROTON_USE_NTSYNC") { s.protonUseNTSync = (value != "0"); return true; }
    if (key == "PROTON_NO_NTSYNC") { s.protonDisableNTSync = (value != "0"); return true; }
    if (key == "PROTON_USE_D7VK" || key == "PROTON_D7VK_DDRAW") {
        if (value != "0") s.protonUseD7VK = true;
        return true;
    }
    if (key == "PROTON_LOG") { s.protonLog = (value != "0"); return true; }
    if (key == "MANGOHUD") { s.enableMangoHud = (value != "0"); return true; }
    // DXVK covers D3D8-11, VKD3D covers D3D12 — both map onto the same
    // frame-rate-limit setting and are emitted together. DXVK_FRAME_RATE is
    // what DXVK read before 3.0; still understood, so old strings migrate to
    // DXVK_CONFIG on the next write.
    if (key == "DXVK_FRAME_RATE" || key == "VKD3D_FRAME_RATE") {
        s.enableFrameRateLimit = true;
        s.targetFrameRate = value.toInt();
        return true;
    }
    if (key == "DXVK_CONFIG") {
        QStringList rest;
        for (const QString& option : value.split(';', Qt::SkipEmptyParts)) {
            const QString o = option.trimmed();
            const int eq = o.indexOf('=');
            const QString name = o.left(eq).trimmed();
            const int fps = eq > 0 ? o.mid(eq + 1).trimmed().toInt() : 0;
            if (name.compare("dxvk.maxFrameRate", Qt::CaseInsensitive) == 0 && fps > 0) {
                s.enableFrameRateLimit = true;
                s.targetFrameRate = fps;
            } else if (!o.isEmpty()) {
                rest << o;
            }
        }
        s.dxvkConfigExtra = joinNonEmpty(s.dxvkConfigExtra, rest.join(';'), ';');
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

        // Dynamic MFG parameters mean nothing in any other mode; they stay in
        // the settings (like an SR preset with the override off) but are not written.
        if (settings.fgMode.compare("DYNAMIC", Qt::CaseInsensitive) == 0) {
            if (settings.fgDynamicTargetFps > 0) {
                drsSettings << QString("NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE=%1").arg(settings.fgDynamicTargetFps);
            }
            if (settings.fgDynamicMaxCount > 0) {
                drsSettings << QString("NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX=%1").arg(settings.fgDynamicMaxCount);
            }
        }
    }

    return drsSettings.join(",");
}

QString EnvBuilder::buildLaunchOptions(const DLSSSettings& settings)
{
    // Custom launch parameters, see splitCustom(). Their env vars join ours in
    // front of every command token, mangohud included.
    CustomParts custom = splitCustom(settings.customLaunchParams);

    QStringList envVars;
    for (const auto& kv : managedEnv(settings, custom.env)) {
        envVars << kv.first + '=' + shellValue(kv.second);
    }
    envVars << custom.env;

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

    tail << custom.wrapper << QStringLiteral("%command%") << custom.args;

    return (envVars + tail).join(" ");
}

QProcessEnvironment EnvBuilder::buildEnvironment(const DLSSSettings& settings)
{
    // Not systemEnvironment(): inside an AppImage ours points at the bundle, and
    // a game that inherits that loads the bundle's libstdc++ over its own — from
    // a mount that disappears when we exit. Identity function everywhere else.
    QProcessEnvironment env = HostEnvironment::forChildProcess();

    QStringList customEnv = splitCustom(settings.customLaunchParams).env;
    for (const auto& kv : managedEnv(settings, customEnv)) {
        env.insert(kv.first, kv.second);
    }

    // Custom launch parameters: their KEY=VALUE portion goes into the process
    // environment. Wrapper and game arguments are customWrapper() and
    // customGameArgs().
    for (const QString& token : customEnv) {
        const int eq = token.indexOf('=');
        env.insert(token.left(eq), stripQuotes(token.mid(eq + 1)));
    }

    return env;
}

bool EnvBuilder::sameLaunchOptions(const QString& a, const QString& b)
{
    const auto normalised = [](const QString& s) {
        const QString t = s.simplified();
        return t.isEmpty() ? QStringLiteral("%command%") : t;
    };
    return normalised(a) == normalised(b);
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
    s.enableNVAPI = true;   // on unless the string turns it off
    s.enableNGXUpdater = false;
    s.enableReflex = false;
    s.enableVkd3dLowLatency = false;
    s.enablePrimeRenderOffload = false;
    s.enableVkd3dDescriptorHeap = false; s.vkd3dConfigExtra.clear();
    s.dlssUpgrade = false; s.dlssVersion.clear();
    s.showIndicator = false;
    s.srOverride = false; s.srMode.clear(); s.srPreset.clear(); s.srScalingRatio = 0;
    s.rrOverride = false; s.rrMode.clear(); s.rrPreset.clear(); s.rrScalingRatio = 0;
    s.fgOverride = false; s.fgMultiFrameCount = 0; s.fgMode.clear(); s.fgPreset.clear();
    s.fgDynamicTargetFps = 0; s.fgDynamicMaxCount = 0;
    s.enableSmoothMotion = false; s.smoothMotionGraphicsQueue = false;
    s.enableFrameRateLimit = false; s.targetFrameRate = 60; s.dxvkConfigExtra.clear();
    s.enableProtonWayland = false; s.enableProtonHDR = false; s.enableHDRWSI = false;
    s.disableAutoHDR = false;
    s.protonUseNTSync = false; s.protonDisableNTSync = false;
    s.protonUseD7VK = false; s.protonLog = false;
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

    // Without %command% Steam appends the string to the game's command line.
    // Spelled out, "-novid -console" becomes "%command% -novid -console" — which
    // is what it meant all along, and what the custom-params field then shows.
    if (!seenCommand) {
        const CustomParts parts = splitCustom(result.customParams);
        if (!parts.args.isEmpty()) {
            result.customParams =
                (parts.env + QStringList{QStringLiteral("%command%")} + parts.args).join(" ");
        }
    }
    return result;
}

QStringList EnvBuilder::customGameArgs(const DLSSSettings& settings)
{
    return splitCustom(settings.customLaunchParams).args;
}

QStringList EnvBuilder::customWrapper(const DLSSSettings& settings)
{
    return splitCustom(settings.customLaunchParams).wrapper;
}
