// The hybrid-graphics probe used to shell out to lspci with a 2 s timeout. On a
// cold page cache lspci needs longer than that (it parses ~1.5 MB of pci.ids),
// so the probe intermittently reported "Unknown" on machines that are plainly
// hybrid — and the PRIME-offload warning silently disappeared with it. It now
// reads /sys/bus/pci/devices/*/{class,vendor} directly, which is a pure file
// read and therefore fully testable against a fixture tree.

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

private:
    QTemporaryDir m_root;

    QString root() const { return m_root.path(); }

    // One PCI device as the kernel exposes it: a directory named after the
    // address, holding a `class` and a `vendor` attribute of "0x…" text.
    void makeDevice(const QString& address, const QString& classCode, const QString& vendorId) const
    {
        const QString dir = root() + "/" + address;
        QVERIFY(QDir().mkpath(dir));
        if (!classCode.isNull())
            writeAttr(dir + "/class", classCode);
        if (!vendorId.isNull())
            writeAttr(dir + "/vendor", vendorId);
    }

    void writeAttr(const QString& path, const QString& value) const
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        // sysfs attributes come back with a trailing newline; keep that faithful.
        file.write((value + "\n").toUtf8());
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

    const QList<GPUInfo::Vendor> vendors = GPUDetector::displayDeviceVendors(root());
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
    QCOMPARE(GPUDetector::displayDeviceVendors(root()),
             QList<GPUInfo::Vendor>{GPUInfo::AMD});
}

void TstGpuDetector::nonDisplayDevicesAreIgnored()
{
    // A machine full of PCI devices but no display class at all: nothing to
    // decide on, so Unknown — the state every caller treats leniently.
    makeDevice("0000:00:1f.6", "0x020000", "0x8086");  // ethernet
    makeDevice("0000:00:14.0", "0x0c0330", "0x8086");  // USB controller
    makeDevice("0000:02:00.0", "0x010802", "0x144d");  // NVMe

    QVERIFY(GPUDetector::displayDeviceVendors(root()).isEmpty());
    QCOMPARE(GPUDetector::detectHybridGpu(root()), GPUDetector::HybridGpu::Unknown);
}

void TstGpuDetector::missingSysfsStaysUnknown()
{
    // No sysfs at all (a sandbox that does not mount it). Unknown, never No.
    const QString absent = root() + "/does-not-exist";

    QVERIFY(GPUDetector::displayDeviceVendors(absent).isEmpty());
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

    QCOMPARE(GPUDetector::displayDeviceVendors(root()),
             QList<GPUInfo::Vendor>{GPUInfo::NVIDIA});
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

QTEST_MAIN(TstGpuDetector)
#include "tst_gpudetector.moc"
