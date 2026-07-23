#include "core/FeatureGate.h"

namespace FeatureGate {

const Requirement& requirementFor(Feature feature)
{
    // Central capability table. Driver thresholds are the meaningful gate;
    // Proton thresholds are coarse hints (compared by major version only),
    // while fork-specific minimums (minCachyOS/minGE) compare at full
    // precision. Tune values here as drivers/Proton evolve.
    static const Requirement smoothMotion{
        QVersionNumber(575, 51, 2), {}, {},
        QStringLiteral("NVIDIA driver ≥ 575.51")};
    static const Requirement multiFrameGen{
        QVersionNumber(570), QVersionNumber(10), {},
        QStringLiteral("DLSS 4 (RTX 50): NVIDIA driver ≥ 570, Proton ≥ 10")};
    static const Requirement rayReconstruction{
        QVersionNumber(545), {}, {},
        QStringLiteral("NVIDIA driver ≥ 545")};
    static const Requirement dlssgMode{
        {}, QVersionNumber(10), {},
        QStringLiteral("Proton ≥ 10 (recent DXVK-NVAPI)")};
    static const Requirement fgPreset{
        {}, QVersionNumber(10), {},
        QStringLiteral("Proton ≥ 10 (recent DXVK-NVAPI)")};
    static const Requirement reflex{
        {}, QVersionNumber(9), {},
        QStringLiteral("Proton ≥ 9 (DXVK-NVAPI Reflex layer)")};
    static const Requirement vkd3dLowLatency{
        {}, {}, {},
        QStringLiteral("Proton-CachyOS ≥ 11.0-20260703 (VKD3D low latency)"),
        QVersionNumber(11, 0, 20260703), {},
        QStringLiteral("only available in Proton-CachyOS ≥ 11.0-20260703")};
    static const Requirement d7vk{
        {}, {}, {},
        QStringLiteral("Proton-CachyOS ≥ 11.0 or GE-Proton ≥ 11-1 (d7vk)"),
        QVersionNumber(11, 0), QVersionNumber(11, 1),
        {}};
    static const Requirement disableAutoHdr{
        {}, {}, {},
        QStringLiteral("auto-HDR requires Proton-CachyOS ≥ 11.0-20260601"),
        QVersionNumber(11, 0, 20260601), {},
        QStringLiteral("auto-HDR is Proton-CachyOS only — this option has no effect here")};
    static const Requirement vkd3dDescriptorHeap{
        QVersionNumber(595, 44, 2), QVersionNumber(11), {},
        QStringLiteral("NVIDIA driver ≥ 595.44.02, Proton ≥ 11 (vkd3d-proton descriptor heap)")};

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
    }

    static const Requirement none{};
    return none;
}

Result evaluate(const Requirement& req, const Context& ctx)
{
    // Driver is the hard signal; only flag when the driver is actually known.
    if (!req.minDriver.isNull() && !ctx.driver.isNull() &&
        ctx.driver < req.minDriver) {
        return {Status::BelowMinDriver,
                QStringLiteral("%1 (detected %2)").arg(req.note, ctx.driver.toString())};
    }

    // Proton checks are coarse and lenient: skipped entirely when unknown.
    if (ctx.protonKnown && !ctx.proton.isNull()) {
        if (!req.maxProton.isNull() &&
            ctx.proton.majorVersion() > req.maxProton.majorVersion()) {
            return {Status::Removed,
                    QStringLiteral("no longer available beyond Proton %1")
                        .arg(req.maxProton.majorVersion())};
        }
        if (!req.minProton.isNull() &&
            ctx.proton.majorVersion() < req.minProton.majorVersion()) {
            return {Status::BelowMinProton, req.note};
        }

        // Fork-specific rules: full-precision, evaluated only when the fork
        // is known (Unknown fork stays lenient, matching the overall policy).
        const bool hasForkRules = !req.minCachyOS.isNull() || !req.minGE.isNull();
        if (hasForkRules && ctx.fork != Fork::Unknown) {
            const QVersionNumber& forkMin =
                (ctx.fork == Fork::CachyOS) ? req.minCachyOS : req.minGE;
            if (forkMin.isNull()) {
                return {Status::WrongFork,
                        req.forkNote.isEmpty() ? req.note : req.forkNote};
            }
            if (ctx.proton < forkMin) {
                return {Status::BelowMinProton, req.note};
            }
        }
    }

    return {Status::Supported, {}};
}

}  // namespace FeatureGate
