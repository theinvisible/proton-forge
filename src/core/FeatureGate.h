#ifndef FEATUREGATE_H
#define FEATUREGATE_H

#include <QString>
#include <QVersionNumber>

// Declarative capability gating for NVIDIA/DLSS options.
//
// Each gateable feature carries a Requirement (minimum NVIDIA driver version,
// a coarse minimum Proton version, and an optional Proton upper bound for
// features that were later removed). Fork-specific features additionally
// carry per-fork minimum versions (Proton-CachyOS vs GE-Proton), compared at
// full precision. At runtime a Requirement is evaluated against a Context
// (the detected driver + the effective Proton version and fork) to decide
// whether to warn the user. The policy is intentionally lenient: an unknown
// driver, Proton version, or fork never produces a warning.
namespace FeatureGate {

enum class Fork {
    Unknown,
    CachyOS,
    GE,
};

struct Requirement {
    QVersionNumber minDriver;   // null = no driver requirement
    QVersionNumber minProton;   // null = no requirement (coarse, CachyOS-style major)
    QVersionNumber maxProton;   // null = no upper bound; feature removed beyond this
    QString note;               // human-readable, e.g. "NVIDIA driver ≥ 575.51"
    // Fork-specific rules, compared at full precision (the CachyOS datestamp
    // is the patch segment, e.g. 11.0.20260703). Null = no rule for that
    // fork. If at least one fork rule is set and the selected fork has none,
    // the feature is unavailable there (Status::WrongFork).
    QVersionNumber minCachyOS;
    QVersionNumber minGE;
    QString forkNote;           // message for WrongFork; falls back to note
};

struct Context {
    QVersionNumber driver;      // null = unknown -> driver check skipped
    QVersionNumber proton;      // null = unknown
    bool protonKnown = false;   // false -> Proton checks skipped (lenient)
    Fork fork = Fork::Unknown;  // Unknown -> fork checks skipped (lenient)
};

enum class Status {
    Supported,
    BelowMinDriver,
    BelowMinProton,
    Removed,
    WrongFork,                  // feature not available on the selected fork
};

struct Result {
    Status status = Status::Supported;
    QString message;            // empty when Supported
};

enum class Feature {
    SmoothMotion,
    MultiFrameGen,
    RayReconstruction,
    DlssgMode,
    FgPreset,
    Reflex,
    Vkd3dLowLatency,
    D7vk,
    DisableAutoHdr,
};

// Central, tunable capability table (defined in FeatureGate.cpp).
const Requirement& requirementFor(Feature feature);

// Evaluate a requirement against the current context.
Result evaluate(const Requirement& req, const Context& ctx);

}  // namespace FeatureGate

#endif // FEATUREGATE_H
