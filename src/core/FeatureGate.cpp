#include "core/FeatureGate.h"

namespace FeatureGate {

GpuGeneration generationFromArchitecture(const QString& architecture)
{
    const QString a = architecture.trimmed();
    if (a.isEmpty()) return GpuGeneration::Unknown;
    if (a.contains("Blackwell", Qt::CaseInsensitive)) return GpuGeneration::Blackwell;
    if (a.contains("Ada", Qt::CaseInsensitive))       return GpuGeneration::Ada;
    if (a.contains("Ampere", Qt::CaseInsensitive))    return GpuGeneration::Ampere;
    if (a.contains("Turing", Qt::CaseInsensitive))    return GpuGeneration::Turing;
    for (const char* old : {"Kepler", "Maxwell", "Pascal", "Volta"}) {
        if (a.contains(QLatin1String(old), Qt::CaseInsensitive))
            return GpuGeneration::PreTuring;
    }
    // Hopper and anything newer or unrecognised: no consumer RTX mapping we
    // can vouch for, so stay lenient.
    return GpuGeneration::Unknown;
}

const Requirement& requirementFor(Feature feature)
{
    // Central capability table. Driver thresholds are the meaningful gate;
    // Proton thresholds are coarse hints (compared by major version only),
    // while fork-specific minimums (minCachyOS/minGE/minValve) compare at full
    // precision. Tune values here as drivers/Proton evolve.
    static const Requirement smoothMotion = [] {
        Requirement r;
        // RTX 50 got it first (575.51.02); RTX 40 followed in 580.82.07.
        r.minDriver = QVersionNumber(575, 51, 2);
        r.minDriverFor[GpuGeneration::Ada] = QVersionNumber(580, 82, 7);
        r.minGpu = GpuGeneration::Ada;
        r.note = QStringLiteral("NVIDIA driver ≥ 575.51 (RTX 50) / ≥ 580.82 (RTX 40)");
        r.gpuNote = QStringLiteral("needs an RTX 40 or RTX 50 GPU");
        r.vendor = GpuVendor::NVIDIA;
        return r;
    }();
    static const Requirement frameGen = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minGpu = GpuGeneration::Ada;
        r.note = QStringLiteral("DLSS Frame Generation needs an RTX 40 or RTX 50 GPU");
        return r;
    }();
    static const Requirement multiFrameGen = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minDriver = QVersionNumber(570);
        r.minProton = QVersionNumber(10);
        r.minGpu = GpuGeneration::Blackwell;
        r.note = QStringLiteral("DLSS 4 (RTX 50): NVIDIA driver ≥ 570, Proton ≥ 10");
        r.gpuNote = QStringLiteral("Multi-Frame Generation needs an RTX 50 GPU");
        return r;
    }();
    static const Requirement rayReconstruction = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minGpu = GpuGeneration::Turing;
        r.minDriver = QVersionNumber(545);
        r.note = QStringLiteral("NVIDIA driver ≥ 545");
        r.gpuNote = QStringLiteral("DLSS needs an RTX GPU");
        return r;
    }();
    // Every SR/RR/FG override travels in DXVK_NVAPI_DRS_SETTINGS as a named
    // NGX_* key, which DXVK-NVAPI understands from a7d1f254 (2025-02-28, the
    // run-up to 0.9.0). Valve: 9.0-4 bundles afb59a8e (2024-11), 10.0-1 has
    // it. GE: 9-25 bundles 26f54ab8 (numeric DRS ids only), 9-26 has it.
    // CachyOS: 9.0-20250211 bundles 26f54ab8, 9.0-20250227 has 1e39a9d7.
    // Before that the variable is ignored and the overrides do nothing.
    static const Requirement dlssOverrides = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minValve = QVersionNumber(10, 0, 1);
        r.minGE = QVersionNumber(9, 26);
        r.minCachyOS = QVersionNumber(9, 0, 20250227);
        r.note = QStringLiteral("DLSS overrides need DXVK-NVAPI's named DRS settings "
                                "(Valve Proton ≥ 10.0-1, GE-Proton ≥ 9-26, "
                                "Proton-CachyOS ≥ 9.0-20250227)");
        return r;
    }();
    // The named NGX_DLSS_{SR,RR}_OVERRIDE_SCALING_RATIO keys came with DXVK-NVAPI
    // 0bcef80 (2025-05-25). Valve: 10.0-1f bundles e870088a, 10.0-2 has it. GE:
    // 10-3 bundles b0d47e7d, 10-4 has it. CachyOS: 10.0-20250520 bundles
    // b0d47e7d, 10.0-20250601 has it. Before that the ratio is ignored and
    // CUSTOM falls back to the driver's default scale.
    static const Requirement dlssScalingRatio = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minValve = QVersionNumber(10, 0, 2);
        r.minGE = QVersionNumber(10, 4);
        r.minCachyOS = QVersionNumber(10, 0, 20250601);
        r.note = QStringLiteral("a custom scaling ratio needs DXVK-NVAPI's named key "
                                "(Valve Proton ≥ 10.0-2, GE-Proton ≥ 10-4, "
                                "Proton-CachyOS ≥ 10.0-20250601)");
        return r;
    }();
    static const Requirement superResolution = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minGpu = GpuGeneration::Turing;
        r.note = QStringLiteral("DLSS needs an RTX GPU");
        return r;
    }();
    // NGX_DLSSG_MODE and the FG preset key arrived in DXVK-NVAPI 0.9.2, which
    // Valve first shipped in Proton 11.0-2. The forks track DXVK-NVAPI master.
    static const Requirement dlssgMode = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minProton = QVersionNumber(10);
        r.minCachyOS = QVersionNumber(0);
        r.minGE = QVersionNumber(0);
        r.minValve = QVersionNumber(11, 0, 2);
        r.note = QStringLiteral("DXVK-NVAPI ≥ 0.9.2 (Proton ≥ 10, Valve Proton ≥ 11.0-2)");
        return r;
    }();
    static const Requirement fgPreset = dlssgMode;
    // Dynamic MFG: the two NGX_DLSSG_DYNAMIC_* keys came with the same
    // DXVK-NVAPI 0.9.2, and the feature itself is RTX 50 only.
    static const Requirement dynamicFrameGen = [] {
        Requirement r = dlssgMode;
        r.minGpu = GpuGeneration::Blackwell;
        r.gpuNote = QStringLiteral("Dynamic Multi-Frame Generation needs an RTX 50 GPU");
        return r;
    }();
    // NVIDIA shader extensions in D3D12 (Shader Execution Reordering): reported
    // by DXVK-NVAPI 0.9.2, implemented by vkd3d-proton 3.0.1 — both in Valve's
    // Proton from 11.0-2, both tracked by the forks.
    static const Requirement d3d12ShaderExtensions = [] {
        Requirement r = dlssgMode;
        r.note = QStringLiteral("DXVK-NVAPI ≥ 0.9.2 and vkd3d-proton ≥ 3.0.1 "
                                "(Proton ≥ 10, Valve Proton ≥ 11.0-2)");
        return r;
    }();
    static const Requirement reflex = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minProton = QVersionNumber(9);
        r.note = QStringLiteral("Proton ≥ 9 (DXVK-NVAPI Reflex layer)");
        return r;
    }();
    static const Requirement vkd3dLowLatency = [] {
        Requirement r;
        r.minCachyOS = QVersionNumber(11, 0, 20260703);
        r.note = QStringLiteral("Proton-CachyOS ≥ 11.0-20260703 (VKD3D low latency)");
        r.forkNote = QStringLiteral("only available in Proton-CachyOS ≥ 11.0-20260703");
        return r;
    }();
    static const Requirement d7vk = [] {
        Requirement r;
        r.minCachyOS = QVersionNumber(11, 0);
        r.minGE = QVersionNumber(11, 1);
        r.note = QStringLiteral("Proton-CachyOS ≥ 11.0 or GE-Proton ≥ 11-1 (d7vk)");
        r.forkNote = QStringLiteral("d7vk is only bundled with Proton-CachyOS and GE-Proton");
        return r;
    }();
    static const Requirement disableAutoHdr = [] {
        Requirement r;
        r.minCachyOS = QVersionNumber(11, 0, 20260601);
        r.note = QStringLiteral("auto-HDR requires Proton-CachyOS ≥ 11.0-20260601");
        r.forkNote = QStringLiteral("auto-HDR is Proton-CachyOS only — this option has no effect here");
        return r;
    }();
    static const Requirement vkd3dDescriptorHeap = [] {
        Requirement r;
        // 595.44.02 is the Vulkan-beta numbering (595.44.x); production
        // drivers have it from 595.45.04. Game fixes landed in 610.57.04.
        r.minDriver = QVersionNumber(595, 44, 2);
        r.minProton = QVersionNumber(11);
        // The flag exists only in vkd3d-proton master (5f319d32, 2026-05-27),
        // which the forks and Experimental ship but Valve's numbered 11.0
        // releases do not. GE has it from 11-1 (vkd3d bee300e4) and no 10.x
        // does; CachyOS from 11.0-20260521 (vkd3d 110e8bd4).
        r.minCachyOS = QVersionNumber(11, 0, 20260521);
        r.minGE = QVersionNumber(11, 1);
        r.note = QStringLiteral("NVIDIA driver ≥ 595.44.02 (≥ 610.57 recommended), "
                                "Proton ≥ 11 with vkd3d-proton master");
        r.forkNote = QStringLiteral("not in Valve's Proton 11.0 — use Proton Experimental, "
                                    "Proton-CachyOS or GE-Proton");
        return r;
    }();
    static const Requirement ntsyncOptIn = [] {
        Requirement r;
        r.maxProton = QVersionNumber(10);
        r.note = QStringLiteral("NTSync is on by default from Proton 11 — forcing it does nothing");
        return r;
    }();
    static const Requirement ntsyncDisable = [] {
        Requirement r;
        r.minProton = QVersionNumber(11);
        r.note = QStringLiteral("PROTON_NO_NTSYNC needs Proton ≥ 11 (NTSync is opt-in before that)");
        return r;
    }();
    // GE: the proton script switches it on from 10-26, and umu-protonfixes'
    // upscalers.py reads any value other than 0/1 as the DLL version — an
    // unknown one falls back to the newest in its manifest. Valve: absent.
    static const Requirement dlssUpgrade = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minCachyOS = QVersionNumber(0);
        r.minGE = QVersionNumber(10, 26);
        r.note = QStringLiteral("PROTON_DLSS_UPGRADE needs Proton-CachyOS or GE-Proton ≥ 10-26");
        r.forkNote = QStringLiteral("Valve's Proton has no DLSS upgrade — use the NGX updater instead");
        return r;
    }();
    static const Requirement dlssUpgradeVersion = [] {
        Requirement r = dlssUpgrade;
        r.note = QStringLiteral("choosing a DLSS version needs Proton-CachyOS or GE-Proton ≥ 10-26");
        return r;
    }();

    // Controller input. PROTON_PREFER_SDL is in GE-Proton's script from 10-7
    // (absent at 10-6) and in Proton-CachyOS; PROTON_NO_STEAMINPUT from GE
    // 10-8, where the script acts on it on the Wayland path, while
    // Proton-CachyOS's winebus reads it directly ("winebus: Implement
    // PROTON_NO_STEAMINPUT support", 2026-04). Valve's Proton has neither —
    // checked in its script and its winebus.so (11.0, Experimental).
    static const Requirement preferSdl = [] {
        Requirement r;
        r.minCachyOS = QVersionNumber(0);
        r.minGE = QVersionNumber(10, 7);
        r.note = QStringLiteral("PROTON_PREFER_SDL needs Proton-CachyOS or GE-Proton ≥ 10-7");
        r.forkNote = QStringLiteral("Valve's Proton has no SDL input switch — use Proton-CachyOS or GE-Proton");
        return r;
    }();
    static const Requirement noSteamInput = [] {
        Requirement r;
        r.minCachyOS = QVersionNumber(0);
        r.minGE = QVersionNumber(10, 8);
        r.note = QStringLiteral("PROTON_NO_STEAMINPUT needs Proton-CachyOS, or GE-Proton ≥ 10-8 with Wayland");
        r.forkNote = QStringLiteral("Valve's Proton cannot hide Steam's virtual controller — use Proton-CachyOS or GE-Proton");
        return r;
    }();

    // The DLSS indicator switch is a GE / CachyOS invention, translated by
    // their scripts into DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS: GE from 10-26 (absent
    // at 10-25), CachyOS from 10.0-20250714 (absent at 10.0-20250702). Valve's
    // script has no such switch (11.0, Experimental). Writing the NGX variable
    // directly would reach Valve too, but it is stored in the prefix's registry
    // and stays on — only the forks reset it when the switch is off.
    static const Requirement dlssIndicator = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.minGE = QVersionNumber(10, 26);
        r.minCachyOS = QVersionNumber(10, 0, 20250714);
        r.note = QStringLiteral("PROTON_DLSS_INDICATOR needs GE-Proton ≥ 10-26 or "
                                "Proton-CachyOS ≥ 10.0-20250714");
        r.forkNote = QStringLiteral("Valve's Proton has no DLSS indicator switch — "
                                    "use Proton-CachyOS or GE-Proton");
        return r;
    }();
    // Read by the NVIDIA driver's own _nvngx.dll, not by Proton — so any
    // Proton will do, but only on NVIDIA.
    static const Requirement ngxUpdater = [] {
        Requirement r;
        r.vendor = GpuVendor::NVIDIA;
        r.note = QStringLiteral("the NGX updater is part of the NVIDIA driver");
        return r;
    }();
    // Wine's Wayland driver behind a switch: GE from 10-1 (none in 9-27),
    // CachyOS from 10.0-20250509 (none in 9.0-20250426); Valve's script has
    // none (11.0, Experimental). HDR rides on it: GE's PROTON_ENABLE_HDR and
    // CachyOS's DXVK_HDR only reach the display through winewayland.
    static const Requirement protonWayland = [] {
        Requirement r;
        r.minGE = QVersionNumber(10, 1);
        r.minCachyOS = QVersionNumber(10, 0, 20250509);
        r.note = QStringLiteral("PROTON_ENABLE_WAYLAND needs GE-Proton ≥ 10-1 or "
                                "Proton-CachyOS ≥ 10.0-20250509");
        r.forkNote = QStringLiteral("Valve's Proton has no Wayland switch — the game "
                                    "runs through XWayland");
        return r;
    }();
    // GE detects HDR per monitor from 11-9 and asks for PROTON_ENABLE_HDR to
    // be left off; it still reads it, as the legacy DXVK_HDR=1 opt-in.
    static const Requirement protonHdr = [] {
        Requirement r = protonWayland;
        r.note = QStringLiteral("HDR needs Proton's Wayland driver: GE-Proton ≥ 10-1 or "
                                "Proton-CachyOS ≥ 10.0-20250509");
        r.forkNote = QStringLiteral("Valve's Proton has no Wayland switch — HDR works "
                                    "there only inside gamescope");
        r.unneededFromFork[Fork::GE] = QVersionNumber(11, 9);
        r.unneededNote = QStringLiteral("not needed from GE-Proton 11-9, which detects HDR "
                                        "per monitor by itself — Wayland and HDR enabled in "
                                        "the desktop are enough; GE recommends leaving "
                                        "PROTON_ENABLE_HDR off");
        return r;
    }();
    // The Vulkan HDR layer fills a gap the NVIDIA driver closed in 595.
    static const Requirement hdrWsiLayer = [] {
        Requirement r;
        r.unneededFromDriver = QVersionNumber(595);
        r.note = QStringLiteral("ENABLE_HDR_WSI is only needed before NVIDIA driver 595");
        r.unneededNote = QStringLiteral("not needed from NVIDIA driver 595 — the driver "
                                        "exposes HDR to Vulkan itself");
        return r;
    }();

    switch (feature) {
        case Feature::SmoothMotion:      return smoothMotion;
        case Feature::MultiFrameGen:     return multiFrameGen;
        case Feature::RayReconstruction: return rayReconstruction;
        case Feature::DlssgMode:         return dlssgMode;
        case Feature::DynamicFrameGen:   return dynamicFrameGen;
        case Feature::D3D12ShaderExtensions: return d3d12ShaderExtensions;
        case Feature::FgPreset:          return fgPreset;
        case Feature::Reflex:            return reflex;
        case Feature::Vkd3dLowLatency:   return vkd3dLowLatency;
        case Feature::D7vk:              return d7vk;
        case Feature::DisableAutoHdr:    return disableAutoHdr;
        case Feature::Vkd3dDescriptorHeap: return vkd3dDescriptorHeap;
        case Feature::FrameGen:          return frameGen;
        case Feature::NtsyncOptIn:       return ntsyncOptIn;
        case Feature::NtsyncDisable:     return ntsyncDisable;
        case Feature::DlssUpgrade:       return dlssUpgrade;
        case Feature::DlssUpgradeVersion: return dlssUpgradeVersion;
        case Feature::NoSteamInput:      return noSteamInput;
        case Feature::PreferSdl:         return preferSdl;
        case Feature::DlssOverrides:     return dlssOverrides;
        case Feature::SuperResolution:   return superResolution;
        case Feature::DlssIndicator:     return dlssIndicator;
        case Feature::NgxUpdater:        return ngxUpdater;
        case Feature::ProtonWayland:     return protonWayland;
        case Feature::ProtonHdr:         return protonHdr;
        case Feature::HdrWsiLayer:       return hdrWsiLayer;
        case Feature::DlssScalingRatio:  return dlssScalingRatio;
    }

    static const Requirement none{};
    return none;
}

namespace {

const QVersionNumber& forkMinimum(const Requirement& req, Fork fork)
{
    static const QVersionNumber none;
    switch (fork) {
        case Fork::CachyOS: return req.minCachyOS;
        case Fork::GE:      return req.minGE;
        case Fork::Valve:   return req.minValve;
        case Fork::Unknown: break;
    }
    return none;
}

}  // namespace

Result evaluate(const Requirement& req, const Context& ctx)
{
    // Hardware first: no driver or Proton makes a GPU run what it can't. A
    // vendor counts as missing only when the detected list is non-empty.
    if (req.vendor && !ctx.vendors.isEmpty() && !ctx.vendors.contains(*req.vendor)) {
        QString message = req.vendorNote;
        if (message.isEmpty()) {
            message = *req.vendor == GpuVendor::NVIDIA ? QStringLiteral("needs an NVIDIA GPU")
                    : *req.vendor == GpuVendor::AMD    ? QStringLiteral("needs an AMD GPU")
                                                       : QStringLiteral("needs an Intel GPU");
        }
        return {Status::WrongGpuVendor, message};
    }
    if (req.minGpu != GpuGeneration::Unknown && ctx.gpu != GpuGeneration::Unknown &&
        ctx.gpu < req.minGpu) {
        return {Status::UnsupportedGpu, req.gpuNote.isEmpty() ? req.note : req.gpuNote};
    }

    // Driver is the hard signal; only flag when the driver is actually known.
    // A per-generation minimum applies only when the generation is known, so an
    // unknown GPU is held to the lowest (most lenient) minimum.
    const QVersionNumber minDriver = ctx.gpu != GpuGeneration::Unknown
                                         ? req.minDriverFor.value(ctx.gpu, req.minDriver)
                                         : req.minDriver;
    if (!minDriver.isNull() && !ctx.driver.isNull() && ctx.driver < minDriver) {
        return {Status::BelowMinDriver,
                QStringLiteral("%1 (detected %2)").arg(req.note, ctx.driver.toString())};
    }
    if (!req.unneededFromDriver.isNull() && !ctx.driver.isNull() &&
        ctx.driver >= req.unneededFromDriver) {
        return {Status::Unneeded,
                QStringLiteral("%1 (detected %2)")
                    .arg(req.unneededNote.isEmpty() ? req.note : req.unneededNote,
                         ctx.driver.toString())};
    }

    // Fork availability needs only the fork, not its version: a Valve build
    // whose version file is unreadable still has no d7vk.
    const bool hasForkRules = !req.minCachyOS.isNull() || !req.minGE.isNull() ||
                              !req.minValve.isNull();
    const QVersionNumber& forkMin = forkMinimum(req, ctx.fork);
    if (hasForkRules && ctx.fork != Fork::Unknown && forkMin.isNull()) {
        return {Status::WrongFork, req.forkNote.isEmpty() ? req.note : req.forkNote};
    }

    // Proton checks are coarse and lenient: skipped entirely when unknown.
    if (ctx.protonKnown && !ctx.proton.isNull()) {
        if (!req.maxProton.isNull() &&
            ctx.proton.majorVersion() > req.maxProton.majorVersion()) {
            QString message = QStringLiteral("no longer available beyond Proton %1")
                                  .arg(req.maxProton.majorVersion());
            if (!req.note.isEmpty())
                message += QStringLiteral(" — ") + req.note;
            return {Status::Removed, message};
        }
        if (!req.minProton.isNull() &&
            ctx.proton.majorVersion() < req.minProton.majorVersion()) {
            return {Status::BelowMinProton, req.note};
        }

        // Fork-specific minimum: full precision, only when the fork is known.
        if (hasForkRules && ctx.fork != Fork::Unknown && ctx.proton < forkMin) {
            return {Status::BelowMinProton, req.note};
        }

        // Last, so a Proton that cannot run the option says so first.
        const QVersionNumber unneededFrom = req.unneededFromFork.value(ctx.fork);
        if (!unneededFrom.isNull() && ctx.proton >= unneededFrom) {
            return {Status::Unneeded, req.unneededNote.isEmpty() ? req.note : req.unneededNote};
        }
    }

    return {Status::Supported, {}};
}

}  // namespace FeatureGate
