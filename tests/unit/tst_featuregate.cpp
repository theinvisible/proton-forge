// FeatureGate decides when the DLSS UI shows a compatibility warning. Its
// stated policy is leniency — an unknown driver, Proton version or fork must
// never warn — because a false warning on a working setup is worse than a
// missing one. That policy is a handful of `if`s spread across one function,
// and it is exactly the kind of thing a well-meaning refactor tightens.

#include <QTest>

#include "core/FeatureGate.h"

using namespace FeatureGate;

class TstFeatureGate : public QObject
{
    Q_OBJECT

private slots:
    void everyFeatureHasARequirement();
    void unknownDriverNeverWarns();
    void unknownProtonNeverWarns();
    void unknownForkNeverWarns();
    void driverBelowMinimum();
    void driverAtMinimumIsFine();
    void protonComparesByMajorOnly();
    void removedBeyondMaxProton();
    void forkRulesUseFullPrecision();
    void wrongForkWhenNoRuleForIt();
    void noRequirementsAlwaysSupported();
    void valveIsItsOwnFork();
    void forkMinZeroMeansAnyVersion();
    void wrongForkWithoutKnownVersion();
    void unknownGpuNeverWarns();
    void gpuBelowMinimum();
    void perGpuDriverOverride();
    void generationFromArchitecture_data();
    void generationFromArchitecture();
    void realFeatures_data();
    void realFeatures();
};

void TstFeatureGate::everyFeatureHasARequirement()
{
    // requirementFor() ends in a fallthrough returning an empty Requirement.
    // A new enumerator that forgets its table entry would land there silently,
    // gating nothing — so assert every one of them carries something.
    const QList<Feature> all = {
        Feature::SmoothMotion, Feature::MultiFrameGen, Feature::RayReconstruction,
        Feature::DlssgMode, Feature::FgPreset, Feature::Reflex,
        Feature::Vkd3dLowLatency, Feature::D7vk, Feature::DisableAutoHdr,
        Feature::Vkd3dDescriptorHeap, Feature::FrameGen, Feature::NtsyncOptIn,
        Feature::NtsyncDisable, Feature::DlssUpgrade, Feature::DlssUpgradeVersion,
    };
    for (Feature f : all) {
        const Requirement& req = requirementFor(f);
        const bool hasSomething = !req.minDriver.isNull() || !req.minProton.isNull()
                                  || !req.maxProton.isNull() || !req.minCachyOS.isNull()
                                  || !req.minGE.isNull() || !req.minValve.isNull()
                                  || req.minGpu != GpuGeneration::Unknown;
        QVERIFY2(hasSomething, "a Feature has no requirement — missing table entry?");
        QVERIFY2(!req.note.isEmpty(), "a Feature has no explanatory note");
    }
}

void TstFeatureGate::unknownDriverNeverWarns()
{
    Requirement req;
    req.minDriver = QVersionNumber(999, 0);
    req.note = "needs a driver from the future";

    Context ctx;                  // driver is null == unknown
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
    QVERIFY(evaluate(req, ctx).message.isEmpty());
}

void TstFeatureGate::unknownProtonNeverWarns()
{
    Requirement req;
    req.minProton = QVersionNumber(99);
    req.note = "needs Proton 99";

    Context ctx;
    ctx.proton = QVersionNumber(9);
    ctx.protonKnown = false;      // e.g. "steam-proton" or an absolute path
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);

    // A known flag with a null version is equally unusable.
    Context nullVersion;
    nullVersion.protonKnown = true;
    QCOMPARE(evaluate(req, nullVersion).status, Status::Supported);
}

void TstFeatureGate::unknownForkNeverWarns()
{
    Requirement req;
    req.minCachyOS = QVersionNumber(11, 0, 20260703);
    req.forkNote = "CachyOS only";

    Context ctx;
    ctx.proton = QVersionNumber(9, 0);
    ctx.protonKnown = true;
    ctx.fork = Fork::Unknown;
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::driverBelowMinimum()
{
    Requirement req;
    req.minDriver = QVersionNumber(575, 51, 2);
    req.note = "NVIDIA driver >= 575.51";

    Context ctx;
    ctx.driver = QVersionNumber(550, 40);

    const Result r = evaluate(req, ctx);
    QCOMPARE(r.status, Status::BelowMinDriver);
    QVERIFY2(r.message.contains("550.40"), qPrintable(r.message));
    QVERIFY2(r.message.contains(req.note), qPrintable(r.message));
}

void TstFeatureGate::driverAtMinimumIsFine()
{
    Requirement req;
    req.minDriver = QVersionNumber(575, 51, 2);

    Context ctx;
    ctx.driver = QVersionNumber(575, 51, 2);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);

    ctx.driver = QVersionNumber(580);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::protonComparesByMajorOnly()
{
    Requirement req;
    req.minProton = QVersionNumber(10);
    req.note = "Proton >= 10";

    Context ctx;
    ctx.protonKnown = true;

    ctx.proton = QVersionNumber(9, 99, 99999999);
    QCOMPARE(evaluate(req, ctx).status, Status::BelowMinProton);

    // 10.0 with a lower minor still passes: only the major segment counts.
    ctx.proton = QVersionNumber(10, 0, 0);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::removedBeyondMaxProton()
{
    Requirement req;
    req.maxProton = QVersionNumber(9);

    Context ctx;
    ctx.protonKnown = true;

    ctx.proton = QVersionNumber(9, 5);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);

    ctx.proton = QVersionNumber(10, 0);
    const Result r = evaluate(req, ctx);
    QCOMPARE(r.status, Status::Removed);
    QVERIFY2(r.message.contains("9"), qPrintable(r.message));
}

void TstFeatureGate::forkRulesUseFullPrecision()
{
    // Proton-CachyOS encodes its build date in the patch segment, so unlike the
    // coarse minProton check this comparison has to look past the major.
    Requirement req;
    req.minCachyOS = QVersionNumber(11, 0, 20260703);
    req.note = "Proton-CachyOS >= 11.0-20260703";

    Context ctx;
    ctx.protonKnown = true;
    ctx.fork = Fork::CachyOS;

    ctx.proton = QVersionNumber(11, 0, 20260702);   // one day too old
    QCOMPARE(evaluate(req, ctx).status, Status::BelowMinProton);

    ctx.proton = QVersionNumber(11, 0, 20260703);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);

    ctx.proton = QVersionNumber(11, 0, 20260901);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::wrongForkWhenNoRuleForIt()
{
    Requirement req;
    req.minCachyOS = QVersionNumber(11, 0, 20260703);
    req.note = "generic note";
    req.forkNote = "only available in Proton-CachyOS";

    Context ctx;
    ctx.protonKnown = true;
    ctx.proton = QVersionNumber(11, 0);
    ctx.fork = Fork::GE;            // no minGE rule -> not available here

    const Result r = evaluate(req, ctx);
    QCOMPARE(r.status, Status::WrongFork);
    QCOMPARE(r.message, req.forkNote);

    // forkNote is optional; note is the fallback.
    req.forkNote.clear();
    QCOMPARE(evaluate(req, ctx).message, req.note);
}

void TstFeatureGate::noRequirementsAlwaysSupported()
{
    const Requirement req;
    Context ctx;
    ctx.driver = QVersionNumber(1, 0);
    ctx.proton = QVersionNumber(1, 0);
    ctx.protonKnown = true;
    ctx.fork = Fork::GE;
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::valveIsItsOwnFork()
{
    // Before Fork::Valve existed, every non-CachyOS fork read minGE. A Valve
    // build must not borrow GE's rule: d7vk is GE/CachyOS-only.
    Requirement req;
    req.minGE = QVersionNumber(11, 1);
    req.forkNote = "GE only";

    Context ctx;
    ctx.protonKnown = true;
    ctx.proton = QVersionNumber(11, 0, 2);
    ctx.fork = Fork::Valve;
    QCOMPARE(evaluate(req, ctx).status, Status::WrongFork);

    req.minValve = QVersionNumber(11, 0, 3);
    QCOMPARE(evaluate(req, ctx).status, Status::BelowMinProton);
    req.minValve = QVersionNumber(11, 0, 2);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::forkMinZeroMeansAnyVersion()
{
    // Adding a Valve rule must not turn the forks into WrongFork, so they
    // carry QVersionNumber(0): "any version of this fork".
    Requirement req;
    req.minValve = QVersionNumber(11, 0, 2);
    req.minGE = QVersionNumber(0);

    Context ctx;
    ctx.protonKnown = true;
    ctx.fork = Fork::GE;
    ctx.proton = QVersionNumber(9, 20);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);

    ctx.fork = Fork::CachyOS;        // no rule at all for CachyOS
    QCOMPARE(evaluate(req, ctx).status, Status::WrongFork);
}

void TstFeatureGate::wrongForkWithoutKnownVersion()
{
    // A Valve directory whose version file is missing still is Valve, and
    // still has no CachyOS-only feature.
    Requirement req;
    req.minCachyOS = QVersionNumber(11, 0, 20260601);
    req.forkNote = "CachyOS only";

    Context ctx;
    ctx.fork = Fork::Valve;
    ctx.protonKnown = false;
    QCOMPARE(evaluate(req, ctx).status, Status::WrongFork);

    // ...but an unknown version on the right fork stays silent.
    ctx.fork = Fork::CachyOS;
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::unknownGpuNeverWarns()
{
    Requirement req;
    req.minGpu = GpuGeneration::Blackwell;
    req.note = "RTX 50 only";

    Context ctx;                     // gpu Unknown
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::gpuBelowMinimum()
{
    Requirement req;
    req.minGpu = GpuGeneration::Ada;
    req.note = "generic";
    req.gpuNote = "needs RTX 40 or newer";

    Context ctx;
    ctx.gpu = GpuGeneration::Ampere;
    const Result r = evaluate(req, ctx);
    QCOMPARE(r.status, Status::UnsupportedGpu);
    QCOMPARE(r.message, req.gpuNote);

    ctx.gpu = GpuGeneration::Ada;
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
    ctx.gpu = GpuGeneration::Blackwell;
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::perGpuDriverOverride()
{
    // Smooth Motion: RTX 50 from 575.51.02, RTX 40 only from 580.82.07.
    const Requirement& req = requirementFor(Feature::SmoothMotion);

    Context ctx;
    ctx.driver = QVersionNumber(576, 0);
    ctx.gpu = GpuGeneration::Ada;
    QCOMPARE(evaluate(req, ctx).status, Status::BelowMinDriver);

    ctx.driver = QVersionNumber(580, 82, 7);
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);

    ctx.driver = QVersionNumber(575, 51, 2);
    ctx.gpu = GpuGeneration::Blackwell;
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);

    // Unknown GPU: held to the lowest minimum, never the stricter one.
    ctx.gpu = GpuGeneration::Unknown;
    QCOMPARE(evaluate(req, ctx).status, Status::Supported);
}

void TstFeatureGate::generationFromArchitecture_data()
{
    QTest::addColumn<QString>("architecture");
    QTest::addColumn<int>("expected");

    QTest::newRow("blackwell") << "Blackwell" << int(GpuGeneration::Blackwell);
    QTest::newRow("ada")       << "Ada Lovelace" << int(GpuGeneration::Ada);
    QTest::newRow("ampere")    << "Ampere" << int(GpuGeneration::Ampere);
    QTest::newRow("turing")    << "Turing" << int(GpuGeneration::Turing);
    QTest::newRow("pascal")    << "Pascal" << int(GpuGeneration::PreTuring);
    QTest::newRow("hopper")    << "Hopper" << int(GpuGeneration::Unknown);
    QTest::newRow("empty")     << "" << int(GpuGeneration::Unknown);
}

void TstFeatureGate::generationFromArchitecture()
{
    QFETCH(QString, architecture);
    QFETCH(int, expected);
    QCOMPARE(int(FeatureGate::generationFromArchitecture(architecture)), expected);
}

void TstFeatureGate::realFeatures_data()
{
    QTest::addColumn<int>("feature");
    QTest::addColumn<QVersionNumber>("driver");
    QTest::addColumn<QVersionNumber>("proton");
    QTest::addColumn<int>("fork");
    QTest::addColumn<int>("gpu");
    QTest::addColumn<int>("expected");

    const auto rowGpu = [](const char* name, Feature f, QVersionNumber driver,
                           QVersionNumber proton, Fork fork, GpuGeneration gpu,
                           Status expected) {
        QTest::newRow(name) << int(f) << driver << proton << int(fork) << int(gpu)
                            << int(expected);
    };
    const auto row = [&](const char* name, Feature f, QVersionNumber driver,
                         QVersionNumber proton, Fork fork, Status expected) {
        rowGpu(name, f, driver, proton, fork, GpuGeneration::Unknown, expected);
    };

    row("smooth motion on an old driver", Feature::SmoothMotion,
        QVersionNumber(550), {}, Fork::Unknown, Status::BelowMinDriver);
    row("smooth motion on a current driver", Feature::SmoothMotion,
        QVersionNumber(580, 10), {}, Fork::Unknown, Status::Supported);

    row("multi frame gen needs driver 570", Feature::MultiFrameGen,
        QVersionNumber(560), QVersionNumber(10), Fork::CachyOS, Status::BelowMinDriver);
    row("multi frame gen needs proton 10", Feature::MultiFrameGen,
        QVersionNumber(575), QVersionNumber(9), Fork::CachyOS, Status::BelowMinProton);

    row("vkd3d low latency on GE is the wrong fork", Feature::Vkd3dLowLatency,
        QVersionNumber(580), QVersionNumber(11, 0, 20260901), Fork::GE, Status::WrongFork);
    row("vkd3d low latency on new CachyOS", Feature::Vkd3dLowLatency,
        QVersionNumber(580), QVersionNumber(11, 0, 20260703), Fork::CachyOS, Status::Supported);
    row("vkd3d low latency on old CachyOS", Feature::Vkd3dLowLatency,
        QVersionNumber(580), QVersionNumber(11, 0, 20260101), Fork::CachyOS, Status::BelowMinProton);

    row("d7vk is available on both forks (GE)", Feature::D7vk,
        QVersionNumber(580), QVersionNumber(11, 1), Fork::GE, Status::Supported);
    row("d7vk is available on both forks (CachyOS)", Feature::D7vk,
        QVersionNumber(580), QVersionNumber(11, 0), Fork::CachyOS, Status::Supported);

    // Valve's numbered Proton: each fork-gated feature is a deliberate call.
    const QVersionNumber valve11_2(11, 0, 2);
    row("d7vk is not in Valve Proton", Feature::D7vk,
        QVersionNumber(580), valve11_2, Fork::Valve, Status::WrongFork);
    row("vkd3d low latency is not in Valve Proton", Feature::Vkd3dLowLatency,
        QVersionNumber(580), valve11_2, Fork::Valve, Status::WrongFork);
    row("auto-HDR opt-out is not in Valve Proton", Feature::DisableAutoHdr,
        QVersionNumber(580), valve11_2, Fork::Valve, Status::WrongFork);
    row("descriptor heap is not in Valve Proton 11.0", Feature::Vkd3dDescriptorHeap,
        QVersionNumber(610), valve11_2, Fork::Valve, Status::WrongFork);
    row("descriptor heap on new CachyOS", Feature::Vkd3dDescriptorHeap,
        QVersionNumber(610), QVersionNumber(11, 0, 20260703), Fork::CachyOS, Status::Supported);
    row("descriptor heap on old CachyOS", Feature::Vkd3dDescriptorHeap,
        QVersionNumber(610), QVersionNumber(11, 0, 20260428), Fork::CachyOS, Status::BelowMinProton);
    row("dlss upgrade is not in Valve Proton", Feature::DlssUpgrade,
        {}, valve11_2, Fork::Valve, Status::WrongFork);
    row("dlss upgrade on old GE", Feature::DlssUpgrade,
        {}, QVersionNumber(10, 25), Fork::GE, Status::BelowMinProton);
    row("dlss upgrade on any CachyOS", Feature::DlssUpgrade,
        {}, QVersionNumber(10, 0, 20250101), Fork::CachyOS, Status::Supported);
    row("dlssg mode on Valve 11.0-1", Feature::DlssgMode,
        {}, QVersionNumber(11, 0, 1), Fork::Valve, Status::BelowMinProton);
    row("dlssg mode on Valve 11.0-2", Feature::DlssgMode,
        {}, valve11_2, Fork::Valve, Status::Supported);
    row("fg preset on GE 10", Feature::FgPreset,
        {}, QVersionNumber(10, 34), Fork::GE, Status::Supported);
    row("ntsync opt-in is pointless on Proton 11", Feature::NtsyncOptIn,
        {}, valve11_2, Fork::Valve, Status::Removed);
    row("ntsync opt-in on Proton 10", Feature::NtsyncOptIn,
        {}, QVersionNumber(10, 34), Fork::GE, Status::Supported);
    row("ntsync opt-out needs Proton 11", Feature::NtsyncDisable,
        {}, QVersionNumber(10, 34), Fork::GE, Status::BelowMinProton);

    // GPU generation.
    rowGpu("smooth motion on RTX 30", Feature::SmoothMotion,
           QVersionNumber(610), {}, Fork::Unknown, GpuGeneration::Ampere, Status::UnsupportedGpu);
    rowGpu("multi frame gen on RTX 40", Feature::MultiFrameGen,
           QVersionNumber(610), QVersionNumber(11), Fork::CachyOS, GpuGeneration::Ada,
           Status::UnsupportedGpu);
    rowGpu("frame gen on RTX 40", Feature::FrameGen,
           QVersionNumber(610), {}, Fork::Unknown, GpuGeneration::Ada, Status::Supported);
    rowGpu("frame gen on RTX 20", Feature::FrameGen,
           QVersionNumber(610), {}, Fork::Unknown, GpuGeneration::Turing, Status::UnsupportedGpu);

    // The whole point of the policy: nothing known, nothing said.
    row("nothing known", Feature::Vkd3dDescriptorHeap,
        {}, {}, Fork::Unknown, Status::Supported);
}

void TstFeatureGate::realFeatures()
{
    QFETCH(int, feature);
    QFETCH(QVersionNumber, driver);
    QFETCH(QVersionNumber, proton);
    QFETCH(int, fork);
    QFETCH(int, gpu);
    QFETCH(int, expected);

    Context ctx;
    ctx.gpu = GpuGeneration(gpu);
    ctx.driver = driver;
    ctx.proton = proton;
    ctx.protonKnown = !proton.isNull();
    ctx.fork = Fork(fork);

    const Result r = evaluate(requirementFor(Feature(feature)), ctx);
    QCOMPARE(int(r.status), expected);
    if (r.status != Status::Supported) {
        QVERIFY2(!r.message.isEmpty(), "a warning without a message tells the user nothing");
    }
}

QTEST_MAIN(TstFeatureGate)
#include "tst_featuregate.moc"
