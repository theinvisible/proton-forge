#ifndef CPUDETECTOR_H
#define CPUDETECTOR_H

#include <QString>

struct CPUInfo {
    QString modelName;
    QString vendor;
    QString architecture;
    int     physicalCores  = 0;
    int     logicalCores   = 0;
    double  baseFreqMHz    = 0.0;
    double  maxFreqMHz     = 0.0;
    double  currentFreqMHz = 0.0;  // average across all cores
    int     l1dCacheKiB    = 0;
    int     l1iCacheKiB    = 0;
    int     l2CacheKiB     = 0;
    int     l3CacheKiB     = 0;
    int     temperature    = 0;    // °C, 0 if unavailable
};

class CPUDetector {
public:
    // Full detection via lscpu + sysfs
    static CPUInfo detect();

    // Refresh only the fast-changing values (freq + temperature)
    static CPUInfo detectDynamic(const CPUInfo& base);

private:
    static double readCurrentFreqMHz();
    static int    readTemperatureCelsius();
    static int    parseCacheKiB(const QString& val);
};

#endif // CPUDETECTOR_H
