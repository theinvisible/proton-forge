#include "core/FeatureGate.h"

namespace FeatureGate {

const Requirement& requirementFor(Feature feature)
{
    // Central capability table. Driver thresholds are the meaningful gate;
    // Proton thresholds are coarse hints (compared by major version only).
    // Tune values here as drivers/Proton evolve.
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

    switch (feature) {
        case Feature::SmoothMotion:      return smoothMotion;
        case Feature::MultiFrameGen:     return multiFrameGen;
        case Feature::RayReconstruction: return rayReconstruction;
        case Feature::DlssgMode:         return dlssgMode;
        case Feature::FgPreset:          return fgPreset;
        case Feature::Reflex:            return reflex;
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
    }

    return {Status::Supported, {}};
}

}  // namespace FeatureGate
