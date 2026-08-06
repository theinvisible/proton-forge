// The PCI display-device scan used to be two separate lspci invocations: one for
// the hybrid-graphics probe (2 s budget) and one to enumerate NVIDIA cards when
// the driver could not (3 s). lspci parses ~1.5 MB of pci.ids and needs longer
// than that on a cold page cache, which is how the hybrid probe reported
// "Unknown" on a plainly hybrid laptop and how "System Information" vanished from
// the Help menu (TESTS.md §7). It now reads /sys/bus/pci/devices/*/{class,vendor,
// device,driver} directly — pure file reads, and therefore testable against a
// fixture tree.

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include "utils/GPUDetector.h"

class TstGpuDetector : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void hybridLaptopIsDetected();
    void singleNvidiaIsNotHybrid();
    void optimus3dControllerCounts();
    void integratedOnlyIsNotHybrid();
    void nonDisplayDevicesAreIgnored();
    void missingSysfsStaysUnknown();
    void unreadableAttributesAreSkipped();
    void unknownVendorDoesNotMakeItHybrid();

    void deviceIdIsReported();
    void boundDriverIsReadFromTheSymlink();
    void unboundDeviceHasNoDriver();

private:
    QTemporaryDir m_root;

    QString root() const { return m_root.path(); }

    // One PCI device as the kernel exposes it: a directory named after the
    // address, holding `class` and `vendor` attributes of "0x…" text. A null
    // argument leaves that attribute out entirely.
    void makeDevice(const QString& address, const QString& classCode, const QString& vendorId,
                    const QString& deviceId = QString()) const
    {
        const QString dir = root() + "/" + address;
        QVERIFY(QDir().mkpath(dir));
        if (!classCode.isNull())
            writeAttr(dir + "/class", classCode);
        if (!vendorId.isNull())
            writeAttr(dir + "/vendor", vendorId);
        if (!deviceId.isNull())
            writeAttr(dir + "/device", deviceId);
    }

    // sysfs exposes the bound driver as a symlink into /sys/bus/pci/drivers/<name>.
    void bindDriver(const QString& address, const QString& driver) const
    {
        const QString driverDir = root() + "/drivers/" + driver;
        QVERIFY(QDir().mkpath(driverDir));
        QVERIFY(QFile::link(driverDir, root() + "/" + address + "/driver"));
    }

    void writeAttr(const QString& path, const QString& value) const
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        // sysfs attributes come back with a trailing newline; keep that faithful.
        file.write((value + "\n").toUtf8());
    }

    // Vendors only, in the order displayDevices() reported them.
    QList<GPUInfo::Vendor> vendorsAt(const QString& path) const
    {
        QList<GPUInfo::Vendor> vendors;
        for (const PciDisplayDevice& d : GPUDetector::displayDevices(path))
            vendors << d.vendor;
        return vendors;
    }
};

void TstGpuDetector::init()
{
    QVERIFY(m_root.isValid());
    // Every case starts from an empty tree.
    QDir dir(root());
    for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        QVERIFY(QDir(dir.filePath(entry)).removeRecursively());
}

void TstGpuDetector::hybridLaptopIsDetected()
{
    // The common Optimus arrangement: Intel iGPU on the root complex, NVIDIA
    // dGPU behind a bridge. Both announce themselves as VGA compatible.
    makeDevice("0000:00:02.0", "0x030000", "0x8086");
    makeDevice("0000:01:00.0", "0x030000", "0x10de");

    QCOMPARE(GPUDetector::detectHybridGpu(root()), GPUDetector::HybridGpu::Yes);

    const QList<GPUInfo::Vendor> vendors = vendorsAt(root());
    QCOMPARE(vendors.size(), 2);
    QVERIFY(vendors.contains(GPUInfo::Intel));
    QVERIFY(vendors.contains(GPUInfo::NVIDIA));
}

void TstGpuDetector::singleNvidiaIsNotHybrid()
{
    // Desktop with one discrete card: PRIME offload does not apply, and the
    // caller is entitled to a definite No rather than a lenient Unknown.
    makeDevice("0000:01:00.0", "0x030000", "0x10de");

    QCOMPARE(GPUDetector::detectHybridGpu(root()), GPUDetector::HybridGpu::No);
}

void TstGpuDetector::optimus3dControllerCounts()
{
    // Optimus dGPUs frequently sit at class 0x0302 ("3D controller") instead of
    // 0x0300 — missing that class was the classic way to misread these laptops.
    makeDevice("0000:00:02.0", "0x030000", "0x8086");
    makeDevice("0000:01:00.0", "0x030200", "0x10de");

    QCOMPARE(GPUDetector::detectHybridGpu(root()), GPUDetector::HybridGpu::Yes);
}

void TstGpuDetector::integratedOnlyIsNotHybrid()
{
    // AMD APU, no NVIDIA anywhere.
    makeDevice("0000:00:02.0", "0x030000", "0x1002");

    QCOMPARE(GPUDetector::detectHybridGpu(root()), GPUDetector::HybridGpu::No);
    QCOMPARE(vendorsAt(root()), QList<GPUInfo::Vendor>{GPUInfo::AMD});
}

void TstGpuDetector::nonDisplayDevicesAreIgnored()
{
    // A machine full of PCI devices but no display class at all: nothing to
    // decide on, so Unknown — the state every caller treats leniently.
    makeDevice("0000:00:1f.6", "0x020000", "0x8086");  // ethernet
    makeDevice("0000:00:14.0", "0x0c0330", "0x8086");  // USB controller
    makeDevice("0000:02:00.0", "0x010802", "0x144d");  // NVMe

    QVERIFY(GPUDetector::displayDevices(root()).isEmpty());
    QCOMPARE(GPUDetector::detectHybridGpu(root()), GPUDetector::HybridGpu::Unknown);
}

void TstGpuDetector::missingSysfsStaysUnknown()
{
    // No sysfs at all (a sandbox that does not mount it). Unknown, never No.
    const QString absent = root() + "/does-not-exist";

    QVERIFY(GPUDetector::displayDevices(absent).isEmpty());
    QCOMPARE(GPUDetector::detectHybridGpu(absent), GPUDetector::HybridGpu::Unknown);
}

void TstGpuDetector::unreadableAttributesAreSkipped()
{
    // A device missing `class` cannot be classified, and a display device
    // missing `vendor` cannot be attributed — both drop out rather than
    // poisoning the result.
    makeDevice("0000:00:02.0", QString(), "0x8086");
    makeDevice("0000:01:00.0", "0x030000", QString());
    makeDevice("0000:02:00.0", "0x030000", "0x10de");

    QCOMPARE(vendorsAt(root()), QList<GPUInfo::Vendor>{GPUInfo::NVIDIA});
    QCOMPARE(GPUDetector::detectHybridGpu(root()), GPUDetector::HybridGpu::No);
}

void TstGpuDetector::unknownVendorDoesNotMakeItHybrid()
{
    // A virtual display adapter (QXL here) next to the NVIDIA card is not an
    // iGPU, so PRIME offload still does not apply.
    makeDevice("0000:00:01.0", "0x030000", "0x1b36");
    makeDevice("0000:01:00.0", "0x030000", "0x10de");

    QCOMPARE(GPUDetector::detectHybridGpu(root()), GPUDetector::HybridGpu::No);
}

void TstGpuDetector::deviceIdIsReported()
{
    // The PCI device id identifies the exact part; absent `device` leaves it 0
    // rather than failing the whole entry.
    makeDevice("0000:01:00.0", "0x030000", "0x10de", "0x2d18");
    makeDevice("0000:02:00.0", "0x030000", "0x10de");

    const QList<PciDisplayDevice> devices = GPUDetector::displayDevices(root());
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices[0].address, QString("0000:01:00.0"));
    QCOMPARE(devices[0].deviceId, quint16(0x2d18));
    QCOMPARE(devices[1].deviceId, quint16(0));
}

void TstGpuDetector::boundDriverIsReadFromTheSymlink()
{
    // Which driver claimed the card decides whether DLSS is even possible:
    // NvidiaGPUDetector::detectFromPci() skips an NVIDIA GPU on nouveau.
    makeDevice("0000:01:00.0", "0x030000", "0x10de");
    bindDriver("0000:01:00.0", "nvidia");
    makeDevice("0000:02:00.0", "0x030000", "0x10de");
    bindDriver("0000:02:00.0", "nouveau");

    const QList<PciDisplayDevice> devices = GPUDetector::displayDevices(root());
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices[0].boundDriver, QString("nvidia"));
    QCOMPARE(devices[1].boundDriver, QString("nouveau"));
}

void TstGpuDetector::unboundDeviceHasNoDriver()
{
    // No `driver` symlink: nothing has claimed the device (vfio handover, a
    // blacklisted module). Reported as empty, not guessed at.
    makeDevice("0000:01:00.0", "0x030000", "0x10de");

    const QList<PciDisplayDevice> devices = GPUDetector::displayDevices(root());
    QCOMPARE(devices.size(), 1);
    QVERIFY(devices[0].boundDriver.isEmpty());
}

QTEST_MAIN(TstGpuDetector)
#include "tst_gpudetector.moc"
