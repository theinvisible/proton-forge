// EnvBuilder is the bridge between DLSSSettings and Steam's launch-options
// string, in both directions, and its header states the contract outright:
// parseLaunchOptions is the inverse of buildLaunchOptions. Nothing enforced
// that until now — and it is the one place in the codebase where a change has
// to be made twice or it silently loses a user's setting on the next import.

#include <QTest>
#include <QJsonObject>
#include <QProcessEnvironment>

#include "core/DLSSSettings.h"
#include "utils/EnvBuilder.h"

// Needed to hand whole settings objects to QTest's data tables.
Q_DECLARE_METATYPE(DLSSSettings)

class TstEnvBuilder : public QObject
{
    Q_OBJECT

private slots:
    void buildsNothingByDefault();
    void emitsKnownVariables_data();
    void emitsKnownVariables();
    void roundTrips_data();
    void roundTrips();
    void absenceMeansOff();
    void primeTrioIsAllOrNothing();
    void preservesCommandTail();
    void keepsUnknownTokens();
    void drsSettings_data();
    void drsSettings();
    void scalingRatioIsRangeChecked();
    void customGameArgs_data();
    void customGameArgs();
    void environmentCarriesTheSameVariables();
    void mangoHudIsAWrapperNotAVariable();
    void mangoHudFollowsEveryEnvVar();
    void mangoHudWrapsAUserWrapper();
    void legacyMangoHudVariableStillParses();
    void customWrapper_data();
    void customWrapper();

private:
    // parseLaunchOptions folds unrecognised tokens into customParams, which the
    // caller then assigns to the settings object (see MainWindow's import path).
    // Doing the same here is what makes the round-trip comparable.
    static DLSSSettings reparse(const QString& raw, const DLSSSettings& base)
    {
        const EnvBuilder::ParsedLaunchOptions parsed = EnvBuilder::parseLaunchOptions(raw, base);
        DLSSSettings out = parsed.settings;
        out.customLaunchParams = parsed.customParams;
        return out;
    }
};

void TstEnvBuilder::buildsNothingByDefault()
{
    // enableNVAPI defaults to true, so a default object is not empty — pinning
    // this because it is the baseline every other expectation is relative to.
    const QString built = EnvBuilder::buildLaunchOptions(DLSSSettings());
    QVERIFY(built.contains("PROTON_ENABLE_NVAPI=1"));
    QVERIFY(built.endsWith("%command%"));
}

void TstEnvBuilder::emitsKnownVariables_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QString>("expected");

    QTest::newRow("ngx updater")     << "enableNGXUpdater"        << "PROTON_ENABLE_NGX_UPDATER=1";
    QTest::newRow("reflex")          << "enableReflex"            << "DXVK_NVAPI_VKREFLEX=1";
    QTest::newRow("vkd3d lowlatency")<< "enableVkd3dLowLatency"   << "PROTON_VKD3D_LOWLATENCY=1";
    QTest::newRow("dlss upgrade")    << "dlssUpgrade"             << "PROTON_DLSS_UPGRADE=1";
    QTest::newRow("indicator")       << "showIndicator"           << "PROTON_DLSS_INDICATOR=1";
    QTest::newRow("smooth motion")   << "enableSmoothMotion"      << "NVPRESENT_ENABLE_SMOOTH_MOTION=1";
    QTest::newRow("wayland")         << "enableProtonWayland"     << "PROTON_ENABLE_WAYLAND=1";
    QTest::newRow("hdr")             << "enableProtonHDR"         << "PROTON_ENABLE_HDR=1";
    QTest::newRow("hdr wsi")         << "enableHDRWSI"            << "ENABLE_HDR_WSI=1";
    QTest::newRow("no auto hdr")     << "disableAutoHDR"          << "DXVK_NO_HDR=1";
    QTest::newRow("priority")        << "protonPriorityHigh"      << "PROTON_PRIORITY_HIGH=1";
    QTest::newRow("ntsync")          << "protonUseNTSync"         << "PROTON_USE_NTSYNC=1";
    QTest::newRow("d7vk")            << "protonUseD7VK"           << "PROTON_USE_D7VK=1";
    QTest::newRow("proton log")      << "protonLog"               << "PROTON_LOG=1";
    // Not an env var: MANGOHUD=1 only enables the Vulkan implicit layer, so the
    // launch options carry the wrapper command that also covers OpenGL.
    QTest::newRow("mangohud")        << "enableMangoHud"          << "mangohud";
}

void TstEnvBuilder::emitsKnownVariables()
{
    QFETCH(QString, field);
    QFETCH(QString, expected);

    // Set the one boolean by name through the JSON representation, so this
    // table does not have to know the C++ member.
    QJsonObject json = DLSSSettings().toJson();
    QVERIFY2(json.contains(field), qPrintable("no such field: " + field));
    json[field] = true;
    const DLSSSettings settings = DLSSSettings::fromJson(json);

    const QString built = EnvBuilder::buildLaunchOptions(settings);
    QVERIFY2(built.contains(expected), qPrintable(field + " -> missing " + expected + " in: " + built));

    // And it must be absent again when the flag is off.
    json[field] = false;
    const QString without = EnvBuilder::buildLaunchOptions(DLSSSettings::fromJson(json));
    QVERIFY2(!without.contains(expected), qPrintable(field + " -> unexpected " + expected));
}

void TstEnvBuilder::roundTrips_data()
{
    QTest::addColumn<DLSSSettings>("settings");

    DLSSSettings plain;
    QTest::newRow("defaults") << plain;

    DLSSSettings sr;
    sr.srOverride = true;
    sr.srMode = "Quality";
    sr.srPreset = "Preset E";
    sr.srScalingRatio = 66;
    QTest::newRow("super resolution") << sr;

    DLSSSettings rr;
    rr.rrOverride = true;
    rr.rrMode = "Balanced";
    rr.rrPreset = "Preset D";
    QTest::newRow("ray reconstruction") << rr;

    DLSSSettings fg;
    fg.fgOverride = true;
    fg.fgMultiFrameCount = 3;
    fg.fgMode = "On";
    fg.fgPreset = "Preset E";
    QTest::newRow("frame generation") << fg;

    DLSSSettings hdr;
    hdr.enableProtonWayland = true;
    hdr.enableProtonHDR = true;
    hdr.enableHDRWSI = true;
    QTest::newRow("hdr stack") << hdr;

    DLSSSettings tweaks;
    tweaks.protonPriorityHigh = true;
    tweaks.protonUseNTSync = true;
    tweaks.protonUseD7VK = true;
    tweaks.protonLog = true;
    tweaks.enableMangoHud = true;
    QTest::newRow("proton tweaks") << tweaks;

    DLSSSettings fps;
    fps.enableFrameRateLimit = true;
    fps.targetFrameRate = 144;
    QTest::newRow("frame rate limit") << fps;

    DLSSSettings prime;
    prime.enablePrimeRenderOffload = true;
    QTest::newRow("prime render offload") << prime;

    DLSSSettings vkd3d;
    vkd3d.enableVkd3dDescriptorHeap = true;
    vkd3d.vkd3dConfigExtra = "dxr";
    QTest::newRow("vkd3d config") << vkd3d;

    DLSSSettings custom;
    custom.customLaunchParams = "MY_OWN_VAR=7 %command% -windowed -novid";
    QTest::newRow("custom params") << custom;

    DLSSSettings everything;
    everything.enableNGXUpdater = true;
    everything.enableReflex = true;
    everything.enableVkd3dLowLatency = true;
    everything.srOverride = true;
    everything.srMode = "Performance";
    everything.rrOverride = true;
    everything.rrMode = "Quality";
    everything.fgOverride = true;
    everything.fgMultiFrameCount = 2;
    everything.dlssUpgrade = true;
    everything.showIndicator = true;
    everything.enableSmoothMotion = true;
    everything.enableProtonHDR = true;
    everything.disableAutoHDR = true;
    everything.protonUseNTSync = true;
    everything.enableFrameRateLimit = true;
    everything.targetFrameRate = 60;
    QTest::newRow("everything at once") << everything;
}

void TstEnvBuilder::roundTrips()
{
    QFETCH(DLSSSettings, settings);

    const QString built = EnvBuilder::buildLaunchOptions(settings);
    const DLSSSettings parsed = reparse(built, DLSSSettings());
    const QString rebuilt = EnvBuilder::buildLaunchOptions(parsed);

    // The string is the contract, not the struct: fields the builder never
    // emits (executablePath, protonVersion, ...) legitimately do not survive.
    QCOMPARE(rebuilt, built);
}

void TstEnvBuilder::absenceMeansOff()
{
    // Importing from Steam must not leave stale flags set. A base with
    // everything on, parsed against a string with nothing in it, must come back
    // off — otherwise a user who cleared an option in Steam gets it back.
    DLSSSettings base;
    base.enableReflex = true;
    base.protonLog = true;
    base.enableSmoothMotion = true;
    base.enableProtonHDR = true;

    const DLSSSettings parsed = reparse("%command%", base);

    QVERIFY(!parsed.enableReflex);
    QVERIFY(!parsed.protonLog);
    QVERIFY(!parsed.enableSmoothMotion);
    QVERIFY(!parsed.enableProtonHDR);
}

void TstEnvBuilder::primeTrioIsAllOrNothing()
{
    DLSSSettings prime;
    prime.enablePrimeRenderOffload = true;
    const QString built = EnvBuilder::buildLaunchOptions(prime);

    QVERIFY(built.contains("__NV_PRIME_RENDER_OFFLOAD=1"));
    QVERIFY(built.contains("__GLX_VENDOR_LIBRARY_NAME=nvidia"));
    QVERIFY(built.contains("__VK_LAYER_NV_optimus=NVIDIA_only"));

    QVERIFY(reparse(built, DLSSSettings()).enablePrimeRenderOffload);

    // Explicitly disabled upstream: the flag is present but zero, so the trio
    // must not be absorbed as "enabled".
    const DLSSSettings off = reparse("__NV_PRIME_RENDER_OFFLOAD=0 %command%", DLSSSettings());
    QVERIFY(!off.enablePrimeRenderOffload);

    // The two companions on their own are not the feature either.
    const DLSSSettings partial =
        reparse("__GLX_VENDOR_LIBRARY_NAME=nvidia %command%", DLSSSettings());
    QVERIFY(!partial.enablePrimeRenderOffload);
}

void TstEnvBuilder::preservesCommandTail()
{
    const QString raw = "PROTON_ENABLE_NVAPI=1 %command% -skipintro -width 2560";
    const EnvBuilder::ParsedLaunchOptions parsed =
        EnvBuilder::parseLaunchOptions(raw, DLSSSettings());

    QVERIFY2(parsed.customParams.contains("-skipintro"),
             qPrintable("lost the tail: " + parsed.customParams));
    QVERIFY(parsed.customParams.contains("-width 2560"));

    DLSSSettings settings = parsed.settings;
    settings.customLaunchParams = parsed.customParams;
    const QString rebuilt = EnvBuilder::buildLaunchOptions(settings);
    QVERIFY2(rebuilt.contains("-skipintro"), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains("-width 2560"), qPrintable(rebuilt));
}

void TstEnvBuilder::keepsUnknownTokens()
{
    // Anything ProtonForge does not model has to survive a load/save cycle, or
    // opening the app would quietly strip a user's own launch options.
    const QString raw = "SOME_FUTURE_PROTON_FLAG=1 gamemoderun %command%";
    const EnvBuilder::ParsedLaunchOptions parsed =
        EnvBuilder::parseLaunchOptions(raw, DLSSSettings());

    QVERIFY2(parsed.customParams.contains("SOME_FUTURE_PROTON_FLAG=1"),
             qPrintable("dropped: " + parsed.customParams));
    QVERIFY2(parsed.customParams.contains("gamemoderun"),
             qPrintable("dropped: " + parsed.customParams));
}

void TstEnvBuilder::drsSettings_data()
{
    QTest::addColumn<DLSSSettings>("settings");
    QTest::addColumn<QStringList>("expectedParts");

    DLSSSettings sr;
    sr.srOverride = true;
    sr.srMode = "Quality";
    QTest::newRow("sr mode is lowercased")
        << sr << QStringList{"NGX_DLSS_SR_OVERRIDE=on", "NGX_DLSS_SR_MODE=quality"};

    DLSSSettings rr;
    rr.rrOverride = true;
    rr.rrMode = "Ultra Performance";
    QTest::newRow("rr mode is lowercased")
        << rr << QStringList{"NGX_DLSS_RR_OVERRIDE=on", "NGX_DLSS_RR_MODE=ultra performance"};

    DLSSSettings fg;
    fg.fgOverride = true;
    fg.fgMultiFrameCount = 3;
    fg.fgMode = "Auto";
    QTest::newRow("frame generation")
        << fg << QStringList{"NGX_DLSS_FG_OVERRIDE=on", "NGX_DLSSG_MULTI_FRAME_COUNT=3",
                             "NGX_DLSSG_MODE=auto"};
}

void TstEnvBuilder::drsSettings()
{
    QFETCH(DLSSSettings, settings);
    QFETCH(QStringList, expectedParts);

    const QString drs = EnvBuilder::buildDRSSettings(settings);
    for (const QString& part : expectedParts) {
        QVERIFY2(drs.contains(part), qPrintable("missing '" + part + "' in: " + drs));
    }
}

void TstEnvBuilder::scalingRatioIsRangeChecked()
{
    DLSSSettings settings;
    settings.srOverride = true;
    settings.srMode = "Quality";

    settings.srScalingRatio = 0;      // "default", must not be emitted
    QVERIFY(!EnvBuilder::buildDRSSettings(settings).contains("SCALING_RATIO"));

    settings.srScalingRatio = 66;
    QVERIFY(EnvBuilder::buildDRSSettings(settings).contains("SCALING_RATIO"));

    settings.srScalingRatio = 101;    // out of range, must not be emitted
    QVERIFY(!EnvBuilder::buildDRSSettings(settings).contains("SCALING_RATIO"));

    settings.srScalingRatio = -5;
    QVERIFY(!EnvBuilder::buildDRSSettings(settings).contains("SCALING_RATIO"));
}

void TstEnvBuilder::customGameArgs_data()
{
    QTest::addColumn<QString>("custom");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("empty")            << QString()                       << QStringList{};
    QTest::newRow("no %command%")     << "FOO=1"                         << QStringList{};
    QTest::newRow("args after")       << "%command% -novid -windowed"    << QStringList{"-novid", "-windowed"};
    QTest::newRow("env before args")  << "FOO=1 %command% -novid"        << QStringList{"-novid"};
    QTest::newRow("nothing after")    << "FOO=1 %command%"               << QStringList{};
}

void TstEnvBuilder::customGameArgs()
{
    QFETCH(QString, custom);
    QFETCH(QStringList, expected);

    DLSSSettings settings;
    settings.customLaunchParams = custom;
    QCOMPARE(EnvBuilder::customGameArgs(settings), expected);
}

void TstEnvBuilder::environmentCarriesTheSameVariables()
{
    // Direct launch and "copy to Steam" have to agree, or a game behaves
    // differently depending on how it was started.
    DLSSSettings settings;
    settings.enableProtonHDR = true;
    settings.protonUseNTSync = true;
    settings.enableFrameRateLimit = true;
    settings.targetFrameRate = 90;

    const QProcessEnvironment env = EnvBuilder::buildEnvironment(settings);

    QCOMPARE(env.value("PROTON_ENABLE_HDR"), QString("1"));
    QCOMPARE(env.value("PROTON_USE_NTSYNC"), QString("1"));
    QCOMPARE(env.value("DXVK_FRAME_RATE"), QString("90"));
    QCOMPARE(env.value("VKD3D_FRAME_RATE"), QString("90"));
}

void TstEnvBuilder::mangoHudIsAWrapperNotAVariable()
{
    // MANGOHUD=1 only switches on the Vulkan implicit layer, so an OpenGL game
    // (Stellaris and every other Clausewitz title) showed no overlay at all.
    // The launch options carry the wrapper command instead, which preloads
    // libMangoHud and therefore covers OpenGL too.
    DLSSSettings settings;
    settings.enableMangoHud = true;

    const QString built = EnvBuilder::buildLaunchOptions(settings);
    QVERIFY2(built.contains("mangohud %command%"), qPrintable(built));

    // The environment keeps the variable: it is what the wrapper itself sets,
    // and it is the fallback that still covers Vulkan if mangohud is missing.
    QCOMPARE(EnvBuilder::buildEnvironment(settings).value("MANGOHUD"), QString("1"));
}

void TstEnvBuilder::mangoHudFollowsEveryEnvVar()
{
    // Ordering is not cosmetic: "mangohud FOO=1 game" hands FOO=1 to mangohud as
    // an argument instead of putting it in the game's environment.
    DLSSSettings settings;
    settings.enableMangoHud = true;
    settings.customLaunchParams = "FOO=1";      // extra env vars, no %command%

    const QString built = EnvBuilder::buildLaunchOptions(settings);
    QVERIFY2(built.indexOf("FOO=1") < built.indexOf("mangohud"), qPrintable(built));
    QVERIFY2(built.endsWith("mangohud %command%"), qPrintable(built));
}

void TstEnvBuilder::mangoHudWrapsAUserWrapper()
{
    // Two wrappers nest: mangohud outside, the user's own inside, and both in
    // front of %command%.
    DLSSSettings settings;
    settings.enableMangoHud = true;
    settings.customLaunchParams = "gamemoderun %command%";

    const QString built = EnvBuilder::buildLaunchOptions(settings);
    QVERIFY2(built.endsWith("mangohud gamemoderun %command%"), qPrintable(built));

    // And it survives a round trip without the token being duplicated or
    // demoted into customParams.
    const DLSSSettings parsed = reparse(built, DLSSSettings());
    QVERIFY(parsed.enableMangoHud);
    QCOMPARE(parsed.customLaunchParams.count("mangohud"), 0);
    QCOMPARE(EnvBuilder::buildLaunchOptions(parsed), built);
}

void TstEnvBuilder::legacyMangoHudVariableStillParses()
{
    // Options written by an older ProtonForge, or by hand, say MANGOHUD=1.
    // Reading them must still tick the box — it just gets rewritten as the
    // wrapper on the next save.
    const DLSSSettings parsed = reparse("MANGOHUD=1 %command%", DLSSSettings());
    QVERIFY(parsed.enableMangoHud);
    QVERIFY2(!parsed.customLaunchParams.contains("MANGOHUD"),
             qPrintable(parsed.customLaunchParams));

    QVERIFY(EnvBuilder::buildLaunchOptions(parsed).contains("mangohud %command%"));
}

void TstEnvBuilder::customWrapper_data()
{
    QTest::addColumn<QString>("custom");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("empty")           << QString()                     << QStringList{};
    // No %command%: by convention the whole string is env vars, nothing wraps.
    QTest::newRow("no %command%")    << "FOO=1 gamemoderun"           << QStringList{};
    QTest::newRow("bare wrapper")    << "gamemoderun %command%"       << QStringList{"gamemoderun"};
    QTest::newRow("env then wrapper")<< "FOO=1 gamemoderun %command%" << QStringList{"gamemoderun"};
    QTest::newRow("wrapper args")    << "strangle 60 %command%"       << QStringList{"strangle", "60"};
    QTest::newRow("env only")        << "FOO=1 %command%"             << QStringList{};
    // Trailing game arguments belong to customGameArgs(), not here.
    QTest::newRow("args after")      << "gamemoderun %command% -novid" << QStringList{"gamemoderun"};
}

void TstEnvBuilder::customWrapper()
{
    QFETCH(QString, custom);
    QFETCH(QStringList, expected);

    DLSSSettings settings;
    settings.customLaunchParams = custom;
    QCOMPARE(EnvBuilder::customWrapper(settings), expected);
}

QTEST_MAIN(TstEnvBuilder)
#include "tst_envbuilder.moc"
