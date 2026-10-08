// Which Proton a game gets used to be decided by sorting directory names:
// "GE-Proton9-20" sorts after "GE-Proton11-7", "GE-Proton10-9" after
// "GE-Proton10-26", and the default took the *first* proton-cachyos in
// ascending order — the oldest. Steam's own per-game choice was looked up as a
// literal directory name, which no Valve Proton ("proton_11" vs "Proton 11.0")
// and no tool with its own internal name ever matched.
//
// CompatToolResolver takes all of that from directories passed in, so a
// temporary tree stands in for compatibilitytools.d and the Steam libraries.

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "utils/CompatToolResolver.h"

using namespace CompatToolResolver;

class TstCompatToolResolver : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void parsesVersions();
    void classifiesDirectoryNames_data();
    void classifiesDirectoryNames();
    void newestGeIsNumeric();
    void newestCachyOSIsNumeric();
    void valveVersionComesFromTheVersionFile();
    void valveWithoutVersionFileStillSorts();
    void directoryWithoutProtonIsNoTool();
    void manifestNameDiffersFromDirectory();
    void manifestCanRegisterSeveralTools();
    void literalDirectoryNameStillResolves();
    void valveInternalNamesResolveAcrossLibraries();
    void legacyInternalNames();
    void aliasesPickNewestStableRelease();
    void unknownNameResolvesToNothing();
    void defaultPrefersCachyOSThenGeThenValve();
    void latestSteamPrefersExperimental();
    void forkMapping();

private:
    QTemporaryDir m_root;
    QString compat() const { return m_root.path() + "/compatibilitytools.d"; }
    QString lib(int n) const { return m_root.path() + QString("/lib%1/steamapps").arg(n); }

    QString makeProton(const QString& dir, const QByteArray& version = QByteArray()) const
    {
        [&] { QVERIFY(QDir().mkpath(dir)); }();
        QFile proton(dir + "/proton");
        [&] { QVERIFY(proton.open(QIODevice::WriteOnly)); }();
        proton.write("#!/bin/sh\n");
        if (!version.isEmpty()) {
            QFile v(dir + "/version");
            [&] { QVERIFY(v.open(QIODevice::WriteOnly)); }();
            v.write(version);
        }
        return dir;
    }

    void writeManifest(const QString& path, const QList<QPair<QString, QString>>& tools) const
    {
        QByteArray body = "\"compatibilitytools\"\n{\n  \"compat_tools\"\n  {\n";
        for (const auto& t : tools) {
            body += "    \"" + t.first.toUtf8() + "\" // Internal name of this tool\n    {\n";
            body += "      \"install_path\" \"" + t.second.toUtf8() + "\"\n";
            body += "      \"display_name\" \"" + t.first.toUtf8() + "\"\n    }\n";
        }
        body += "  }\n}\n";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(body);
    }

    QList<ToolInfo> everything() const
    {
        return scanCompatDirs({compat()}) + scanLibraries({lib(1), lib(2)});
    }
};

void TstCompatToolResolver::init()
{
    QVERIFY(m_root.isValid());
    QDir(m_root.path()).removeRecursively();
    QVERIFY(QDir().mkpath(compat()));
}

void TstCompatToolResolver::parsesVersions()
{
    QCOMPARE(parseCachyOS("proton-cachyos-11.0-20260703-slr-x86_64_v3"),
             QVersionNumber(11, 0, 20260703));
    QCOMPARE(parseCachyOS("proton-cachyos-10.0-20260127-slr-x86_64.tar.xz"),
             QVersionNumber(10, 0, 20260127));
    QCOMPARE(parseGE("GE-Proton11-7"), QVersionNumber(11, 7, 0));
    QCOMPARE(parseGE("GE-Proton9-20"), QVersionNumber(9, 20, 0));
    // The real file from a Proton 11.0-2c install.
    QCOMPARE(parseValveVersionFile("1788504981 proton-11.0-2c-x86_64"),
             QVersionNumber(11, 0, 2));
    QCOMPARE(parseValveVersionFile("1790244902 experimental-11.0-20260924-x86_64"),
             QVersionNumber());
    QVERIFY(parseGE("proton-cachyos-11.0-20260703").isNull());
}

void TstCompatToolResolver::classifiesDirectoryNames_data()
{
    QTest::addColumn<QString>("dir");
    QTest::addColumn<int>("kind");

    QTest::newRow("cachyos")      << "proton-cachyos-11.0-20261005-slr-x86_64" << int(Kind::CachyOS);
    QTest::newRow("ge")           << "GE-Proton11-7" << int(Kind::GE);
    QTest::newRow("valve 11")     << "Proton 11.0" << int(Kind::Valve);
    QTest::newRow("valve 5.13")   << "Proton 5.13" << int(Kind::Valve);
    QTest::newRow("experimental") << "Proton - Experimental" << int(Kind::ValveExperimental);
    QTest::newRow("hotfix")       << "Proton Hotfix" << int(Kind::ValveHotfix);
    QTest::newRow("custom")       << "Proton-Special" << int(Kind::Other);
    QTest::newRow("luxtorpeda")   << "luxtorpeda" << int(Kind::Other);
}

void TstCompatToolResolver::classifiesDirectoryNames()
{
    QFETCH(QString, dir);
    QFETCH(int, kind);
    QCOMPARE(int(classify(dir)), kind);
}

void TstCompatToolResolver::newestGeIsNumeric()
{
    for (const char* name : {"GE-Proton9-20", "GE-Proton10-3", "GE-Proton10-26", "GE-Proton10-9"})
        makeProton(compat() + "/" + name);

    QCOMPARE(newest(everything(), Kind::GE).dirName, QString("GE-Proton10-26"));

    makeProton(compat() + "/GE-Proton11-7");
    QCOMPARE(newest(everything(), Kind::GE).dirName, QString("GE-Proton11-7"));
}

void TstCompatToolResolver::newestCachyOSIsNumeric()
{
    makeProton(compat() + "/proton-cachyos-9.0-20250101-slr-x86_64");
    makeProton(compat() + "/proton-cachyos-11.0-20260703-slr-x86_64_v3");
    makeProton(compat() + "/proton-cachyos-10.0-20260127-slr-x86_64");

    const ToolInfo t = newest(everything(), Kind::CachyOS);
    QCOMPARE(t.dirName, QString("proton-cachyos-11.0-20260703-slr-x86_64_v3"));
    QCOMPARE(t.version, QVersionNumber(11, 0, 20260703));
}

void TstCompatToolResolver::valveVersionComesFromTheVersionFile()
{
    makeProton(lib(1) + "/common/Proton 11.0", "1788504981 proton-11.0-2c-x86_64\n");
    const ToolInfo t = newest(everything(), Kind::Valve);
    QCOMPARE(t.dirName, QString("Proton 11.0"));
    QCOMPARE(t.version, QVersionNumber(11, 0, 2));
    QCOMPARE(forkOf(t.kind), FeatureGate::Fork::Valve);
}

void TstCompatToolResolver::valveWithoutVersionFileStillSorts()
{
    makeProton(lib(1) + "/common/Proton 9.0");
    makeProton(lib(1) + "/common/Proton 10.0", "1 proton-10.0-4-x86_64\n");
    makeProton(lib(2) + "/common/Proton 11.0");

    const ToolInfo t = newest(everything(), Kind::Valve);
    QCOMPARE(t.dirName, QString("Proton 11.0"));
    QVERIFY(t.version.isNull());     // unknown, not guessed: the gate stays lenient
}

void TstCompatToolResolver::directoryWithoutProtonIsNoTool()
{
    QVERIFY(QDir().mkpath(compat() + "/GE-Proton11-7"));      // half-extracted
    QVERIFY(QDir().mkpath(lib(1) + "/common/Proton 11.0"));   // uninstalled leftovers
    QVERIFY(everything().isEmpty());
}

void TstCompatToolResolver::manifestNameDiffersFromDirectory()
{
    const QString dir = makeProton(compat() + "/proton-cachyos-11.0-20261005-slr-x86_64_v3");
    writeManifest(dir + "/compatibilitytool.vdf", {{"proton-cachyos-slr", "."}});

    const ToolInfo t = resolveMappedName("proton-cachyos-slr", everything());
    QVERIFY(t.isValid());
    QCOMPARE(t.dirName, QString("proton-cachyos-11.0-20261005-slr-x86_64_v3"));
    QCOMPARE(t.kind, Kind::CachyOS);
}

void TstCompatToolResolver::manifestCanRegisterSeveralTools()
{
    const QString bundle = compat() + "/bundle";
    makeProton(bundle + "/a");
    makeProton(bundle + "/b");
    writeManifest(bundle + "/compatibilitytool.vdf", {{"tool-a", "a"}, {"tool-b", "b"}});

    const QList<ToolInfo> tools = everything();
    QCOMPARE(tools.size(), 2);
    QCOMPARE(resolveMappedName("tool-b", tools).path, bundle + "/b");
}

void TstCompatToolResolver::literalDirectoryNameStillResolves()
{
    // What tests/steam-lab/cases/45_launch.sh relies on: a mapping that names
    // a directory with no manifest.
    makeProton(compat() + "/Proton-Special");
    const ToolInfo t = resolveMappedName("Proton-Special", everything());
    QVERIFY(t.isValid());
    QCOMPARE(t.kind, Kind::Other);
}

void TstCompatToolResolver::valveInternalNamesResolveAcrossLibraries()
{
    makeProton(lib(1) + "/common/Proton - Experimental");
    makeProton(lib(1) + "/common/Proton 10.0");
    makeProton(lib(2) + "/common/Proton 11.0", "1 proton-11.0-2-x86_64\n");
    makeProton(lib(2) + "/common/Proton Hotfix");

    const QList<ToolInfo> tools = everything();
    QCOMPARE(resolveMappedName("proton_11", tools).dirName, QString("Proton 11.0"));
    QCOMPARE(resolveMappedName("proton_10", tools).dirName, QString("Proton 10.0"));
    QCOMPARE(resolveMappedName("proton_experimental", tools).dirName,
             QString("Proton - Experimental"));
    QCOMPARE(resolveMappedName("proton_hotfix", tools).dirName, QString("Proton Hotfix"));
    QVERIFY(!resolveMappedName("proton_9", tools).isValid());
}

void TstCompatToolResolver::legacyInternalNames()
{
    makeProton(lib(1) + "/common/Proton 6.3");
    makeProton(lib(1) + "/common/Proton 5.13");
    makeProton(lib(1) + "/common/Proton 5.0");

    const QList<ToolInfo> tools = everything();
    QCOMPARE(resolveMappedName("proton_63", tools).dirName, QString("Proton 6.3"));
    QCOMPARE(resolveMappedName("proton_513", tools).dirName, QString("Proton 5.13"));
    QCOMPARE(resolveMappedName("proton_5", tools).dirName, QString("Proton 5.0"));
}

void TstCompatToolResolver::aliasesPickNewestStableRelease()
{
    makeProton(lib(1) + "/common/Proton - Experimental");
    makeProton(lib(1) + "/common/Proton 10.0", "1 proton-10.0-4-x86_64\n");
    makeProton(lib(2) + "/common/Proton 11.0", "1 proton-11.0-2-x86_64\n");

    const QList<ToolInfo> tools = everything();
    QCOMPARE(resolveMappedName("proton-stable", tools).dirName, QString("Proton 11.0"));
    QCOMPARE(resolveMappedName("proton-next", tools).dirName, QString("Proton 11.0"));
}

void TstCompatToolResolver::unknownNameResolvesToNothing()
{
    makeProton(compat() + "/GE-Proton11-7");
    QVERIFY(!resolveMappedName("does-not-exist", everything()).isValid());
    QVERIFY(!resolveMappedName("", everything()).isValid());
}

void TstCompatToolResolver::defaultPrefersCachyOSThenGeThenValve()
{
    makeProton(lib(1) + "/common/Proton 11.0");
    makeProton(lib(1) + "/common/Proton Hotfix");
    QCOMPARE(defaultChoice(everything()).dirName, QString("Proton 11.0"));

    makeProton(lib(1) + "/common/Proton - Experimental");
    QCOMPARE(defaultChoice(everything()).dirName, QString("Proton - Experimental"));

    makeProton(compat() + "/GE-Proton9-20");
    makeProton(compat() + "/GE-Proton11-7");
    QCOMPARE(defaultChoice(everything()).dirName, QString("GE-Proton11-7"));

    makeProton(compat() + "/proton-cachyos-10.0-20260127-slr-x86_64");
    makeProton(compat() + "/proton-cachyos-11.0-20260703-slr-x86_64");
    QCOMPARE(defaultChoice(everything()).dirName,
             QString("proton-cachyos-11.0-20260703-slr-x86_64"));
}

void TstCompatToolResolver::latestSteamPrefersExperimental()
{
    makeProton(lib(1) + "/common/Proton 10.0");
    makeProton(lib(2) + "/common/Proton 11.0");
    QCOMPARE(latestSteam(everything()).dirName, QString("Proton 11.0"));

    makeProton(lib(2) + "/common/Proton - Experimental");
    QCOMPARE(latestSteam(everything()).dirName, QString("Proton - Experimental"));
}

void TstCompatToolResolver::forkMapping()
{
    QCOMPARE(forkOf(Kind::CachyOS), FeatureGate::Fork::CachyOS);
    QCOMPARE(forkOf(Kind::GE), FeatureGate::Fork::GE);
    QCOMPARE(forkOf(Kind::Valve), FeatureGate::Fork::Valve);
    // A moving target: no version rule can describe it, so stay lenient.
    QCOMPARE(forkOf(Kind::ValveExperimental), FeatureGate::Fork::Unknown);
    QCOMPARE(forkOf(Kind::Other), FeatureGate::Fork::Unknown);
}

QTEST_MAIN(TstCompatToolResolver)
#include "tst_compattoolresolver.moc"
