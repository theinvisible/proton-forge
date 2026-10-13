// FeatureWarnings turns a game's settings into the lines of the panel's warning
// banner and the CLI's featureWarnings. It replaced a check that read the
// panel's controls, so the first group of tests pins the texts and the
// conditions the banner already had: a user must not notice the move. The
// rest covers what the controls could never tell — a native game seen from
// the CLI, a machine without NVIDIA, a Proton too old for the overrides.

#include <QTest>

#include "utils/FeatureWarnings.h"
#include "utils/GpuInfoCache.h"

using namespace FeatureGate;

class TstFeatureWarnings : public QObject
{
    Q_OBJECT

private slots:
    void nothingOnSaysNothing();
    void keepsTheBannerTexts();
    void nativeGameSkipsProtonOnlyOptions();
    void frameGenDetailsNeedTheOverride();
    void missingVendorIsOneLine();
    void oldProtonIsOneOverridesLine();
    void linesFollowTheTabs();
    void inputForUsesTheResolvedProton();
};

namespace {

FeatureWarnings::Input valve(int major, int minor, int patch)
{
    FeatureWarnings::Input in;
    in.gate.proton = QVersionNumber(major, minor, patch);
    in.gate.protonKnown = true;
    in.gate.fork = Fork::Valve;
    return in;
}

}  // namespace

void TstFeatureWarnings::nothingOnSaysNothing()
{
    FeatureWarnings::Input in = valve(9, 0, 4);
    in.gate.vendors = {GpuVendor::AMD};
    in.hybrid = GPUDetector::HybridGpu::No;
    QVERIFY(FeatureWarnings::collect(DLSSSettings(), in).isEmpty());
}

void TstFeatureWarnings::keepsTheBannerTexts()
{
    DLSSSettings s;
    s.protonPreferSdl = true;
    s.protonUseNTSync = true;
    s.enableSmoothMotion = true;
    s.fgOverride = true;
    s.enablePrimeRenderOffload = true;

    FeatureWarnings::Input in = valve(11, 0, 2);
    in.hybrid = GPUDetector::HybridGpu::No;

    QCOMPARE(FeatureWarnings::collect(s, in), (QStringList{
        "PRIME Render Offload: no hybrid iGPU + dGPU setup detected — "
        "this option has no effect on this system",
        "Smooth Motion + DLSS Frame Generation: NVIDIA advises against using both — "
        "Smooth Motion is meant for games without frame generation of their own",
        "NTSync: no longer available beyond Proton 10 — NTSync is on by default from "
        "Proton 11 — forcing it does nothing",
        "Controller via SDL: Valve's Proton has no SDL input switch — use "
        "Proton-CachyOS or GE-Proton",
    }));
}

void TstFeatureWarnings::nativeGameSkipsProtonOnlyOptions()
{
    DLSSSettings s;
    s.protonPreferSdl = true;      // Proton-only
    s.srOverride = true;           // DXVK-NVAPI, Proton-only
    s.fgOverride = true;
    s.enableSmoothMotion = true;   // driver level: reaches a native game too

    FeatureWarnings::Input in = valve(9, 0, 4);
    in.gate.driver = QVersionNumber(550);
    in.nativeLinux = true;

    const QStringList lines = FeatureWarnings::collect(s, in);
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.first().startsWith(QStringLiteral("Smooth Motion: NVIDIA driver")));
}

void TstFeatureWarnings::frameGenDetailsNeedTheOverride()
{
    // Multi-frame count and mode survive in the settings with the override
    // off; they reach nothing then, and the banner never checked them.
    DLSSSettings s;
    s.fgMultiFrameCount = 3;
    s.fgMode = QStringLiteral("DYNAMIC");
    s.fgPreset = QStringLiteral("RENDER_PRESET_B");

    FeatureWarnings::Input in = valve(10, 0, 3);
    in.gate.gpu = GpuGeneration::Ampere;
    QVERIFY(FeatureWarnings::collect(s, in).isEmpty());

    s.fgOverride = true;
    const QStringList lines = FeatureWarnings::collect(s, in);
    QVERIFY(lines.contains(QStringLiteral(
        "Multi-Frame Generation (3x–6x): Multi-Frame Generation needs an RTX 50 GPU")));
    QVERIFY(lines.contains(QStringLiteral(
        "Dynamic Frame Generation: Dynamic Multi-Frame Generation needs an RTX 50 GPU")));
}

void TstFeatureWarnings::missingVendorIsOneLine()
{
    DLSSSettings s;
    s.srOverride = true;
    s.fgOverride = true;
    s.enableReflex = true;
    s.protonPreferSdl = true;      // vendor-neutral: unaffected

    FeatureWarnings::Input in;
    in.gate.vendors = {GpuVendor::AMD};

    QCOMPARE(FeatureWarnings::collect(s, in), (QStringList{
        "No NVIDIA GPU detected — these options have no effect: Reflex, DLSS overrides, "
        "Super Resolution, Frame Generation",
    }));
}

void TstFeatureWarnings::oldProtonIsOneOverridesLine()
{
    DLSSSettings s;
    s.srOverride = true;
    s.rrOverride = true;

    QCOMPARE(FeatureWarnings::collect(s, valve(9, 0, 4)), (QStringList{
        "DLSS overrides: DLSS overrides need DXVK-NVAPI's named DRS settings "
        "(Valve Proton ≥ 10.0-1, GE-Proton ≥ 9-26, Proton-CachyOS ≥ 9.0-20250227)",
    }));
    QVERIFY(FeatureWarnings::collect(s, valve(10, 0, 1)).isEmpty());
}

void TstFeatureWarnings::linesFollowTheTabs()
{
    DLSSSettings s;
    s.showIndicator = true;        // General
    s.enableProtonHDR = true;      // HDR
    s.protonNoSteamInput = true;   // Proton

    const QStringList lines = FeatureWarnings::collect(s, valve(11, 0, 2));
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines.at(0).startsWith(QStringLiteral("DLSS Indicator: Valve's Proton")));
    QVERIFY(lines.at(1).startsWith(QStringLiteral("HDR: Valve's Proton has no Wayland switch")));
    QVERIFY(lines.at(2).startsWith(QStringLiteral("Controller without Steam Input:")));
}

void TstFeatureWarnings::inputForUsesTheResolvedProton()
{
    GpuFacts gpu;
    gpu.driver = QVersionNumber(615, 71, 9);
    gpu.generation = GpuGeneration::Blackwell;
    gpu.vendors = {GpuVendor::NVIDIA};
    gpu.hybrid = GPUDetector::HybridGpu::Yes;

    CompatToolResolver::ToolInfo proton;
    proton.path = QStringLiteral("/steam/compatibilitytools.d/GE-Proton10-26");
    proton.kind = CompatToolResolver::Kind::GE;
    proton.version = QVersionNumber(10, 26);

    Game game(QStringLiteral("1001"), QStringLiteral("Test"), QStringLiteral("Steam"));
    game.setIsNativeLinux(true);

    const FeatureWarnings::Input in = FeatureWarnings::inputFor(gpu, proton, game);
    QCOMPARE(in.gate.driver, gpu.driver);
    QCOMPARE(in.gate.gpu, GpuGeneration::Blackwell);
    QCOMPARE(in.gate.vendors, gpu.vendors);
    QCOMPARE(in.gate.fork, Fork::GE);
    QCOMPARE(in.gate.proton, QVersionNumber(10, 26));
    QVERIFY(in.gate.protonKnown);
    QCOMPARE(in.hybrid, GPUDetector::HybridGpu::Yes);
    QVERIFY(in.nativeLinux);

    // No Proton resolved: nothing about it is assumed.
    const FeatureWarnings::Input none =
        FeatureWarnings::inputFor(gpu, CompatToolResolver::ToolInfo(), game);
    QVERIFY(!none.gate.protonKnown);
    QCOMPARE(none.gate.fork, Fork::Unknown);
}

QTEST_MAIN(TstFeatureWarnings)
#include "tst_featurewarnings.moc"
