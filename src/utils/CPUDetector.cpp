#include "CPUDetector.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>

// ─────────────────────────────────────────────────────────────────────────────
// Small helpers
// ─────────────────────────────────────────────────────────────────────────────

QString CPUDetector::readSysFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromLocal8Bit(f.readAll()).trimmed();
}

// Parses a cpulist such as "0-11,14,16-19" into individual logical CPU indices.
QList<int> CPUDetector::parseCpuList(const QString& list)
{
    QList<int> out;
    const QString s = list.trimmed();
    if (s.isEmpty())
        return out;
    for (const QString& part : s.split(',', Qt::SkipEmptyParts)) {
        const int dash = part.indexOf('-');
        if (dash < 0) {
            out << part.toInt();
        } else {
            const int a = part.left(dash).toInt();
            const int b = part.mid(dash + 1).toInt();
            for (int i = a; i <= b; ++i)
                out << i;
        }
    }
    return out;
}

// Parses cache size strings such as "512 KiB (12 instances)" → 512 (KiB).
int CPUDetector::parseCacheKiB(const QString& val)
{
    static const QRegularExpression re(
        R"((\d+(?:\.\d+)?)\s*(KiB|MiB|GiB|KB|MB|GB|K|M|G))",
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch m = re.match(val);
    if (!m.hasMatch())
        return 0;

    const double num  = m.captured(1).toDouble();
    const QString unit = m.captured(2).toLower();

    if (unit == "kib" || unit == "kb" || unit == "k") return static_cast<int>(num);
    if (unit == "mib" || unit == "mb" || unit == "m") return static_cast<int>(num * 1024.0);
    if (unit == "gib" || unit == "gb" || unit == "g") return static_cast<int>(num * 1024.0 * 1024.0);
    return 0;
}

// Average current CPU frequency across all cores (MHz).
double CPUDetector::readCurrentFreqMHz()
{
    double total = 0.0;
    int    count = 0;
    for (int cpu = 0; cpu < 512; ++cpu) {
        QFile f(QString("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(cpu));
        if (!f.open(QIODevice::ReadOnly))
            break;
        const double kHz = f.readAll().trimmed().toDouble();
        if (kHz > 0.0) { total += kHz / 1000.0; ++count; }
    }
    return count > 0 ? total / count : 0.0;
}

// Per-core current frequency (MHz), indexed by logical CPU.
QList<double> CPUDetector::readPerCoreFreqMHz()
{
    QList<double> out;
    for (int cpu = 0; cpu < 512; ++cpu) {
        QFile f(QString("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(cpu));
        if (!f.open(QIODevice::ReadOnly))
            break;
        out << f.readAll().trimmed().toDouble() / 1000.0;
    }
    return out;
}

// CPU package temperature (single value) from thermal_zone or hwmon.
int CPUDetector::readTemperatureCelsius()
{
    const QDir tzDir("/sys/class/thermal");
    for (const QString& zone : tzDir.entryList({"thermal_zone*"}, QDir::Dirs)) {
        const QString type = readSysFile(QString("/sys/class/thermal/%1/type").arg(zone)).toLower();
        if (type == "x86_pkg_temp" || type.startsWith("cpu") || type == "acpitz") {
            const int c = readSysFile(QString("/sys/class/thermal/%1/temp").arg(zone)).toInt() / 1000;
            if (c > 0 && c < 120) return c;
        }
    }
    const QDir hwmonDir("/sys/class/hwmon");
    for (const QString& hwmon : hwmonDir.entryList({"hwmon*"}, QDir::Dirs)) {
        const QString name = readSysFile(QString("/sys/class/hwmon/%1/name").arg(hwmon)).toLower();
        if (name == "coretemp" || name == "k10temp" || name == "zenpower") {
            const int c = readSysFile(QString("/sys/class/hwmon/%1/temp1_input").arg(hwmon)).toInt() / 1000;
            if (c > 0 && c < 120) return c;
        }
    }
    return 0;
}

// Labelled temperatures (Package + per-core for Intel coretemp; Tctl/Tdie/Tccd* for AMD).
QList<QPair<QString, int>> CPUDetector::readLabeledTemps()
{
    QList<QPair<QString, int>> out;
    const QDir hwmonDir("/sys/class/hwmon");
    for (const QString& hwmon : hwmonDir.entryList({"hwmon*"}, QDir::Dirs)) {
        const QString base = QString("/sys/class/hwmon/%1").arg(hwmon);
        const QString name = readSysFile(base + "/name").toLower();
        if (name != "coretemp" && name != "k10temp" && name != "zenpower")
            continue;
        for (int i = 1; i < 64; ++i) {
            const QString inputPath = QString("%1/temp%2_input").arg(base).arg(i);
            if (!QFile::exists(inputPath))
                continue;
            const int c = readSysFile(inputPath).toInt() / 1000;
            if (c <= 0 || c >= 120)
                continue;
            QString label = readSysFile(QString("%1/temp%2_label").arg(base).arg(i));
            if (label.isEmpty())
                label = QString("Sensor %1").arg(i);
            out << qMakePair(label, c);
        }
        if (!out.isEmpty())
            break;  // first matching CPU hwmon wins
    }
    return out;
}

double CPUDetector::readLoadAvg1()
{
    const QString s = readSysFile("/proc/loadavg");
    return s.isEmpty() ? 0.0 : s.section(' ', 0, 0).toDouble();
}

// Aggregate CPU jiffies from the first line of /proc/stat.
bool CPUDetector::readAggregateJiffies(quint64& idle, quint64& total)
{
    QFile f("/proc/stat");
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QString line = QString::fromLatin1(f.readLine()).trimmed();  // "cpu  u n s idle iowait ..."
    if (!line.startsWith("cpu"))
        return false;
    const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 5)
        return false;
    quint64 sum = 0;
    for (int i = 1; i < parts.size(); ++i)
        sum += parts[i].toULongLong();
    const quint64 idleJ   = parts[4].toULongLong();                                  // idle
    const quint64 iowaitJ = parts.size() > 5 ? parts[5].toULongLong() : 0;           // iowait
    idle  = idleJ + iowaitJ;
    total = sum;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Identity / features from /proc/cpuinfo (locale- and vendor-neutral)
// ─────────────────────────────────────────────────────────────────────────────

void CPUDetector::readCpuInfoIdentity(CPUInfo& info)
{
    QFile f("/proc/cpuinfo");
    if (!f.open(QIODevice::ReadOnly))
        return;

    // Only need the first processor block for scalar identity + flags.
    QString flagsLine;
    for (const QString& line : QString::fromLocal8Bit(f.readAll()).split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0) {
            if (line.trimmed().isEmpty() && !flagsLine.isEmpty())
                break;  // reached end of first block
            continue;
        }
        const QString key = line.left(colon).trimmed().toLower();
        const QString val = line.mid(colon + 1).trimmed();

        if      (key == "model name" && info.modelName.isEmpty()) info.modelName = val;
        else if (key == "cpu family")  info.cpuFamily = val.toInt();
        else if (key == "model")       info.cpuModel  = val.toInt();
        else if (key == "stepping")    info.stepping  = val.toInt();
        else if (key == "microcode")   info.microcode = val;
        else if (key == "flags" || key == "features") { flagsLine = val; }
    }

    if (flagsLine.isEmpty())
        return;

    QSet<QString> flags;
    for (const QString& t : flagsLine.split(' ', Qt::SkipEmptyParts))
        flags.insert(t);
    auto has = [&flags](const char* f) { return flags.contains(QString::fromLatin1(f)); };

    // SIMD / FP
    if (has("sse4_2"))                 info.simd << "SSE4.2";
    if (has("avx"))                    info.simd << "AVX";
    if (has("avx2"))                   info.simd << "AVX2";
    if (has("avx512f"))                info.simd << "AVX-512";
    if (has("avx_vnni"))               info.simd << "AVX-VNNI";
    if (has("fma"))                    info.simd << "FMA";
    // Crypto
    if (has("aes"))                    info.crypto << "AES";
    if (has("sha_ni") || has("sha1_ni") || has("sha2_ni") || has("sha")) info.crypto << "SHA";
    if (has("vaes"))                   info.crypto << "VAES";
    if (has("pclmulqdq"))              info.crypto << "PCLMULQDQ";
    // Virtualization
    if (has("vmx"))                    info.virtualization = "VT-x";
    else if (has("svm"))               info.virtualization = "AMD-V";
    // Other notable
    if (has("f16c"))                   info.otherIsa << "F16C";
    if (has("bmi2"))                   info.otherIsa << "BMI2";
    if (has("rdrand"))                 info.otherIsa << "RDRAND";
}

// ─────────────────────────────────────────────────────────────────────────────
// Hybrid P/E topology (Intel cpu_core/cpu_atom; absent → homogeneous)
// ─────────────────────────────────────────────────────────────────────────────

void CPUDetector::detectHybrid(CPUInfo& info)
{
    const QString pList = readSysFile("/sys/devices/cpu_core/cpus");
    const QString eList = readSysFile("/sys/devices/cpu_atom/cpus");
    if (pList.isEmpty() || eList.isEmpty())
        return;  // not a hybrid CPU (or kernel doesn't expose the perf PMUs)

    info.pCoreCpus = parseCpuList(pList);
    info.eCoreCpus = parseCpuList(eList);
    info.pThreads  = info.pCoreCpus.size();
    info.eThreads  = info.eCoreCpus.size();

    // Physical core count = distinct topology/core_id within each thread set.
    auto distinctCores = [](const QList<int>& cpus) {
        QSet<int> ids;
        for (int cpu : cpus) {
            const QString id = readSysFile(
                QString("/sys/devices/system/cpu/cpu%1/topology/core_id").arg(cpu));
            if (!id.isEmpty())
                ids.insert(id.toInt());
        }
        return ids.size();
    };
    info.pCores = distinctCores(info.pCoreCpus);
    info.eCores = distinctCores(info.eCoreCpus);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frequency policy: governor / driver / EPP / turbo (Intel + AMD paths)
// ─────────────────────────────────────────────────────────────────────────────

void CPUDetector::readFreqPolicy(CPUInfo& info)
{
    const QString cf = "/sys/devices/system/cpu/cpu0/cpufreq/";
    info.governor      = readSysFile(cf + "scaling_governor");
    info.scalingDriver = readSysFile(cf + "scaling_driver");
    info.epp           = readSysFile(cf + "energy_performance_preference");

    // Turbo/boost: intel_pstate exposes no_turbo (inverted); acpi-cpufreq/amd-pstate
    // expose cpufreq/boost. Prefer whichever exists.
    const QString noTurbo = readSysFile("/sys/devices/system/cpu/intel_pstate/no_turbo");
    if (!noTurbo.isEmpty()) {
        info.turboEnabled = (noTurbo.toInt() == 0) ? 1 : 0;
    } else {
        const QString boost = readSysFile("/sys/devices/system/cpu/cpufreq/boost");
        if (!boost.isEmpty())
            info.turboEnabled = (boost.toInt() == 1) ? 1 : 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Power limits from the powercap tree (Intel-RAPL / AMD). World-readable; the
// live energy_uj counter is root-only on modern kernels and is deliberately skipped.
// ─────────────────────────────────────────────────────────────────────────────

void CPUDetector::readPowerLimits(CPUInfo& info)
{
    const QDir pc("/sys/class/powercap");
    for (const QString& entry : pc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        // Prefer the primary MSR RAPL interface; the parallel "-mmio" provider
        // exposes the same package domain with different (often lower) limits.
        if (entry.contains("mmio"))
            continue;
        const QString base = "/sys/class/powercap/" + entry;
        const QString name = readSysFile(base + "/name").toLower();
        if (!name.startsWith("package"))
            continue;
        const QString pl1 = readSysFile(base + "/constraint_0_power_limit_uw");
        const QString pl2 = readSysFile(base + "/constraint_1_power_limit_uw");
        if (!pl1.isEmpty()) info.powerLimitLongW  = pl1.toDouble() / 1'000'000.0;
        if (!pl2.isEmpty()) info.powerLimitShortW = pl2.toDouble() / 1'000'000.0;
        if (info.powerLimitLongW > 0.0)
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

CPUInfo CPUDetector::detect()
{
    CPUInfo info;

    // lscpu with LC_ALL=C → stable English keys and '.' decimals regardless of locale.
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    proc.setProcessEnvironment(env);
    proc.start("lscpu", {});
    int coresPerSocket = 0;
    int sockets        = 1;
    if (proc.waitForFinished(4000)) {
        const QString output = proc.readAllStandardOutput();
        for (const QString& line : output.split('\n')) {
            const int colon = line.indexOf(':');
            if (colon < 0) continue;
            const QString key = line.left(colon).trimmed().toLower();
            const QString val = line.mid(colon + 1).trimmed();
            if (val.isEmpty()) continue;

            if      (key == "model name")          info.modelName      = val;
            else if (key == "vendor id")           info.vendor         = val;
            else if (key == "architecture")        info.architecture   = val;
            else if (key == "cpu(s)")              info.logicalCores   = val.toInt();
            else if (key == "core(s) per socket")  coresPerSocket      = val.toInt();
            else if (key == "socket(s)")           sockets             = val.toInt();
            else if (key == "thread(s) per core")  info.threadsPerCore = val.toInt();
            else if (key == "numa node(s)")        info.numaNodes      = val.toInt();
            else if (key == "cpu max mhz")         info.maxFreqMHz     = val.toDouble();
            else if (key == "cpu min mhz")         info.baseFreqMHz    = val.toDouble();
            else if (key == "l1d cache" || key == "l1d")  info.l1dCacheKiB = parseCacheKiB(val);
            else if (key == "l1i cache" || key == "l1i")  info.l1iCacheKiB = parseCacheKiB(val);
            else if (key == "l2 cache"  || key == "l2")   info.l2CacheKiB  = parseCacheKiB(val);
            else if (key == "l3 cache"  || key == "l3")   info.l3CacheKiB  = parseCacheKiB(val);
        }
    }

    info.sockets = sockets;
    if (coresPerSocket > 0)
        info.physicalCores = coresPerSocket * sockets;

    // Identity + instruction sets + model-name fallback (locale/vendor-neutral).
    readCpuInfoIdentity(info);

    // Hybrid P/E, frequency policy, power limits (vendor-aware, graceful when absent).
    detectHybrid(info);
    readFreqPolicy(info);
    readPowerLimits(info);

    // Live snapshot.
    info.currentFreqMHz = readCurrentFreqMHz();
    info.perCoreFreqMHz = readPerCoreFreqMHz();
    info.temperature    = readTemperatureCelsius();
    info.tempSensors    = readLabeledTemps();
    info.loadAvg1       = readLoadAvg1();

    // Seed the utilization delta; first real value appears on the next refresh.
    quint64 idle = 0, total = 0;
    if (readAggregateJiffies(idle, total)) {
        info.prevIdleJiffies  = idle;
        info.prevTotalJiffies = total;
    }
    info.cpuUtilization = -1.0;

    return info;
}

CPUInfo CPUDetector::detectDynamic(const CPUInfo& base)
{
    CPUInfo info = base;

    info.currentFreqMHz = readCurrentFreqMHz();
    info.perCoreFreqMHz = readPerCoreFreqMHz();
    info.temperature    = readTemperatureCelsius();
    info.tempSensors    = readLabeledTemps();
    info.loadAvg1       = readLoadAvg1();

    // Governor / turbo can change at runtime — cheap to re-read.
    readFreqPolicy(info);

    // CPU utilization from the jiffies delta against the previous sample.
    quint64 idle = 0, total = 0;
    if (readAggregateJiffies(idle, total) &&
        total > base.prevTotalJiffies && idle >= base.prevIdleJiffies) {
        const double dTotal = static_cast<double>(total - base.prevTotalJiffies);
        const double dIdle  = static_cast<double>(idle  - base.prevIdleJiffies);
        info.cpuUtilization = dTotal > 0.0 ? (dTotal - dIdle) / dTotal * 100.0 : -1.0;
        info.prevIdleJiffies  = idle;
        info.prevTotalJiffies = total;
    }

    return info;
}
