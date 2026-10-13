#include "DLSSSettings.h"

#include <variant>
#include <vector>

namespace {

// Every persisted field, once. toJson, fromJson and operator== are loops over
// this, so a new field needs exactly one row here — a field missing from it is
// neither saved nor compared. The key is the JSON name; changing one breaks
// every existing settings.json.
struct Field {
    const char* key;
    std::variant<bool DLSSSettings::*, int DLSSSettings::*, QString DLSSSettings::*> member;
    bool omitIfEmpty = false;   // strings only: keep settings.json free of empty noise
};

const std::vector<Field>& fields()
{
    using S = DLSSSettings;
    static const std::vector<Field> table = {
        // General
        {"enableNVAPI",                 &S::enableNVAPI},
        {"enableNGXUpdater",            &S::enableNGXUpdater},
        {"enableReflex",                &S::enableReflex},
        {"enableVkd3dLowLatency",       &S::enableVkd3dLowLatency},
        {"enablePrimeRenderOffload",    &S::enablePrimeRenderOffload},
        {"enableVkd3dDescriptorHeap",   &S::enableVkd3dDescriptorHeap},
        {"enableD3D12ShaderExtensions", &S::enableD3D12ShaderExtensions},
        {"vkd3dConfigExtra",            &S::vkd3dConfigExtra, true},

        // Super Resolution
        {"srOverride",                  &S::srOverride},
        {"srMode",                      &S::srMode},
        {"srPreset",                    &S::srPreset},
        {"srScalingRatio",              &S::srScalingRatio},

        // Ray Reconstruction
        {"rrOverride",                  &S::rrOverride},
        {"rrMode",                      &S::rrMode},
        {"rrPreset",                    &S::rrPreset},
        {"rrScalingRatio",              &S::rrScalingRatio},

        // Frame Generation
        {"fgOverride",                  &S::fgOverride},
        {"fgMultiFrameCount",           &S::fgMultiFrameCount},
        {"fgMode",                      &S::fgMode},
        {"fgPreset",                    &S::fgPreset},
        {"fgDynamicTargetFps",          &S::fgDynamicTargetFps},
        {"fgDynamicMaxCount",           &S::fgDynamicMaxCount},

        // DLSS Upgrade
        {"dlssUpgrade",                 &S::dlssUpgrade},
        {"dlssVersion",                 &S::dlssVersion},

        // Indicators
        {"showIndicator",               &S::showIndicator},

        // Smooth Motion / frame rate
        {"enableSmoothMotion",          &S::enableSmoothMotion},
        {"smoothMotionGraphicsQueue",   &S::smoothMotionGraphicsQueue},
        {"enableFrameRateLimit",        &S::enableFrameRateLimit},
        {"targetFrameRate",             &S::targetFrameRate},
        {"dxvkConfigExtra",             &S::dxvkConfigExtra, true},

        // HDR
        {"enableProtonWayland",         &S::enableProtonWayland},
        {"enableProtonHDR",             &S::enableProtonHDR},
        {"enableHDRWSI",                &S::enableHDRWSI},
        {"disableAutoHDR",              &S::disableAutoHDR},

        // Proton tweaks
        {"protonUseNTSync",             &S::protonUseNTSync},
        {"protonDisableNTSync",         &S::protonDisableNTSync},
        {"protonNoSteamInput",          &S::protonNoSteamInput},
        {"protonPreferSdl",             &S::protonPreferSdl},
        {"protonUseD7VK",               &S::protonUseD7VK},
        {"protonLog",                   &S::protonLog},

        // Overlay
        {"enableSteamOverlay",          &S::enableSteamOverlay},
        {"enableMangoHud",              &S::enableMangoHud},

        // Selection and free-form parameters
        {"executablePath",              &S::executablePath, true},
        {"protonVersion",               &S::protonVersion, true},
        {"customLaunchParams",          &S::customLaunchParams, true},
    };
    return table;
}

// One visitor per operation, so each loop below stays a single line.
template <typename... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

}  // namespace

QJsonObject DLSSSettings::toJson() const
{
    QJsonObject json;
    for (const Field& f : fields()) {
        std::visit(Overloaded{
            [&](bool DLSSSettings::* m)    { json[f.key] = this->*m; },
            [&](int DLSSSettings::* m)     { json[f.key] = this->*m; },
            [&](QString DLSSSettings::* m) {
                if (!f.omitIfEmpty || !(this->*m).isEmpty()) json[f.key] = this->*m;
            },
        }, f.member);
    }
    return json;
}

DLSSSettings DLSSSettings::fromJson(const QJsonObject& json)
{
    // A missing key falls back to the struct's own default, so the defaults
    // live in exactly one place: the member initialisers in the header.
    DLSSSettings settings;
    for (const Field& f : fields()) {
        const QJsonValue v = json.value(QLatin1String(f.key));
        std::visit(Overloaded{
            [&](bool DLSSSettings::* m)    { settings.*m = v.toBool(settings.*m); },
            [&](int DLSSSettings::* m)     { settings.*m = v.toInt(settings.*m); },
            [&](QString DLSSSettings::* m) { settings.*m = v.toString(settings.*m); },
        }, f.member);
    }
    settings.adoptCustomScaling();
    return settings;
}

void DLSSSettings::adoptCustomScaling()
{
    const auto adopt = [](QString& mode, int& ratio) {
        if (ratio == 0)
            return;
        if (mode.isEmpty())
            mode = QStringLiteral("CUSTOM");
        ratio = qBound(33, ratio, 100);
    };
    adopt(srMode, srScalingRatio);
    adopt(rrMode, rrScalingRatio);
}

bool DLSSSettings::operator==(const DLSSSettings& other) const
{
    for (const Field& f : fields()) {
        const bool same = std::visit([&](auto m) { return this->*m == other.*m; }, f.member);
        if (!same) return false;
    }
    return true;
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
        "ULTRA_PERFORMANCE",
        "CUSTOM"
    };
}

QStringList DLSSSettings::availablePresets()
{
    // Super Resolution presets that exist, newest first. G/H/I ("do not use")
    // and N/O (reserved) were never real models and are left out; A-F stay
    // because a game that still loads an old DLL only knows those. DLSS 310
    // removed A-D and deprecated E/F — a preset the loaded DLL does not know
    // falls back to its default. A saved value missing here still round-trips
    // verbatim (EnvBuilder's canonicalize() leaves unknown values alone).
    return {
        "",  // Default/App controlled
        "RENDER_PRESET_LATEST",  // "Recommended": K/L/M per quality mode
        "RENDER_PRESET_M",
        "RENDER_PRESET_L",
        "RENDER_PRESET_K",
        "RENDER_PRESET_J",
        "RENDER_PRESET_F",
        "RENDER_PRESET_E",
        "RENDER_PRESET_D",
        "RENDER_PRESET_C",
        "RENDER_PRESET_B",
        "RENDER_PRESET_A"
    };
}

QStringList DLSSSettings::availableRRPresets()
{
    // Ray Reconstruction has its own, much shorter list: D and E are the
    // transformer models, F is Ray Reconstruction 2 (DLSS 310.9.1).
    return {
        "",  // Default/App controlled
        "RENDER_PRESET_LATEST",
        "RENDER_PRESET_F",
        "RENDER_PRESET_E",
        "RENDER_PRESET_D"
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
    // Frame Generation: A is the original model, B adds UI recomposition
    // (DLSS 310.6.0).
    return {
        "",  // Default/App controlled
        "RENDER_PRESET_LATEST",
        "RENDER_PRESET_B",
        "RENDER_PRESET_A"
    };
}
