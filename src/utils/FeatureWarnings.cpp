#include "utils/FeatureWarnings.h"

#include <vector>

#include "utils/EnvBuilder.h"
#include "utils/GpuInfoCache.h"

namespace FeatureWarnings {

namespace {

using FeatureGate::Feature;
using S = DLSSSettings;

// The tabs, in order. Lines come out in the order a user meets the options.
enum class Section { General, Dlss, Hdr, Proton };

// One gated option: when `active` says it is switched on, `feature` is
// evaluated, and a native game skips it if only Proton reads `envVar`.
struct Rule {
    Section section;
    Feature feature;
    const char* label;
    const char* envVar;
    bool (*active)(const S&);
};

const std::vector<Rule>& rules()
{
    static const std::vector<Rule> table = {
        // General
        {Section::General, Feature::NgxUpdater, "NGX Updater", "PROTON_ENABLE_NGX_UPDATER",
         [](const S& s) { return s.enableNGXUpdater; }},
        {Section::General, Feature::Reflex, "Reflex", "DXVK_NVAPI_VKREFLEX",
         [](const S& s) { return s.enableReflex; }},
        {Section::General, Feature::Vkd3dLowLatency, "VKD3D Low Latency", "PROTON_VKD3D_LOWLATENCY",
         [](const S& s) { return s.enableVkd3dLowLatency; }},
        {Section::General, Feature::Vkd3dDescriptorHeap, "VKD3D Descriptor Heap", "VKD3D_CONFIG",
         [](const S& s) { return s.enableVkd3dDescriptorHeap; }},
        {Section::General, Feature::D3D12ShaderExtensions, "D3D12 Shader Extensions", "DXVK_NVAPI_D3D12_NV_SHADER_EXTN",
         [](const S& s) { return s.enableD3D12ShaderExtensions; }},
        {Section::General, Feature::DlssIndicator, "DLSS Indicator", "PROTON_DLSS_INDICATOR",
         [](const S& s) { return s.showIndicator; }},

        // DLSS — one line for what all three overrides share.
        {Section::Dlss, Feature::DlssOverrides, "DLSS overrides", "DXVK_NVAPI_DRS_SETTINGS",
         [](const S& s) { return s.srOverride || s.rrOverride || s.fgOverride; }},
        {Section::Dlss, Feature::SuperResolution, "Super Resolution", "DXVK_NVAPI_DRS_SETTINGS",
         [](const S& s) { return s.srOverride; }},
        {Section::Dlss, Feature::SmoothMotion, "Smooth Motion", "NVPRESENT_ENABLE_SMOOTH_MOTION",
         [](const S& s) { return s.enableSmoothMotion; }},
        {Section::Dlss, Feature::FrameGen, "Frame Generation", "DXVK_NVAPI_DRS_SETTINGS",
         [](const S& s) { return s.fgOverride; }},
        {Section::Dlss, Feature::MultiFrameGen, "Multi-Frame Generation (3x–6x)", "DXVK_NVAPI_DRS_SETTINGS",
         [](const S& s) { return s.fgOverride && s.fgMultiFrameCount >= 2; }},
        {Section::Dlss, Feature::RayReconstruction, "Ray Reconstruction", "DXVK_NVAPI_DRS_SETTINGS",
         [](const S& s) { return s.rrOverride; }},
        {Section::Dlss, Feature::DynamicFrameGen, "Dynamic Frame Generation", "DXVK_NVAPI_DRS_SETTINGS",
         [](const S& s) { return s.fgOverride && s.fgMode == QLatin1String("DYNAMIC"); }},
        {Section::Dlss, Feature::DlssgMode, "FG Mode", "DXVK_NVAPI_DRS_SETTINGS",
         [](const S& s) {
             return s.fgOverride && !s.fgMode.isEmpty() && s.fgMode != QLatin1String("DYNAMIC");
         }},
        {Section::Dlss, Feature::FgPreset, "FG Render Preset", "DXVK_NVAPI_DRS_SETTINGS",
         [](const S& s) { return s.fgOverride && !s.fgPreset.isEmpty(); }},
        {Section::Dlss, Feature::DlssUpgrade, "DLSS Upgrade", "PROTON_DLSS_UPGRADE",
         [](const S& s) { return s.dlssUpgrade && s.dlssVersion.trimmed().isEmpty(); }},
        {Section::Dlss, Feature::DlssUpgradeVersion, "DLSS version", "PROTON_DLSS_UPGRADE",
         [](const S& s) { return s.dlssUpgrade && !s.dlssVersion.trimmed().isEmpty(); }},

        // HDR
        {Section::Hdr, Feature::ProtonWayland, "Wayland", "PROTON_ENABLE_WAYLAND",
         [](const S& s) { return s.enableProtonWayland; }},
        {Section::Hdr, Feature::ProtonHdr, "HDR", "PROTON_ENABLE_HDR",
         [](const S& s) { return s.enableProtonHDR; }},
        {Section::Hdr, Feature::HdrWsiLayer, "HDR WSI layer", "ENABLE_HDR_WSI",
         [](const S& s) { return s.enableHDRWSI; }},
        {Section::Hdr, Feature::DisableAutoHdr, "Disable auto-HDR", "DXVK_NO_HDR",
         [](const S& s) { return s.disableAutoHDR; }},

        // Proton
        {Section::Proton, Feature::D7vk, "D7VK", "PROTON_USE_D7VK",
         [](const S& s) { return s.protonUseD7VK; }},
        {Section::Proton, Feature::NtsyncOptIn, "NTSync", "PROTON_USE_NTSYNC",
         [](const S& s) { return s.protonUseNTSync && !s.protonDisableNTSync; }},
        {Section::Proton, Feature::NtsyncDisable, "NTSync off", "PROTON_NO_NTSYNC",
         [](const S& s) { return s.protonDisableNTSync; }},
        {Section::Proton, Feature::NoSteamInput, "Controller without Steam Input", "PROTON_NO_STEAMINPUT",
         [](const S& s) { return s.protonNoSteamInput && !s.protonPreferSdl; }},
        {Section::Proton, Feature::PreferSdl, "Controller via SDL", "PROTON_PREFER_SDL",
         [](const S& s) { return s.protonPreferSdl; }},
    };
    return table;
}

bool reachesGame(const char* envVar, bool nativeLinux)
{
    return !nativeLinux ||
           EnvBuilder::scopeOf(QLatin1String(envVar)) != EnvBuilder::VarScope::ProtonOnly;
}

}  // namespace

Input inputFor(const GpuFacts& gpu, const CompatToolResolver::ToolInfo& proton,
               const Game& game)
{
    Input in;
    in.gate.driver = gpu.driver;
    in.gate.gpu = gpu.generation;
    in.gate.vendors = gpu.vendors;
    in.gate.proton = proton.version;
    in.gate.protonKnown = proton.isValid() && !proton.version.isNull();
    in.gate.fork = proton.isValid() ? CompatToolResolver::forkOf(proton.kind)
                                    : FeatureGate::Fork::Unknown;
    in.hybrid = gpu.hybrid;
    in.nativeLinux = game.isNativeLinux();
    return in;
}

QStringList collect(const DLSSSettings& s, const Input& in)
{
    using namespace FeatureGate;

    QStringList lines;
    // A missing vendor would otherwise repeat "needs an NVIDIA GPU" once per
    // option; it is one fact, so it is one line, ahead of the rest.
    QStringList wrongVendor;

    // The two checks that are not one feature each, at the end of their tab.
    auto endOfSection = [&](Section section) {
        if (section == Section::General) {
            // PRIME offload is gated by hardware, not driver/Proton versions:
            // warn only when the probe positively found a non-hybrid system
            // (Unknown stays silent).
            if (s.enablePrimeRenderOffload && in.hybrid == GPUDetector::HybridGpu::No) {
                lines << QStringLiteral(
                    "PRIME Render Offload: no hybrid iGPU + dGPU setup detected — "
                    "this option has no effect on this system");
            }
        } else if (section == Section::Dlss) {
            // Not a version gate but a combination NVIDIA advises against. Only
            // the FG override can be seen here — whether a game turns FG on by
            // itself cannot. Quiet on native titles, where the override is inert.
            if (s.enableSmoothMotion && s.fgOverride && s.fgMode != QLatin1String("OFF") &&
                reachesGame("DXVK_NVAPI_DRS_SETTINGS", in.nativeLinux)) {
                lines << QStringLiteral(
                    "Smooth Motion + DLSS Frame Generation: NVIDIA advises against "
                    "using both — Smooth Motion is meant for games without frame "
                    "generation of their own");
            }
        }
    };

    Section current = Section::General;
    for (const Rule& rule : rules()) {
        if (rule.section != current) {
            endOfSection(current);
            current = rule.section;
        }
        if (!rule.active(s) || !reachesGame(rule.envVar, in.nativeLinux))
            continue;
        const Result r = evaluate(requirementFor(rule.feature), in.gate);
        if (r.status == Status::Supported)
            continue;
        const QString label = QString::fromUtf8(rule.label);
        if (r.status == Status::WrongGpuVendor) {
            wrongVendor << label;
        } else {
            lines << QStringLiteral("%1: %2").arg(label, r.message);
        }
    }
    endOfSection(current);

    if (!wrongVendor.isEmpty()) {
        lines.prepend(QStringLiteral("No NVIDIA GPU detected — these options have no effect: %1")
                          .arg(wrongVendor.join(QStringLiteral(", "))));
    }
    return lines;
}

}  // namespace FeatureWarnings
