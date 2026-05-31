#ifndef FEATUREGATE_H
#define FEATUREGATE_H

#include <QString>
#include <QVersionNumber>

// Declarative capability gating for NVIDIA/DLSS options.
//
// Each gateable feature carries a Requirement (minimum NVIDIA driver version,
// a coarse minimum Proton version, and an optional Proton upper bound for
// features that were later removed). At runtime a Requirement is evaluated
// against a Context (the detected driver + the effective Proton version) to
// decide whether to warn the user. The policy is intentionally lenient: an
// unknown driver or Proton version never produces a warning.
namespace FeatureGate {

struct Requirement {
    QVersionNumber minDriver;   // null = no driver requirement
    QVersionNumber minProton;   // null = no requirement (coarse, CachyOS-style major)
    QVersionNumber maxProton;   // null = no upper bound; feature removed beyond this
    QString note;               // human-readable, e.g. "NVIDIA driver ≥ 575.51"
};

struct Context {
    QVersionNumber driver;      // null = unknown -> driver check skipped
    QVersionNumber proton;      // null = unknown
    bool protonKnown = false;   // false -> Proton checks skipped (lenient)
};

enum class Status {
    Supported,
    BelowMinDriver,
    BelowMinProton,
    Removed,
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
};

// Central, tunable capability table (defined in FeatureGate.cpp).
const Requirement& requirementFor(Feature feature);

// Evaluate a requirement against the current context.
Result evaluate(const Requirement& req, const Context& ctx);

}  // namespace FeatureGate

#endif // FEATUREGATE_H
