#ifndef FEATUREWARNINGS_H
#define FEATUREWARNINGS_H

#include <QStringList>

#include "core/DLSSSettings.h"
#include "core/FeatureGate.h"
#include "core/Game.h"
#include "utils/CompatToolResolver.h"
#include "utils/GPUDetector.h"

struct GpuFacts;

// What the detected GPU, driver and Proton cannot do with a game's settings.
// Works on DLSSSettings rather than on the panel's controls, so the panel and
// the CLI report the same lines. Every gated option is one row in the rule
// table in FeatureWarnings.cpp; FeatureGate holds the requirements themselves.
namespace FeatureWarnings {

struct Input {
    FeatureGate::Context gate;
    GPUDetector::HybridGpu hybrid = GPUDetector::HybridGpu::Unknown;
    // A native Linux build reads no Proton-only variable, so those are not
    // checked at all — a warning about a variable nothing reads is noise.
    bool nativeLinux = false;
};

// The context a launch of `game` would meet: the GPU facts plus the Proton
// the launch path resolved (GameRunner::resolveProtonTool()).
Input inputFor(const GpuFacts& gpu, const CompatToolResolver::ToolInfo& proton,
               const Game& game);

// One line per problem, "Label: message", without any decoration.
QStringList collect(const DLSSSettings& settings, const Input& input);

}  // namespace FeatureWarnings

#endif // FEATUREWARNINGS_H
