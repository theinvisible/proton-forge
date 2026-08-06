#ifndef CPUDETECTOR_H
#define CPUDETECTOR_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>

struct CPUInfo {
    QString modelName;
    QString vendor;
    QString architecture;

    int     physicalCores  = 0;
    int     logicalCores   = 0;
    int     threadsPerCore = 0;
    int     sockets        = 0;
    int     numaNodes      = 0;

    // Hybrid topology (Intel P/E, some ARM big.LITTLE). 0 = homogeneous/unknown.
    int     pCores   = 0;   // physical performance cores
    int     eCores   = 0;   // physical efficiency cores
    int     pThreads = 0;   // logical CPUs on P cores
    int     eThreads = 0;   // logical CPUs on E cores

    int     cpuFamily = 0;
    int     cpuModel  = 0;
    int     stepping  = 0;
    QString microcode;
    QString virtualization;        // "VT-x" / "AMD-V" / empty

    double  baseFreqMHz    = 0.0;  // lowest cpuinfo_min_freq — shown as "Min Frequency"
    double  maxFreqMHz     = 0.0;  // highest cpuinfo_max_freq across all CPUs
    double  currentFreqMHz = 0.0;  // average across all cores

    int     l1dCacheKiB    = 0;
    int     l1iCacheKiB    = 0;
    int     l2CacheKiB     = 0;
    int     l3CacheKiB     = 0;

    // Curated instruction-set extensions actually present on this CPU.
    QStringList simd;              // SSE4.2, AVX, AVX2, AVX-512, FMA, AVX-VNNI
    QStringList crypto;            // AES, SHA, VAES, PCLMULQDQ
    QStringList otherIsa;

    // Power & frequency policy.
    QString governor;
    QString scalingDriver;
    QString epp;                   // energy_performance_preference
    int     turboEnabled     = -1; // -1 unknown, 0 off, 1 on
    double  powerLimitLongW  = 0.0;// PL1 (long-term)
    double  powerLimitShortW = 0.0;// PL2 (short-term)

    // Live / fast-changing.
    int     temperature    = 0;    // °C package, 0 if unavailable
    double  cpuUtilization = -1.0; // %, -1 = not yet sampled
    double  loadAvg1       = 0.0;  // 1-minute load average
    QList<double>              perCoreFreqMHz;  // indexed by logical CPU
    QList<QPair<QString, int>> tempSensors;     // label → °C (Package, Core N, Tctl, Tccd*)
    QList<int>                 pCoreCpus;        // logical CPU indices on P cores (hybrid)
    QList<int>                 eCoreCpus;        // logical CPU indices on E cores (hybrid)

    // Internal: previous /proc/stat aggregate jiffies, for the utilization delta.
    quint64 prevIdleJiffies  = 0;
    quint64 prevTotalJiffies = 0;
};

// All-static CPU probe. Everything comes from /proc and /sys — no subprocess, so
// nothing here can time out or depend on a tool being installed, and the parsing
// is locale-independent by construction. The few vendor-specific values (turbo
// path, temperature sensor, hybrid split, RAPL power limits) are read by trying
// known Intel *and* AMD paths and are simply omitted when unavailable.
class CPUDetector {
public:
    // Full detection from /proc + /sys.
    static CPUInfo detect();

    // Refresh only the fast-changing values (freq, per-core freq, temps, utilization,
    // load average, governor/turbo). Carries everything else over from `base`.
    static CPUInfo detectDynamic(const CPUInfo& base);

    // Core counts, sockets, NUMA nodes, the frequency envelope and cache sizes —
    // the part that used to be parsed out of lscpu's output. The roots are
    // parameters so tests can point at a fixture tree, the same way
    // GPUDetector::displayDeviceVendors() takes a pciRoot.
    static void readTopology(CPUInfo& info,
                             const QString& sysRoot  = QStringLiteral("/sys"),
                             const QString& procRoot = QStringLiteral("/proc"));

private:
    static void readCacheSizes(CPUInfo& info, const QString& cpuRoot, const QList<int>& cpus);

    static int    parseCacheKiB(const QString& val);
    static double readCurrentFreqMHz();
    static int    readTemperatureCelsius();

    // Small sysfs/proc helpers.
    static QString    readSysFile(const QString& path);   // trimmed contents, or empty
    static QList<int> parseCpuList(const QString& list);  // "0-11,14" → [0..11,14]

    // Feature/identity reads.
    static void readCpuInfoIdentity(CPUInfo& info);       // family/model/stepping/microcode/flags/model-name
    static void detectHybrid(CPUInfo& info);              // cpu_core / cpu_atom
    static void readFreqPolicy(CPUInfo& info);            // governor/driver/epp/turbo
    static void readPowerLimits(CPUInfo& info);           // powercap PL1/PL2

    // Live reads.
    static QList<double>              readPerCoreFreqMHz();
    static QList<QPair<QString, int>> readLabeledTemps();
    static double                     readLoadAvg1();
    static bool                       readAggregateJiffies(quint64& idle, quint64& total);
};

#endif // CPUDETECTOR_H
