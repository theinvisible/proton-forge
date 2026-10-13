#ifndef FEATUREGATE_H
#define FEATUREGATE_H

#include <QMap>
#include <QString>
#include <QVersionNumber>

// Declarative capability gating for NVIDIA/DLSS options.
//
// Each gateable feature carries a Requirement (minimum GPU generation,
// minimum NVIDIA driver version — optionally per GPU generation — a coarse
// minimum Proton version, and an optional Proton upper bound for features
// that were later removed). Fork-specific features additionally carry
// per-fork minimum versions (Proton-CachyOS, GE-Proton, Valve's Proton),
// compared at full precision. At runtime a Requirement is evaluated against a
// Context (the detected GPU and driver + the effective Proton version and
// fork) to decide whether to warn the user. The policy is intentionally
// lenient: an unknown GPU, driver, Proton version, or fork never produces a
// warning.
namespace FeatureGate {

enum class Fork {
    Unknown,
    CachyOS,
    GE,
    Valve,      // a numbered Valve release ("Proton 11.0"); Experimental is Unknown
};

// Ordered oldest to newest so that `<` means "older than".
enum class GpuGeneration {
    Unknown,
    PreTuring,  // Maxwell, Pascal, Volta, … — no RTX features at all
    Turing,     // RTX 20
    Ampere,     // RTX 30
    Ada,        // RTX 40
    Blackwell,  // RTX 50
};

// Maps an architecture name as GPUDetector reports it ("Ada Lovelace",
// "Blackwell", …) to a generation. Anything unrecognised is Unknown, which
// the gate treats leniently.
GpuGeneration generationFromArchitecture(const QString& architecture);

struct Requirement {
    QVersionNumber minDriver;   // null = no driver requirement
    QVersionNumber minProton;   // null = no requirement (coarse, CachyOS-style major)
    QVersionNumber maxProton;   // null = no upper bound; feature removed beyond this
    QString note;               // human-readable, e.g. "NVIDIA driver ≥ 575.51"
    // Fork-specific rules, compared at full precision (the CachyOS datestamp
    // is the patch segment, e.g. 11.0.20260703). Null = no rule for that
    // fork; QVersionNumber(0) = any version of that fork. If at least one
    // fork rule is set and the selected fork has none, the feature is
    // unavailable there (Status::WrongFork).
    QVersionNumber minCachyOS;
    QVersionNumber minGE;
    QString forkNote;           // message for WrongFork; falls back to note
    QVersionNumber minValve;    // e.g. 11.0.2 for "Proton 11.0-2"
    // Hardware: the oldest GPU generation the feature runs on, and driver
    // minimums that differ per generation (falls back to minDriver).
    GpuGeneration minGpu = GpuGeneration::Unknown;
    QMap<GpuGeneration, QVersionNumber> minDriverFor;
    QString gpuNote;            // message for UnsupportedGpu; falls back to note
};

struct Context {
    QVersionNumber driver;      // null = unknown -> driver check skipped
    QVersionNumber proton;      // null = unknown
    bool protonKnown = false;   // false -> Proton version checks skipped (lenient)
    Fork fork = Fork::Unknown;  // Unknown -> fork checks skipped (lenient)
    GpuGeneration gpu = GpuGeneration::Unknown;  // Unknown -> GPU check skipped
};

enum class Status {
    Supported,
    BelowMinDriver,
    BelowMinProton,
    Removed,
    WrongFork,                  // feature not available on the selected fork
    UnsupportedGpu,             // the GPU generation cannot run it
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
    DynamicFrameGen,
    D3D12ShaderExtensions,
    FgPreset,
    Reflex,
    Vkd3dLowLatency,
    D7vk,
    DisableAutoHdr,
    Vkd3dDescriptorHeap,
    FrameGen,
    NtsyncOptIn,
    NtsyncDisable,
    DlssUpgrade,
    DlssUpgradeVersion,
    NoSteamInput,
    PreferSdl,
};

// Central, tunable capability table (defined in FeatureGate.cpp).
const Requirement& requirementFor(Feature feature);

// Evaluate a requirement against the current context.
Result evaluate(const Requirement& req, const Context& ctx);

}  // namespace FeatureGate

#endif // FEATUREGATE_H
