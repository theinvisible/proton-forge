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
    void argumentsWithoutCommandFollowTheGame();
    void mangoHudPrecedesAUserCommandsArguments();
    void legacyMangoHudVariableStillParses();
    void customWrapper_data();
    void customWrapper();
    void everyEmittedVariableIsClassified();
    void dlssOverridesAreProtonOnly();
    void driverLevelVariablesReachNativeGames();
    void unknownVariablesAreTreatedLeniently();
    void nvapiIsOnUnlessDisabled();
    void legacyFrameRateMigrates();
    void dxvkConfigKeepsForeignEntries();
    void customConfigIsMergedNotShadowed();
    void dlssVersionRoundTrips();
    void priorityHighIsKeptAsCustom();
    void hdrImportsFromDxvkHdr();
    void d7vkImportsFromEitherName();
    void sameLaunchOptions_data();
    void sameLaunchOptions();
    void dynamicFrameGenOnlyInDynamicMode();
    void queueFamilyNeedsSmoothMotion();
    void rrPresetsCanonicalizeAgainstTheirOwnList();
    void emissionOrderIsStable();
    void plainSwitchesRoundTrip();
    void controllerInputImport();

private:
    // Every option turned on, so a test can see the full set of variables
    // ProtonForge is capable of emitting rather than a sample of it.
    static DLSSSettings everythingOn()
    {
        DLSSSettings s;
        s.enablePrimeRenderOffload = true;
        s.enableNVAPI = false;          // the only state that emits anything
        s.enableNGXUpdater = true;
        s.enableReflex = true;
        s.enableVkd3dLowLatency = true;
        s.enableVkd3dDescriptorHeap = true;
        s.enableD3D12ShaderExtensions = true;
        s.dlssUpgrade = true;
        s.showIndicator = true;
        s.srOverride = true;
        s.rrOverride = true;
        s.fgOverride = true;
        s.enableSmoothMotion = true;
        s.smoothMotionGraphicsQueue = true;
        s.enableFrameRateLimit = true;
        s.targetFrameRate = 60;
        s.enableProtonWayland = true;
        s.enableProtonHDR = true;
        s.enableHDRWSI = true;
        s.disableAutoHDR = true;
        s.protonUseNTSync = true;
        s.protonDisableNTSync = true;
        s.protonNoSteamInput = true;
        s.protonPreferSdl = true;
        s.protonUseD7VK = true;
        s.protonLog = true;
        s.enableMangoHud = true;
        return s;
    }

    // parseLaunchOptions folds unrecognised tokens into customParams, which the
    // caller then assigns to the settings object (see MainWindow's import path).
    // Doing the same here is what makes the round-trip comparable.
    // Everything on, every value set — the widest string ProtonForge writes.
    static DLSSSettings fullyLoaded()
    {
        DLSSSettings s = everythingOn();
        s.srMode = "QUALITY";
        s.srPreset = "RENDER_PRESET_K";
        s.srScalingRatio = 66;
        s.rrMode = "BALANCED";
        s.rrPreset = "RENDER_PRESET_F";
        s.fgMultiFrameCount = 3;
        s.fgMode = "DYNAMIC";
        s.fgPreset = "RENDER_PRESET_B";
        s.fgDynamicTargetFps = 120;
        s.fgDynamicMaxCount = 3;
        s.dlssVersion = "310.9.1";
        s.targetFrameRate = 90;
        s.dxvkConfigExtra = "dxgi.syncInterval=0";
        s.vkd3dConfigExtra = "dxr";
        s.customLaunchParams = "FOO=1 gamemoderun %command% -novid";
        return s;
    }

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
    // NVAPI is on by default and on by default in Proton, so a default object
    // says nothing at all — the baseline every other expectation is relative to.
    QCOMPARE(EnvBuilder::buildLaunchOptions(DLSSSettings()), QStringLiteral("%command%"));
}

void TstEnvBuilder::emitsKnownVariables_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QString>("expected");

    QTest::newRow("ngx updater")     << "enableNGXUpdater"        << "PROTON_ENABLE_NGX_UPDATER=1";
    QTest::newRow("reflex")          << "enableReflex"            << "DXVK_NVAPI_VKREFLEX=1";
    QTest::newRow("vkd3d lowlatency")<< "enableVkd3dLowLatency"   << "PROTON_VKD3D_LOWLATENCY=1";
    QTest::newRow("d3d12 shader extn")<< "enableD3D12ShaderExtensions" << "DXVK_NVAPI_D3D12_NV_SHADER_EXTN=1";
    QTest::newRow("dlss upgrade")    << "dlssUpgrade"             << "PROTON_DLSS_UPGRADE=1";
    QTest::newRow("indicator")       << "showIndicator"           << "PROTON_DLSS_INDICATOR=1";
    QTest::newRow("smooth motion")   << "enableSmoothMotion"      << "NVPRESENT_ENABLE_SMOOTH_MOTION=1";
    QTest::newRow("wayland")         << "enableProtonWayland"     << "PROTON_ENABLE_WAYLAND=1";
    QTest::newRow("hdr")             << "enableProtonHDR"         << "PROTON_ENABLE_HDR=1";
    QTest::newRow("hdr (cachyos)")   << "enableProtonHDR"         << "DXVK_HDR=1";
    QTest::newRow("hdr wsi")         << "enableHDRWSI"            << "ENABLE_HDR_WSI=1";
    QTest::newRow("no auto hdr")     << "disableAutoHDR"          << "DXVK_NO_HDR=1";
    QTest::newRow("ntsync")          << "protonUseNTSync"         << "PROTON_USE_NTSYNC=1";
    QTest::newRow("ntsync off")      << "protonDisableNTSync"     << "PROTON_NO_NTSYNC=1";
    QTest::newRow("d7vk")            << "protonUseD7VK"           << "PROTON_USE_D7VK=1";
    QTest::newRow("d7vk (cachyos)")  << "protonUseD7VK"           << "PROTON_D7VK_DDRAW=1";
    QTest::newRow("proton log")      << "protonLog"               << "PROTON_LOG=1";
    QTest::newRow("no steam input")  << "protonNoSteamInput"      << "PROTON_NO_STEAMINPUT=1";
    QTest::newRow("prefer sdl")      << "protonPreferSdl"         << "PROTON_PREFER_SDL=1";
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

    DLSSSettings dynamic;
    dynamic.fgOverride = true;
    dynamic.fgMode = "DYNAMIC";
    dynamic.fgDynamicTargetFps = 117;
    dynamic.fgDynamicMaxCount = 3;
    QTest::newRow("dynamic frame generation") << dynamic;

    DLSSSettings rrF;
    rrF.rrOverride = true;
    rrF.rrPreset = "RENDER_PRESET_F";
    QTest::newRow("rr preset F") << rrF;

    DLSSSettings srDropped;   // no longer offered, still round-trips verbatim
    srDropped.srOverride = true;
    srDropped.srPreset = "RENDER_PRESET_N";
    QTest::newRow("dropped sr preset") << srDropped;

    DLSSSettings smoothQueue;
    smoothQueue.enableSmoothMotion = true;
    smoothQueue.smoothMotionGraphicsQueue = true;
    QTest::newRow("smooth motion graphics queue") << smoothQueue;

    DLSSSettings hdr;
    hdr.enableProtonWayland = true;
    hdr.enableProtonHDR = true;
    hdr.enableHDRWSI = true;
    QTest::newRow("hdr stack") << hdr;

    DLSSSettings tweaks;
    tweaks.protonUseNTSync = true;
    tweaks.protonUseD7VK = true;
    tweaks.protonLog = true;
    tweaks.enableMangoHud = true;
    QTest::newRow("proton tweaks") << tweaks;

    DLSSSettings noSteamInput;
    noSteamInput.protonNoSteamInput = true;
    QTest::newRow("controller: no steam input") << noSteamInput;

    DLSSSettings sdl;
    sdl.protonPreferSdl = true;
    QTest::newRow("controller: sdl") << sdl;

    DLSSSettings fps;
    fps.enableFrameRateLimit = true;
    fps.targetFrameRate = 144;
    QTest::newRow("frame rate limit") << fps;

    DLSSSettings dxvk;
    dxvk.enableFrameRateLimit = true;
    dxvk.targetFrameRate = 90;
    dxvk.dxvkConfigExtra = "dxgi.syncInterval=0;d3d11.samplerAnisotropy=16";
    QTest::newRow("dxvk config with frame cap") << dxvk;

    DLSSSettings nvapiOff;
    nvapiOff.enableNVAPI = false;
    QTest::newRow("nvapi off") << nvapiOff;

    DLSSSettings ntsyncOff;
    ntsyncOff.protonDisableNTSync = true;
    QTest::newRow("ntsync off") << ntsyncOff;

    DLSSSettings pinned;
    pinned.dlssUpgrade = true;
    pinned.dlssVersion = "310.9.1";
    QTest::newRow("dlss upgrade pinned") << pinned;

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

    // No %command%: Steam appends the string to the game's command line.
    DLSSSettings bareArgs;
    bareArgs.customLaunchParams = "-novid -console";
    QTest::newRow("game args without %command%") << bareArgs;

    DLSSSettings envAndArgs;
    envAndArgs.customLaunchParams = "FOO=1 -novid";
    QTest::newRow("env and args without %command%") << envAndArgs;

    DLSSSettings mangoCustom;
    mangoCustom.enableMangoHud = true;
    mangoCustom.customLaunchParams = "FOO=1 %command% -novid";
    QTest::newRow("mangohud with custom env") << mangoCustom;

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
    base.enableD3D12ShaderExtensions = true;
    base.protonNoSteamInput = true;
    base.protonPreferSdl = true;
    base.protonLog = true;
    base.enableSmoothMotion = true;
    base.enableProtonHDR = true;
    base.smoothMotionGraphicsQueue = true;
    base.fgDynamicTargetFps = 120;
    base.fgDynamicMaxCount = 3;

    const DLSSSettings parsed = reparse("%command%", base);

    QVERIFY(!parsed.enableReflex);
    QVERIFY(!parsed.enableD3D12ShaderExtensions);
    QVERIFY(!parsed.protonNoSteamInput);
    QVERIFY(!parsed.protonPreferSdl);
    QVERIFY(!parsed.protonLog);
    QVERIFY(!parsed.enableSmoothMotion);
    QVERIFY(!parsed.enableProtonHDR);
    QVERIFY(!parsed.smoothMotionGraphicsQueue);
    QCOMPARE(parsed.fgDynamicTargetFps, 0);
    QCOMPARE(parsed.fgDynamicMaxCount, 0);
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

    DLSSSettings dynamic;
    dynamic.fgOverride = true;
    dynamic.fgMode = "DYNAMIC";
    dynamic.fgDynamicTargetFps = 144;
    dynamic.fgDynamicMaxCount = 5;
    QTest::newRow("dynamic frame generation")
        << dynamic << QStringList{"NGX_DLSSG_MODE=dynamic",
                                  "NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE=144",
                                  "NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX=5"};
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
    // Without %command%, Steam appends the string to the command line.
    QTest::newRow("bare args")        << "-novid -console"               << QStringList{"-novid", "-console"};
    QTest::newRow("env then args")    << "FOO=1 -novid"                  << QStringList{"-novid"};
    QTest::newRow("assignment later") << "-novid BAR=2"                  << QStringList{"-novid", "BAR=2"};
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
    QCOMPARE(env.value("DXVK_HDR"), QString("1"));
    QCOMPARE(env.value("PROTON_USE_NTSYNC"), QString("1"));
    QCOMPARE(env.value("DXVK_CONFIG"), QString("dxvk.maxFrameRate=90"));
    QCOMPARE(env.value("VKD3D_FRAME_RATE"), QString("90"));
    QVERIFY(!env.contains("DXVK_FRAME_RATE"));      // removed in DXVK 3.0

    // Every managed variable, set the same way on both paths. The launch
    // string may quote a value for the shell; the environment never does.
    const DLSSSettings all = everythingOn();
    const QProcessEnvironment allEnv = EnvBuilder::buildEnvironment(all);
    for (const QString& token : EnvBuilder::buildLaunchOptions(all).split(' ', Qt::SkipEmptyParts)) {
        const int eq = token.indexOf('=');
        if (eq <= 0) continue;
        QString value = token.mid(eq + 1);
        if (value.startsWith('"')) value = value.mid(1, value.size() - 2);
        QCOMPARE(allEnv.value(token.left(eq)), value);
    }
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

void TstEnvBuilder::argumentsWithoutCommandFollowTheGame()
{
    // "-novid -console" imported from Steam used to be written back as
    // "-novid -console %command%" — Steam then tried to run "-novid", and the
    // game did not start. A direct launch dropped both flags instead.
    DLSSSettings settings;
    settings.customLaunchParams = "-novid -console";
    QString built = EnvBuilder::buildLaunchOptions(settings);
    QVERIFY2(built.endsWith(" %command% -novid -console") || built == "%command% -novid -console",
             qPrintable(built));
    QVERIFY2(!built.contains("-novid -console %command%"), qPrintable(built));

    settings.customLaunchParams = "FOO=1 -novid";
    built = EnvBuilder::buildLaunchOptions(settings);
    QVERIFY2(built.endsWith("FOO=1 %command% -novid"), qPrintable(built));
    QCOMPARE(EnvBuilder::buildEnvironment(settings).value("FOO"), QStringLiteral("1"));

    // Imported, the position is spelled out, so the field says what it means.
    QCOMPARE(EnvBuilder::parseLaunchOptions("-novid -console", DLSSSettings()).customParams,
             QStringLiteral("%command% -novid -console"));
    QCOMPARE(EnvBuilder::parseLaunchOptions("FOO=1 -novid", DLSSSettings()).customParams,
             QStringLiteral("FOO=1 %command% -novid"));
    // Env vars alone keep the old, shorter form.
    QCOMPARE(EnvBuilder::parseLaunchOptions("FOO=1", DLSSSettings()).customParams,
             QStringLiteral("FOO=1"));
}

void TstEnvBuilder::mangoHudPrecedesAUserCommandsArguments()
{
    // The same ordering rule when the user did write %command%: their env vars
    // go in front of mangohud, not behind it.
    DLSSSettings settings;
    settings.enableMangoHud = true;
    settings.customLaunchParams = "FOO=1 %command% -novid";

    const QString built = EnvBuilder::buildLaunchOptions(settings);
    QVERIFY2(built.endsWith("FOO=1 mangohud %command% -novid"), qPrintable(built));
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
    // No %command%: nothing wraps a command whose position was never given —
    // "gamemoderun" here is a game argument, which is how Steam treats it.
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

void TstEnvBuilder::everyEmittedVariableIsClassified()
{
    // The drift guard. scopeOf() decides which controls the UI greys out for a
    // native Linux game; a new option whose variable nobody classified would
    // silently default to Any and be offered as if it worked. Adding a variable
    // to buildLaunchOptions without adding it to the scope table fails here.
    const QStringList managed = EnvBuilder::managedVars();
    const QString built = EnvBuilder::buildLaunchOptions(everythingOn());

    QStringList unclassified;
    for (const QString& token : built.split(' ', Qt::SkipEmptyParts)) {
        const int eq = token.indexOf('=');
        if (eq <= 0)
            continue;   // "mangohud", "%command%" and other command-part tokens
        const QString key = token.left(eq);
        if (!managed.contains(key))
            unclassified << key;
    }

    QVERIFY2(unclassified.isEmpty(),
             qPrintable("unclassified in EnvBuilder::scopeOf: " + unclassified.join(", ")));

    // ...and the reverse, so the table cannot accumulate variables that no
    // longer exist and quietly grey out a control for no reason. Both builders
    // count: MANGOHUD is a real environment variable on the direct-launch path
    // but appears in launch options as the `mangohud` wrapper token instead.
    const QProcessEnvironment env = EnvBuilder::buildEnvironment(everythingOn());
    for (const QString& key : managed) {
        QVERIFY2(built.contains(key + "=") || env.contains(key),
                 qPrintable("scope table lists " + key + ", which nothing emits"));
    }
}

void TstEnvBuilder::dlssOverridesAreProtonOnly()
{
    // The reason this whole distinction exists: every DLSS SR/RR/FG override
    // travels in DXVK_NVAPI_DRS_SETTINGS, which only DXVK-NVAPI reads, and
    // DXVK-NVAPI exists only inside a Proton prefix. NVIDIA do not support
    // preset overrides on Linux outside of it, so a native game cannot be
    // given these settings at all.
    QCOMPARE(EnvBuilder::scopeOf("DXVK_NVAPI_DRS_SETTINGS"), EnvBuilder::VarScope::ProtonOnly);
    QCOMPARE(EnvBuilder::scopeOf("DXVK_NVAPI_VKREFLEX"), EnvBuilder::VarScope::ProtonOnly);
    QCOMPARE(EnvBuilder::scopeOf("DXVK_NVAPI_D3D12_NV_SHADER_EXTN"), EnvBuilder::VarScope::ProtonOnly);
    QCOMPARE(EnvBuilder::scopeOf("PROTON_NO_STEAMINPUT"), EnvBuilder::VarScope::ProtonOnly);
    QCOMPARE(EnvBuilder::scopeOf("PROTON_PREFER_SDL"), EnvBuilder::VarScope::ProtonOnly);
    QCOMPARE(EnvBuilder::scopeOf("PROTON_DISABLE_NVAPI"), EnvBuilder::VarScope::ProtonOnly);
    QCOMPARE(EnvBuilder::scopeOf("VKD3D_CONFIG"), EnvBuilder::VarScope::ProtonOnly);
    QCOMPARE(EnvBuilder::scopeOf("DXVK_CONFIG"), EnvBuilder::VarScope::ProtonOnly);
}

void TstEnvBuilder::driverLevelVariablesReachNativeGames()
{
    // The counterpart: these are read by the driver, a Vulkan implicit layer or
    // a wrapper, all of which sit below the API a game talks to. Greying these
    // out for a native game would be wrong.
    QCOMPARE(EnvBuilder::scopeOf("__NV_PRIME_RENDER_OFFLOAD"), EnvBuilder::VarScope::Any);
    QCOMPARE(EnvBuilder::scopeOf("__GLX_VENDOR_LIBRARY_NAME"), EnvBuilder::VarScope::Any);
    QCOMPARE(EnvBuilder::scopeOf("__VK_LAYER_NV_optimus"), EnvBuilder::VarScope::Any);
    QCOMPARE(EnvBuilder::scopeOf("NVPRESENT_ENABLE_SMOOTH_MOTION"), EnvBuilder::VarScope::Any);
    QCOMPARE(EnvBuilder::scopeOf("NVPRESENT_QUEUE_FAMILY"), EnvBuilder::VarScope::Any);
    QCOMPARE(EnvBuilder::scopeOf("ENABLE_HDR_WSI"), EnvBuilder::VarScope::Any);
    QCOMPARE(EnvBuilder::scopeOf("MANGOHUD"), EnvBuilder::VarScope::Any);
}

void TstEnvBuilder::unknownVariablesAreTreatedLeniently()
{
    // A user's own variable from Custom Launch Parameters is none of our
    // business, and neither is one we have not classified yet. Matching
    // FeatureGate's policy, the unknown case never disables anything.
    QCOMPARE(EnvBuilder::scopeOf("SOMETHING_WE_DO_NOT_KNOW"), EnvBuilder::VarScope::Any);
    QCOMPARE(EnvBuilder::scopeOf(""), EnvBuilder::VarScope::Any);
}

void TstEnvBuilder::nvapiIsOnUnlessDisabled()
{
    // Valve's Proton 11 and Proton-CachyOS read PROTON_DISABLE_NVAPI and
    // PROTON_FORCE_NVAPI; nothing current reads PROTON_ENABLE_NVAPI. So the
    // default emits nothing and switching NVAPI off is what gets written.
    DLSSSettings off;
    off.enableNVAPI = false;
    QCOMPARE(EnvBuilder::buildLaunchOptions(off), QStringLiteral("PROTON_DISABLE_NVAPI=1 %command%"));
    QCOMPARE(EnvBuilder::buildEnvironment(off).value("PROTON_DISABLE_NVAPI"), QString("1"));

    // A string without any NVAPI variable means NVAPI on — even when the base
    // had it off. (The reset used to say "off", which would have turned every
    // import into PROTON_DISABLE_NVAPI=1.)
    QVERIFY(reparse("%command%", off).enableNVAPI);

    // The relic is understood and dropped on the next write.
    DLSSSettings legacy = reparse("PROTON_ENABLE_NVAPI=1 %command%", off);
    QVERIFY(legacy.enableNVAPI);
    QCOMPARE(legacy.customLaunchParams, QStringLiteral("%command%"));
    QVERIFY(!reparse("PROTON_ENABLE_NVAPI=0 %command%", DLSSSettings()).enableNVAPI);

    // Disabling wins, in either order.
    QVERIFY(!reparse("PROTON_DISABLE_NVAPI=1 PROTON_ENABLE_NVAPI=1 %command%", DLSSSettings()).enableNVAPI);
    QVERIFY(!reparse("PROTON_ENABLE_NVAPI=1 PROTON_DISABLE_NVAPI=1 %command%", DLSSSettings()).enableNVAPI);

    // PROTON_FORCE_NVAPI is not modelled and survives as the user's own.
    QVERIFY(reparse("PROTON_FORCE_NVAPI=1 %command%", DLSSSettings())
                .customLaunchParams.contains("PROTON_FORCE_NVAPI=1"));
}

void TstEnvBuilder::legacyFrameRateMigrates()
{
    // DXVK 3.0 removed DXVK_FRAME_RATE, and Valve's Proton 11 ships a DXVK
    // without it. An old string still imports, and is rewritten.
    const DLSSSettings parsed = reparse("DXVK_FRAME_RATE=144 VKD3D_FRAME_RATE=144 %command%", DLSSSettings());
    QVERIFY(parsed.enableFrameRateLimit);
    QCOMPARE(parsed.targetFrameRate, 144);
    QVERIFY(parsed.customLaunchParams == "%command%");

    const QString rebuilt = EnvBuilder::buildLaunchOptions(parsed);
    QVERIFY2(rebuilt.contains("DXVK_CONFIG=dxvk.maxFrameRate=144"), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains("VKD3D_FRAME_RATE=144"), qPrintable(rebuilt));
    QVERIFY2(!rebuilt.contains("DXVK_FRAME_RATE"), qPrintable(rebuilt));
}

void TstEnvBuilder::dxvkConfigKeepsForeignEntries()
{
    // A user's own DXVK options travel in the same variable as the frame cap.
    const QString raw = R"(DXVK_CONFIG="dxgi.syncInterval=0;dxvk.maxFrameRate=120" %command%)";
    const DLSSSettings parsed = reparse(raw, DLSSSettings());
    QVERIFY(parsed.enableFrameRateLimit);
    QCOMPARE(parsed.targetFrameRate, 120);
    QCOMPARE(parsed.dxvkConfigExtra, QStringLiteral("dxgi.syncInterval=0"));

    // Switching the cap off keeps their options.
    DLSSSettings uncapped = parsed;
    uncapped.enableFrameRateLimit = false;
    QCOMPARE(EnvBuilder::buildLaunchOptions(uncapped),
             QStringLiteral("DXVK_CONFIG=dxgi.syncInterval=0 %command%"));

    // Two entries need quoting, or the shell Steam runs ends the command at ';'.
    const QString built = EnvBuilder::buildLaunchOptions(parsed);
    QVERIFY2(built.contains(R"(DXVK_CONFIG="dxvk.maxFrameRate=120;dxgi.syncInterval=0")"),
             qPrintable(built));
    QCOMPARE(EnvBuilder::buildEnvironment(parsed).value("DXVK_CONFIG"),
             QStringLiteral("dxvk.maxFrameRate=120;dxgi.syncInterval=0"));
}

void TstEnvBuilder::customConfigIsMergedNotShadowed()
{
    // Custom variables are emitted after ours, and the later assignment wins:
    // a DXVK_CONFIG typed into Custom Launch Parameters used to silently drop
    // the frame cap, and a VKD3D_CONFIG the descriptor heap.
    DLSSSettings s;
    s.enableFrameRateLimit = true;
    s.targetFrameRate = 60;
    s.enableVkd3dDescriptorHeap = true;
    s.customLaunchParams = "DXVK_CONFIG=dxgi.maxDeviceMemory=4096 VKD3D_CONFIG=dxr %command% -novid";

    const QString built = EnvBuilder::buildLaunchOptions(s);
    QCOMPARE(built.count("DXVK_CONFIG="), 1);
    QCOMPARE(built.count("VKD3D_CONFIG="), 1);
    QVERIFY2(built.contains(R"(DXVK_CONFIG="dxvk.maxFrameRate=60;dxgi.maxDeviceMemory=4096")"),
             qPrintable(built));
    QVERIFY2(built.contains("VKD3D_CONFIG=descriptor_heap,dxr"), qPrintable(built));
    QVERIFY2(built.endsWith("%command% -novid"), qPrintable(built));

    const QProcessEnvironment env = EnvBuilder::buildEnvironment(s);
    QCOMPARE(env.value("DXVK_CONFIG"), QStringLiteral("dxvk.maxFrameRate=60;dxgi.maxDeviceMemory=4096"));
    QCOMPARE(env.value("VKD3D_CONFIG"), QStringLiteral("descriptor_heap,dxr"));

    // And the merged string is stable.
    QCOMPARE(EnvBuilder::buildLaunchOptions(reparse(built, DLSSSettings())), built);
}

void TstEnvBuilder::dlssVersionRoundTrips()
{
    DLSSSettings s;
    s.dlssUpgrade = true;
    s.dlssVersion = "310.9.1";
    QCOMPARE(EnvBuilder::buildLaunchOptions(s), QStringLiteral("PROTON_DLSS_UPGRADE=310.9.1 %command%"));

    const DLSSSettings parsed = reparse("PROTON_DLSS_UPGRADE=310.2 %command%", DLSSSettings());
    QVERIFY(parsed.dlssUpgrade);
    QCOMPARE(parsed.dlssVersion, QStringLiteral("310.2"));

    // "1" means newest, and clears a pinned version from the base.
    const DLSSSettings newest = reparse("PROTON_DLSS_UPGRADE=1 %command%", s);
    QVERIFY(newest.dlssUpgrade);
    QVERIFY(newest.dlssVersion.isEmpty());
    QVERIFY(!reparse("%command%", s).dlssUpgrade);
    QVERIFY(reparse("%command%", s).dlssVersion.isEmpty());

    // Anything that is not a version is never written as one.
    s.dlssVersion = "latest; rm -rf ~";
    QCOMPARE(EnvBuilder::buildLaunchOptions(s), QStringLiteral("PROTON_DLSS_UPGRADE=1 %command%"));
}

void TstEnvBuilder::priorityHighIsKeptAsCustom()
{
    // PROTON_PRIORITY_HIGH is read by no current Proton. ProtonForge no longer
    // offers it, but a string that has it keeps it — it is the user's.
    const DLSSSettings parsed = reparse("PROTON_PRIORITY_HIGH=1 %command%", DLSSSettings());
    QCOMPARE(parsed.customLaunchParams, QStringLiteral("PROTON_PRIORITY_HIGH=1 %command%"));
    QCOMPARE(EnvBuilder::buildLaunchOptions(parsed), QStringLiteral("PROTON_PRIORITY_HIGH=1 %command%"));
}

void TstEnvBuilder::hdrImportsFromDxvkHdr()
{
    // How Proton-CachyOS users turn HDR on since PROTON_ENABLE_HDR was retired.
    const DLSSSettings parsed = reparse("DXVK_HDR=1 %command%", DLSSSettings());
    QVERIFY(parsed.enableProtonHDR);
    QCOMPARE(parsed.customLaunchParams, QStringLiteral("%command%"));

    // An explicit "off" is not something ProtonForge models.
    const DLSSSettings off = reparse("DXVK_HDR=0 %command%", DLSSSettings());
    QVERIFY(!off.enableProtonHDR);
    QVERIFY(off.customLaunchParams.contains("DXVK_HDR=0"));
}

void TstEnvBuilder::d7vkImportsFromEitherName()
{
    QVERIFY(reparse("PROTON_USE_D7VK=1 %command%", DLSSSettings()).protonUseD7VK);
    QVERIFY(reparse("PROTON_D7VK_DDRAW=1 %command%", DLSSSettings()).protonUseD7VK);
    QVERIFY(!reparse("PROTON_D7VK_DDRAW=0 %command%", DLSSSettings()).protonUseD7VK);
}

void TstEnvBuilder::sameLaunchOptions_data()
{
    QTest::addColumn<QString>("a");
    QTest::addColumn<QString>("b");
    QTest::addColumn<bool>("same");

    QTest::newRow("identical")          << "FOO=1 %command%" << "FOO=1 %command%" << true;
    QTest::newRow("whitespace")         << "  FOO=1   %command% " << "FOO=1 %command%" << true;
    QTest::newRow("empty is %command%") << "" << "%command%" << true;
    QTest::newRow("blank is %command%") << "   " << "%command%" << true;
    QTest::newRow("old form")           << "DXVK_FRAME_RATE=60 %command%"
                                        << "DXVK_CONFIG=dxvk.maxFrameRate=60 VKD3D_FRAME_RATE=60 %command%"
                                        << false;
    QTest::newRow("empty vs options")   << "" << "PROTON_LOG=1 %command%" << false;
}

void TstEnvBuilder::sameLaunchOptions()
{
    QFETCH(QString, a);
    QFETCH(QString, b);
    QFETCH(bool, same);
    QCOMPARE(EnvBuilder::sameLaunchOptions(a, b), same);
    QCOMPARE(EnvBuilder::sameLaunchOptions(b, a), same);
}

void TstEnvBuilder::dynamicFrameGenOnlyInDynamicMode()
{
    // The Dynamic MFG parameters mean nothing in any other mode, so they are
    // kept in the settings but not written — like an SR preset with the SR
    // override off.
    DLSSSettings s;
    s.fgOverride = true;
    s.fgMode = "ON";
    s.fgDynamicTargetFps = 144;
    s.fgDynamicMaxCount = 5;
    const QString drs = EnvBuilder::buildDRSSettings(s);
    QVERIFY2(!drs.contains("NGX_DLSSG_DYNAMIC"), qPrintable(drs));

    // Zero means "driver default" and writes nothing either.
    s.fgMode = "DYNAMIC";
    s.fgDynamicTargetFps = 0;
    s.fgDynamicMaxCount = 0;
    QVERIFY(!EnvBuilder::buildDRSSettings(s).contains("NGX_DLSSG_DYNAMIC"));

    // An imported string is understood regardless of case.
    const DLSSSettings parsed = reparse(
        "DXVK_NVAPI_DRS_SETTINGS=NGX_DLSS_FG_OVERRIDE=on,NGX_DLSSG_MODE=dynamic,"
        "NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE=120,NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX=3 %command%",
        DLSSSettings());
    QCOMPARE(parsed.fgMode, QStringLiteral("DYNAMIC"));
    QCOMPARE(parsed.fgDynamicTargetFps, 120);
    QCOMPARE(parsed.fgDynamicMaxCount, 3);
}

void TstEnvBuilder::queueFamilyNeedsSmoothMotion()
{
    DLSSSettings s;
    s.smoothMotionGraphicsQueue = true;
    QVERIFY(!EnvBuilder::buildLaunchOptions(s).contains("NVPRESENT_QUEUE_FAMILY"));

    s.enableSmoothMotion = true;
    QVERIFY(EnvBuilder::buildLaunchOptions(s).contains("NVPRESENT_QUEUE_FAMILY=1"));
    QCOMPARE(EnvBuilder::buildEnvironment(s).value("NVPRESENT_QUEUE_FAMILY"), QString("1"));

    // Only 1 is documented; any other value is the user's own and stays put.
    const DLSSSettings other = reparse(
        "NVPRESENT_ENABLE_SMOOTH_MOTION=1 NVPRESENT_QUEUE_FAMILY=2 %command%", DLSSSettings());
    QVERIFY(!other.smoothMotionGraphicsQueue);
    QVERIFY2(other.customLaunchParams.contains("NVPRESENT_QUEUE_FAMILY=2"),
             qPrintable(other.customLaunchParams));
}

void TstEnvBuilder::rrPresetsCanonicalizeAgainstTheirOwnList()
{
    const DLSSSettings parsed = reparse(
        "DXVK_NVAPI_DRS_SETTINGS=NGX_DLSS_RR_OVERRIDE=on,"
        "NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION=render_preset_f %command%",
        DLSSSettings());
    QCOMPARE(parsed.rrPreset, QStringLiteral("RENDER_PRESET_F"));
}

void TstEnvBuilder::emissionOrderIsStable()
{
    // The exact string, order included. Steam stores what was written, and
    // --list-games compares against it, so a refactor that merely reorders
    // variables would make every configured game look out of date. Change
    // this literal only for a deliberate change to what gets written.
    const QString expected = QStringLiteral(
        "__NV_PRIME_RENDER_OFFLOAD=1 "
        "__GLX_VENDOR_LIBRARY_NAME=nvidia "
        "__VK_LAYER_NV_optimus=NVIDIA_only "
        "PROTON_DISABLE_NVAPI=1 "
        "PROTON_ENABLE_NGX_UPDATER=1 "
        "DXVK_NVAPI_VKREFLEX=1 "
        "DXVK_NVAPI_D3D12_NV_SHADER_EXTN=1 "
        "PROTON_VKD3D_LOWLATENCY=1 "
        "VKD3D_CONFIG=descriptor_heap,dxr "
        "PROTON_DLSS_UPGRADE=310.9.1 "
        "PROTON_DLSS_INDICATOR=1 "
        "DXVK_NVAPI_DRS_SETTINGS=NGX_DLSS_SR_OVERRIDE=on,NGX_DLSS_SR_MODE=quality,NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION=RENDER_PRESET_K,NGX_DLSS_SR_OVERRIDE_SCALING_RATIO=66,NGX_DLSS_RR_OVERRIDE=on,NGX_DLSS_RR_MODE=balanced,NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION=RENDER_PRESET_F,NGX_DLSS_FG_OVERRIDE=on,NGX_DLSSG_MULTI_FRAME_COUNT=3,NGX_DLSSG_MODE=dynamic,NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION=RENDER_PRESET_B,NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE=120,NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX=3 "
        "NVPRESENT_ENABLE_SMOOTH_MOTION=1 "
        "NVPRESENT_QUEUE_FAMILY=1 "
        "DXVK_CONFIG=\"dxvk.maxFrameRate=90;dxgi.syncInterval=0\" "
        "VKD3D_FRAME_RATE=90 "
        "PROTON_ENABLE_WAYLAND=1 "
        "PROTON_ENABLE_HDR=1 "
        "DXVK_HDR=1 "
        "ENABLE_HDR_WSI=1 "
        "DXVK_NO_HDR=1 "
        "PROTON_USE_NTSYNC=1 "
        "PROTON_NO_NTSYNC=1 "
        "PROTON_USE_D7VK=1 "
        "PROTON_D7VK_DDRAW=1 "
        "PROTON_LOG=1 "
        "PROTON_NO_STEAMINPUT=1 "
        "PROTON_PREFER_SDL=1 "
        "MANGOHUD=1 "
        "FOO=1 "
        "mangohud "
        "gamemoderun "
        "%command% "
        "-novid");
    QCOMPARE(EnvBuilder::buildLaunchOptions(fullyLoaded()), expected);

    // A direct launch carries the same variables (the full equivalence is
    // environmentCarriesTheSameVariables; this pins the values).
    const QProcessEnvironment env = EnvBuilder::buildEnvironment(fullyLoaded());
    QCOMPARE(env.value("DXVK_CONFIG"), QStringLiteral("dxvk.maxFrameRate=90;dxgi.syncInterval=0"));
    QCOMPARE(env.value("VKD3D_CONFIG"), QStringLiteral("descriptor_heap,dxr"));
    QCOMPARE(env.value("PROTON_DLSS_UPGRADE"), QStringLiteral("310.9.1"));
}

void TstEnvBuilder::plainSwitchesRoundTrip()
{
    // Every plain switch in the variable table is read back and written again
    // with nothing but its table row — this covers a new one automatically.
    const QStringList switches = EnvBuilder::plainSwitchVars();
    QVERIFY(switches.contains("DXVK_NVAPI_VKREFLEX"));
    QVERIFY(!switches.contains("PROTON_DISABLE_NVAPI"));   // inverted, hand-written

    for (const QString& var : switches) {
        const DLSSSettings on = reparse(var + "=1 %command%", DLSSSettings());
        QVERIFY2(on.customLaunchParams == "%command%",
                 qPrintable(var + " was not consumed: " + on.customLaunchParams));
        QVERIFY2(EnvBuilder::buildLaunchOptions(on).contains(var + "=1"),
                 qPrintable(var + " did not survive the round trip"));

        const DLSSSettings off = reparse(var + "=0 %command%", on);
        QVERIFY2(!EnvBuilder::buildLaunchOptions(off).contains(var + "="),
                 qPrintable(var + "=0 still writes the switch"));
    }
}

void TstEnvBuilder::controllerInputImport()
{
    // GE's alias is understood and written back under the documented name.
    const DLSSSettings alias = reparse("PROTON_USE_SDL=1 %command%", DLSSSettings());
    QVERIFY(alias.protonPreferSdl);
    QCOMPARE(alias.customLaunchParams, QStringLiteral("%command%"));
    QCOMPARE(EnvBuilder::buildLaunchOptions(alias), QStringLiteral("PROTON_PREFER_SDL=1 %command%"));

    // "=0" means something here — it overrides a protonfix, or keeps Steam
    // Input under Wayland — so it is the user's own and stays where it was.
    const DLSSSettings zeros =
        reparse("PROTON_PREFER_SDL=0 PROTON_NO_STEAMINPUT=0 %command%", DLSSSettings());
    QVERIFY(!zeros.protonPreferSdl);
    QVERIFY(!zeros.protonNoSteamInput);
    QVERIFY2(zeros.customLaunchParams.contains("PROTON_PREFER_SDL=0") &&
                 zeros.customLaunchParams.contains("PROTON_NO_STEAMINPUT=0"),
             qPrintable(zeros.customLaunchParams));
    QVERIFY(EnvBuilder::buildLaunchOptions(zeros).contains("PROTON_NO_STEAMINPUT=0"));
}

QTEST_MAIN(TstEnvBuilder)
#include "tst_envbuilder.moc"
