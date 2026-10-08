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
        return r;
    }();
    static const Requirement frameGen = [] {
        Requirement r;
        r.minGpu = GpuGeneration::Ada;
        r.note = QStringLiteral("DLSS Frame Generation needs an RTX 40 or RTX 50 GPU");
        return r;
    }();
    static const Requirement multiFrameGen = [] {
        Requirement r;
        r.minDriver = QVersionNumber(570);
        r.minProton = QVersionNumber(10);
        r.minGpu = GpuGeneration::Blackwell;
        r.note = QStringLiteral("DLSS 4 (RTX 50): NVIDIA driver ≥ 570, Proton ≥ 10");
        r.gpuNote = QStringLiteral("Multi-Frame Generation needs an RTX 50 GPU");
        return r;
    }();
    static const Requirement rayReconstruction = [] {
        Requirement r;
        r.minDriver = QVersionNumber(545);
        r.note = QStringLiteral("NVIDIA driver ≥ 545");
        return r;
    }();
    // NGX_DLSSG_MODE and the FG preset key arrived in DXVK-NVAPI 0.9.2, which
    // Valve first shipped in Proton 11.0-2. The forks track DXVK-NVAPI master.
    static const Requirement dlssgMode = [] {
        Requirement r;
        r.minProton = QVersionNumber(10);
        r.minCachyOS = QVersionNumber(0);
        r.minGE = QVersionNumber(0);
        r.minValve = QVersionNumber(11, 0, 2);
        r.note = QStringLiteral("DXVK-NVAPI ≥ 0.9.2 (Proton ≥ 10, Valve Proton ≥ 11.0-2)");
        return r;
    }();
    static const Requirement fgPreset = dlssgMode;
    static const Requirement reflex = [] {
        Requirement r;
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
        // The flag exists only in vkd3d-proton master, which the forks and
        // Experimental ship but Valve's numbered 11.0 releases do not.
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
    static const Requirement dlssUpgrade = [] {
        Requirement r;
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

    switch (feature) {
        case Feature::SmoothMotion:      return smoothMotion;
        case Feature::MultiFrameGen:     return multiFrameGen;
        case Feature::RayReconstruction: return rayReconstruction;
        case Feature::DlssgMode:         return dlssgMode;
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
    // Hardware first: no driver or Proton makes a GPU run what it can't.
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
    }

    return {Status::Supported, {}};
}

}  // namespace FeatureGate
