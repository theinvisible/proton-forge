// CPUDetector::detect() used to shell out to lscpu with LC_ALL=C and parse its
// English key/value output. Everything lscpu reports is in /proc and /sys, so
// readTopology() reads it directly — no subprocess to time out, no locale
// workaround, and the tricky parts become testable against a fixture tree.
//
// The tricky parts, verified below: core counts come from distinct
// (package, core) pairs rather than from a "cores per socket" number, the
// frequency envelope has to consider every CPU (on a 13th-gen part cpu0 reports
// 4700 MHz while the favoured cores reach 4900), and cache totals must sum the
// individual instances because a hybrid CPU's P and E cores have different cache
// sizes — lscpu's "544 KiB (14 instances)" is 6×48K + 8×32K, not 14×48K.

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "utils/CPUDetector.h"

class TstCpuDetector : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void singleSocketWithSmt();
    void dualSocket();
    void hybridCachesAreSummedPerInstance();
    void frequencyEnvelopeSpansAllCpus();
    void missingCpufreqLeavesFrequenciesUnset();
    void missingSysfsFallsBackToProcCpuinfo();
    void emptyTreeReportsNothing();
    void cacheSizesInMebibytes();

private:
    QTemporaryDir m_root;

    QString sysRoot() const  { return m_root.path() + "/sys"; }
    QString procRoot() const { return m_root.path() + "/proc"; }
    QString cpuRoot() const  { return sysRoot() + "/devices/system/cpu"; }

    void write(const QString& path, const QString& value) const
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write((value + "\n").toUtf8());   // sysfs attributes end in a newline
    }

    void setPresent(const QString& list) const { write(cpuRoot() + "/present", list); }

    void setTopology(int cpu, int package, int core, const QString& siblings) const
    {
        const QString t = QString("%1/cpu%2/topology/").arg(cpuRoot()).arg(cpu);
        write(t + "physical_package_id", QString::number(package));
        write(t + "core_id", QString::number(core));
        write(t + "thread_siblings_list", siblings);
    }

    void setFreq(int cpu, int minKHz, int maxKHz) const
    {
        const QString cf = QString("%1/cpu%2/cpufreq/").arg(cpuRoot()).arg(cpu);
        write(cf + "cpuinfo_min_freq", QString::number(minKHz));
        write(cf + "cpuinfo_max_freq", QString::number(maxKHz));
    }

    // One cache instance as sysfs presents it. `shared` is what deduplicates
    // instances across the CPUs that share them.
    void setCache(int cpu, int index, int level, const QString& type,
                  const QString& size, const QString& shared) const
    {
        const QString c = QString("%1/cpu%2/cache/index%3/").arg(cpuRoot()).arg(cpu).arg(index);
        write(c + "level", QString::number(level));
        write(c + "type", type);
        write(c + "size", size);
        write(c + "shared_cpu_list", shared);
    }

    void setNumaNodes(int count) const
    {
        for (int i = 0; i < count; ++i)
            QVERIFY(QDir().mkpath(QString("%1/devices/system/node/node%2").arg(sysRoot()).arg(i)));
    }

    CPUInfo topology() const
    {
        CPUInfo info;
        CPUDetector::readTopology(info, sysRoot(), procRoot());
        return info;
    }
};

void TstCpuDetector::init()
{
    QVERIFY(m_root.isValid());
    QDir dir(m_root.path());
    for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        QVERIFY(QDir(dir.filePath(entry)).removeRecursively());
}

void TstCpuDetector::singleSocketWithSmt()
{
    // Four logical CPUs, two physical cores, SMT paired 0/2 and 1/3 — the
    // interleaved numbering real hardware uses, so core_id is what identifies a
    // core, not the CPU index.
    setPresent("0-3");
    setTopology(0, 0, 0, "0,2");
    setTopology(1, 0, 1, "1,3");
    setTopology(2, 0, 0, "0,2");
    setTopology(3, 0, 1, "1,3");
    setNumaNodes(1);

    const CPUInfo info = topology();
    QCOMPARE(info.logicalCores, 4);
    QCOMPARE(info.physicalCores, 2);
    QCOMPARE(info.sockets, 1);
    QCOMPARE(info.threadsPerCore, 2);
    QCOMPARE(info.numaNodes, 1);
}

void TstCpuDetector::dualSocket()
{
    // core_id is only unique *within* a package: both sockets number their cores
    // 0 and 1 here, so counting distinct core_ids alone would report 2 physical
    // cores instead of 4.
    setPresent("0-3");
    setTopology(0, 0, 0, "0");
    setTopology(1, 0, 1, "1");
    setTopology(2, 1, 0, "2");
    setTopology(3, 1, 1, "3");
    setNumaNodes(2);

    const CPUInfo info = topology();
    QCOMPARE(info.logicalCores, 4);
    QCOMPARE(info.physicalCores, 4);
    QCOMPARE(info.sockets, 2);
    QCOMPARE(info.threadsPerCore, 1);
    QCOMPARE(info.numaNodes, 2);
}

void TstCpuDetector::hybridCachesAreSummedPerInstance()
{
    // Two SMT P cores (48K L1d each) and two E cores sharing an L2 (32K L1d
    // each), all under one L3 — the shape of an Intel hybrid part.
    setPresent("0-5");
    setTopology(0, 0, 0, "0,1");
    setTopology(1, 0, 0, "0,1");
    setTopology(2, 0, 1, "2,3");
    setTopology(3, 0, 1, "2,3");
    setTopology(4, 0, 2, "4");
    setTopology(5, 0, 3, "5");

    // P cores: private L1s, private 1280K L2.
    for (int cpu : {0, 1, 2, 3}) {
        const QString pair = (cpu < 2) ? "0,1" : "2,3";
        setCache(cpu, 0, 1, "Data", "48K", pair);
        setCache(cpu, 1, 1, "Instruction", "32K", pair);
        setCache(cpu, 2, 2, "Unified", "1280K", pair);
    }
    // E cores: private L1s, one L2 shared by the cluster.
    for (int cpu : {4, 5}) {
        setCache(cpu, 0, 1, "Data", "32K", QString::number(cpu));
        setCache(cpu, 1, 1, "Instruction", "64K", QString::number(cpu));
        setCache(cpu, 2, 2, "Unified", "2048K", "4,5");
    }
    // L3 is shared by everything.
    for (int cpu = 0; cpu < 6; ++cpu)
        setCache(cpu, 3, 3, "Unified", "12M", "0-5");

    const CPUInfo info = topology();
    QCOMPARE(info.physicalCores, 4);          // 2 P + 2 E
    QCOMPARE(info.l1dCacheKiB, 2 * 48 + 2 * 32);   // 160 — not 4 × 48
    QCOMPARE(info.l1iCacheKiB, 2 * 32 + 2 * 64);   // 192
    QCOMPARE(info.l2CacheKiB, 2 * 1280 + 2048);    // two P instances + one E cluster
    QCOMPARE(info.l3CacheKiB, 12 * 1024);          // counted once despite six CPUs
}

void TstCpuDetector::frequencyEnvelopeSpansAllCpus()
{
    // cpu0 is deliberately not the fastest: reading only cpu0 would report
    // 4700 MHz on hardware that reaches 4900, which is what lscpu shows.
    setPresent("0-3");
    setFreq(0, 800000, 4700000);
    setFreq(1, 800000, 4900000);
    setFreq(2, 400000, 3600000);
    setFreq(3, 800000, 3600000);

    const CPUInfo info = topology();
    QCOMPARE(info.maxFreqMHz, 4900.0);
    QCOMPARE(info.baseFreqMHz, 400.0);
}

void TstCpuDetector::missingCpufreqLeavesFrequenciesUnset()
{
    // No cpufreq driver (a VM, or a kernel without it): report nothing rather
    // than a fabricated 0 MHz that the UI would print as a real value.
    setPresent("0-1");
    setTopology(0, 0, 0, "0");
    setTopology(1, 0, 1, "1");

    const CPUInfo info = topology();
    QCOMPARE(info.logicalCores, 2);
    QCOMPARE(info.maxFreqMHz, 0.0);
    QCOMPARE(info.baseFreqMHz, 0.0);
}

void TstCpuDetector::missingSysfsFallsBackToProcCpuinfo()
{
    // A sandbox that mounts /proc but not /sys still gets a logical CPU count.
    write(procRoot() + "/cpuinfo",
          "processor\t: 0\nmodel name\t: Test CPU\n\n"
          "processor\t: 1\nmodel name\t: Test CPU\n");

    const CPUInfo info = topology();
    QCOMPARE(info.logicalCores, 2);
    QCOMPARE(info.physicalCores, 0);   // topology genuinely unknown
    QCOMPARE(info.sockets, 0);
}

void TstCpuDetector::emptyTreeReportsNothing()
{
    // Neither /sys nor /proc readable: every field stays at its default instead
    // of the function inventing a single-core machine.
    const CPUInfo info = topology();
    QCOMPARE(info.logicalCores, 0);
    QCOMPARE(info.physicalCores, 0);
    QCOMPARE(info.sockets, 0);
    QCOMPARE(info.numaNodes, 0);
    QCOMPARE(info.l3CacheKiB, 0);
}

void TstCpuDetector::cacheSizesInMebibytes()
{
    // sysfs writes L2/L3 in whole MiB once they are large enough; the suffix has
    // to be honoured or a 24 MiB L3 reads as 24 KiB.
    setPresent("0");
    setCache(0, 0, 2, "Unified", "2M", "0");
    setCache(0, 1, 3, "Unified", "24M", "0");

    const CPUInfo info = topology();
    QCOMPARE(info.l2CacheKiB, 2 * 1024);
    QCOMPARE(info.l3CacheKiB, 24 * 1024);
}

QTEST_MAIN(TstCpuDetector)
#include "tst_cpudetector.moc"
