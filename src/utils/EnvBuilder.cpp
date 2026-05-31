#include "EnvBuilder.h"
#include <QRegularExpression>

namespace {
// Splits a launch-options string into whitespace-separated tokens. The
// DXVK_NVAPI_DRS_SETTINGS value is comma-joined (no spaces), so plain
// whitespace splitting is safe. Quoted values containing spaces are not
// handled (rare in Steam launch options) — a known v1 limitation.
QStringList tokenize(const QString& s)
{
    static const QRegularExpression ws("\\s+");
    return s.split(ws, Qt::SkipEmptyParts);
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
    if (key == "PROTON_ENABLE_NGX_UPDATER") { s.enableNGXUpdater = (value != "0"); return true; }
    if (key == "DXVK_NVAPI_VKREFLEX") { s.enableReflex = (value != "0"); return true; }
    if (key == "PROTON_DLSS_UPGRADE") { s.dlssUpgrade = (value != "0"); return true; }
    if (key == "PROTON_DLSS_INDICATOR") { s.showIndicator = (value != "0"); return true; }
    if (key == "NVPRESENT_ENABLE_SMOOTH_MOTION") { s.enableSmoothMotion = (value != "0"); return true; }
    if (key == "PROTON_ENABLE_WAYLAND") { s.enableProtonWayland = (value != "0"); return true; }
    if (key == "PROTON_ENABLE_HDR") { s.enableProtonHDR = (value != "0"); return true; }
    if (key == "ENABLE_HDR_WSI") { s.enableHDRWSI = (value != "0"); return true; }
    if (key == "PROTON_PRIORITY_HIGH") { s.protonPriorityHigh = (value != "0"); return true; }
    if (key == "PROTON_USE_NTSYNC") { s.protonUseNTSync = (value != "0"); return true; }
    if (key == "PROTON_LOG") { s.protonLog = (value != "0"); return true; }
    if (key == "MANGOHUD") { s.enableMangoHud = (value != "0"); return true; }
    if (key == "DXVK_FRAME_RATE") {
        s.enableFrameRateLimit = true;
        s.targetFrameRate = value.toInt();
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

    // Proton Tweaks
    if (settings.protonPriorityHigh) {
        envVars << "PROTON_PRIORITY_HIGH=1";
    }
    if (settings.protonUseNTSync) {
        envVars << "PROTON_USE_NTSYNC=1";
    }
    if (settings.protonLog) {
        envVars << "PROTON_LOG=1";
    }

    // Overlay
    if (settings.enableMangoHud) {
        envVars << "MANGOHUD=1";
    }

    // Custom launch parameters. If they contain "%command%", the custom text
    // controls placement of %command% and any trailing game arguments;
    // otherwise it is treated as extra env vars before an appended %command%.
    const QString custom = settings.customLaunchParams.trimmed();
    if (custom.contains("%command%")) {
        envVars << custom;
    } else {
        if (!custom.isEmpty()) {
            envVars << custom;
        }
        envVars << "%command%";
    }

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

    if (settings.enableReflex) {
        env.insert("DXVK_NVAPI_VKREFLEX", "1");
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

    // Frame Rate Limit
    if (settings.enableFrameRateLimit && settings.targetFrameRate > 0) {
        env.insert("DXVK_FRAME_RATE", QString::number(settings.targetFrameRate));
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

    // Proton Tweaks
    if (settings.protonPriorityHigh) {
        env.insert("PROTON_PRIORITY_HIGH", "1");
    }
    if (settings.protonUseNTSync) {
        env.insert("PROTON_USE_NTSYNC", "1");
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
            env.insert(token.left(eq), token.mid(eq + 1));
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
    s.dlssUpgrade = false;
    s.showIndicator = false;
    s.srOverride = false; s.srMode.clear(); s.srPreset.clear(); s.srScalingRatio = 0;
    s.rrOverride = false; s.rrMode.clear(); s.rrPreset.clear(); s.rrScalingRatio = 0;
    s.fgOverride = false; s.fgMultiFrameCount = 0; s.fgMode.clear(); s.fgPreset.clear();
    s.enableSmoothMotion = false;
    s.enableFrameRateLimit = false; s.targetFrameRate = 60;
    s.enableProtonWayland = false; s.enableProtonHDR = false; s.enableHDRWSI = false;
    s.protonPriorityHigh = false; s.protonUseNTSync = false; s.protonLog = false;
    s.enableMangoHud = false;

    QStringList leftover;
    bool seenCommand = false;
    const QStringList tokens = tokenize(raw);
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
        int eq = token.indexOf('=');
        if (eq > 0 && applyKnownEnvVar(s, token.left(eq), token.mid(eq + 1))) {
            continue;
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
