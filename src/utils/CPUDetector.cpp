#include "CPUDetector.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Parses cache size strings such as:
//   "512 KiB (12 instances)"   →  512
//   "30 MiB (1 instance)"      →  30720
//   "36864 KB"                 →  36864
//   "32K"                      →  32
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

// Average current CPU frequency by reading cpufreq sysfs entries.
double CPUDetector::readCurrentFreqMHz()
{
    double total = 0.0;
    int    count = 0;

    for (int cpu = 0; cpu < 512; ++cpu) {
        QFile f(QString("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(cpu));
        if (!f.open(QIODevice::ReadOnly))
            break;
        const double kHz = f.readAll().trimmed().toDouble();
        if (kHz > 0.0) {
            total += kHz / 1000.0;
            ++count;
        }
    }
    return count > 0 ? total / count : 0.0;
}

// CPU package temperature from thermal_zone (x86_pkg_temp) or hwmon (coretemp / k10temp).
int CPUDetector::readTemperatureCelsius()
{
    // 1. Try thermal_zone with known CPU types
    const QDir tzDir("/sys/class/thermal");
    const QStringList zones = tzDir.entryList({"thermal_zone*"}, QDir::Dirs);
    for (const QString& zone : zones) {
        QFile typeFile(QString("/sys/class/thermal/%1/type").arg(zone));
        if (!typeFile.open(QIODevice::ReadOnly))
            continue;
        const QString type = typeFile.readAll().trimmed().toLower();
        if (type == "x86_pkg_temp" || type.startsWith("cpu") || type == "acpitz") {
            QFile tempFile(QString("/sys/class/thermal/%1/temp").arg(zone));
            if (tempFile.open(QIODevice::ReadOnly)) {
                const int c = tempFile.readAll().trimmed().toInt() / 1000;
                if (c > 0 && c < 120) return c;
            }
        }
    }

    // 2. Fall back to hwmon (coretemp for Intel, k10temp / zenpower for AMD)
    const QDir hwmonDir("/sys/class/hwmon");
    const QStringList hwmons = hwmonDir.entryList({"hwmon*"}, QDir::Dirs);
    for (const QString& hwmon : hwmons) {
        QFile nameFile(QString("/sys/class/hwmon/%1/name").arg(hwmon));
        if (!nameFile.open(QIODevice::ReadOnly))
            continue;
        const QString name = nameFile.readAll().trimmed().toLower();
        if (name == "coretemp" || name == "k10temp" || name == "zenpower") {
            QFile tempFile(QString("/sys/class/hwmon/%1/temp1_input").arg(hwmon));
            if (tempFile.open(QIODevice::ReadOnly)) {
                const int c = tempFile.readAll().trimmed().toInt() / 1000;
                if (c > 0 && c < 120) return c;
            }
        }
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

CPUInfo CPUDetector::detect()
{
    CPUInfo info;

    QProcess proc;
    proc.start("lscpu", {});
    if (!proc.waitForFinished(4000))
        return info;

    int coresPerSocket = 0;
    int sockets        = 1;

    const QString output = proc.readAllStandardOutput();
    for (const QString& line : output.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString key = line.left(colon).trimmed().toLower();
        const QString val = line.mid(colon + 1).trimmed();
        if (val.isEmpty()) continue;

        if      (key == "model name")          info.modelName    = val;
        else if (key == "vendor id")           info.vendor       = val;
        else if (key == "architecture")        info.architecture = val;
        else if (key == "cpu(s)")              info.logicalCores = val.toInt();
        else if (key == "core(s) per socket")  coresPerSocket    = val.toInt();
        else if (key == "socket(s)")           sockets           = val.toInt();
        else if (key == "cpu max mhz")         info.maxFreqMHz   = val.toDouble();
        else if (key == "cpu min mhz")         info.baseFreqMHz  = val.toDouble();
        // Cache — lscpu reports both "L1d cache:" (old) and "L1d:" (new indented style)
        else if (key == "l1d cache" || key == "l1d")  info.l1dCacheKiB = parseCacheKiB(val);
        else if (key == "l1i cache" || key == "l1i")  info.l1iCacheKiB = parseCacheKiB(val);
        else if (key == "l2 cache"  || key == "l2")   info.l2CacheKiB  = parseCacheKiB(val);
        else if (key == "l3 cache"  || key == "l3")   info.l3CacheKiB  = parseCacheKiB(val);
    }

    if (coresPerSocket > 0)
        info.physicalCores = coresPerSocket * sockets;

    // Fallback: read model name from /proc/cpuinfo if lscpu didn't provide it
    if (info.modelName.isEmpty()) {
        QFile cpuinfo("/proc/cpuinfo");
        if (cpuinfo.open(QIODevice::ReadOnly)) {
            for (const QString& line : QString::fromLocal8Bit(cpuinfo.readAll()).split('\n')) {
                if (line.startsWith("model name")) {
                    const int colon = line.indexOf(':');
                    if (colon >= 0) {
                        info.modelName = line.mid(colon + 1).trimmed();
                        break;
                    }
                }
            }
        }
    }

    info.currentFreqMHz = readCurrentFreqMHz();
    info.temperature    = readTemperatureCelsius();

    return info;
}

CPUInfo CPUDetector::detectDynamic(const CPUInfo& base)
{
    CPUInfo info = base;
    info.currentFreqMHz = readCurrentFreqMHz();
    info.temperature    = readTemperatureCelsius();
    return info;
}
