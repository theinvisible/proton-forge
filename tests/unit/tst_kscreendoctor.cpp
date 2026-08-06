// KDE's `kscreen-doctor -o` dump has two independent consumers — KdeDisplayProbe
// (native mode, VRR, bit depth, scale) and HDRChecker (the HDR line) — and each
// used to spawn its own copy, with the timeout return value dropped in both. A
// slow tool therefore read back as a *successful* "HDR: disabled" on a machine
// with HDR on. The output is now fetched once by DisplayDetector and passed in,
// which is also what makes the parsing testable.
//
// The input here is tests/steam-lab/fixtures/kscreen-doctor-o.txt, captured from
// a real Plasma session with its ANSI colour codes intact, so stripAnsi() is on
// the tested path too.

#include <QTest>
#include <QFile>

#include "utils/KScreenDoctor.h"
#include "utils/KdeDisplayProbe.h"
#include "utils/HDRChecker.h"

class TstKScreenDoctor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void ansiCodesAreStripped();
    void connectorFieldsAreParsed();
    void currentModeIsTheStarredOne();
    void nullOutputLeavesTheBaselineAlone();
    void unmatchedConnectorNameIsNotOverwritten();

    void hdrDisabledIsReported();
    void hdrEnabledIsReported();
    void missingHdrLineDoesNotClaimDisabled();

private:
    QString m_raw;        // fixture as captured, ANSI codes included
    QString m_clean;      // after stripAnsi()

    // The QScreen baseline DisplayDetector produces before enrichment.
    static QList<DisplayInfo> baseline(const QString& name)
    {
        DisplayInfo d;
        d.name = name;
        d.width = 2048;          // KWin hands legacy clients the scaled size
        d.height = 1280;
        d.refreshRate = 60.0;
        return {d};
    }
};

void TstKScreenDoctor::initTestCase()
{
    QFile fixture(QStringLiteral(PROTONFORGE_FIXTURES_DIR) + "/kscreen-doctor-o.txt");
    QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.fileName()));
    m_raw = QString::fromUtf8(fixture.readAll());
    QVERIFY(!m_raw.isEmpty());

    m_clean = KScreenDoctor::stripAnsi(m_raw);

    // KDE on Wayland is what both parsers are for; HDRChecker refuses to answer
    // otherwise. Set that up once for the HDR cases below.
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    qputenv("XDG_SESSION_TYPE", "wayland");
}

void TstKScreenDoctor::ansiCodesAreStripped()
{
    QVERIFY(m_raw.contains(QChar(0x1b)));       // the fixture really is colourised
    QVERIFY(!m_clean.contains(QChar(0x1b)));
    QVERIFY(m_clean.contains("HDR: disabled"));  // and the payload survived intact
}

void TstKScreenDoctor::connectorFieldsAreParsed()
{
    QList<DisplayInfo> displays = baseline("eDP-1");
    KdeDisplayProbe().enrich(displays, m_clean);

    QCOMPARE(displays.size(), 1);
    const DisplayInfo& d = displays.first();
    QCOMPARE(d.vrr, DisplayInfo::Vrr::Unsupported);   // "Vrr: incapable"
    QCOMPARE(d.vrrRaw, QString("incapable"));
    QCOMPARE(d.bitsPerColor, 10);                     // "automatic (10)"
    QCOMPARE(d.scaleFactor, 1.25);
}

void TstKScreenDoctor::currentModeIsTheStarredOne()
{
    // The mode list holds 22 entries; only "1:2560x1600@165.00*!" is current.
    // Picking any other one would report the wrong native resolution, and the
    // baseline's scaled 2048x1280 is exactly what this is meant to correct.
    QList<DisplayInfo> displays = baseline("eDP-1");
    KdeDisplayProbe().enrich(displays, m_clean);

    QCOMPARE(displays.first().width, 2560);
    QCOMPARE(displays.first().height, 1600);
    QCOMPARE(displays.first().refreshRate, 165.00);
}

void TstKScreenDoctor::nullOutputLeavesTheBaselineAlone()
{
    // "Could not ask" must not overwrite what QScreen already established.
    QList<DisplayInfo> displays = baseline("eDP-1");
    KdeDisplayProbe().enrich(displays, QString());

    QCOMPARE(displays.first().width, 2048);
    QCOMPARE(displays.first().refreshRate, 60.0);
    QCOMPARE(displays.first().vrr, DisplayInfo::Vrr::Unknown);
}

void TstKScreenDoctor::unmatchedConnectorNameIsNotOverwritten()
{
    // Two displays in the baseline but only eDP-1 in the dump: the sole-output
    // shortcut must not apply, or HDMI-1 would inherit the panel's mode.
    QList<DisplayInfo> displays = baseline("eDP-1");
    DisplayInfo second;
    second.name = "HDMI-1";
    second.width = 1920;
    second.height = 1080;
    displays << second;

    KdeDisplayProbe().enrich(displays, m_clean);

    QCOMPARE(displays[0].width, 2560);   // matched by name
    QCOMPARE(displays[1].width, 1920);   // untouched
    QCOMPARE(displays[1].vrr, DisplayInfo::Vrr::Unknown);
}

void TstKScreenDoctor::hdrDisabledIsReported()
{
    const HDRChecker::HDRStatus status = HDRChecker::checkHDRStatus(m_clean);

    QCOMPARE(status.de, HDRChecker::KDE);
    QVERIFY(status.isSupported);
    QVERIFY(!status.isEnabled);
}

void TstKScreenDoctor::hdrEnabledIsReported()
{
    QString enabled = m_clean;
    enabled.replace("HDR: disabled", "HDR: enabled");

    const HDRChecker::HDRStatus status = HDRChecker::checkHDRStatus(enabled);

    QVERIFY(status.isEnabled);
}

void TstKScreenDoctor::missingHdrLineDoesNotClaimDisabled()
{
    // An older Plasma without the HDR line: the regex finds nothing and the
    // check has to fall through to kwinrc rather than assert "off" from a dump
    // that never mentioned HDR. It ends up reporting disabled here — this pins
    // that it does so without crashing and with the DE still identified.
    QString withoutHdr = m_clean;
    withoutHdr.remove("\tHDR: disabled\n");
    QVERIFY(!withoutHdr.contains("HDR:"));

    const HDRChecker::HDRStatus status = HDRChecker::checkHDRStatus(withoutHdr);
    QCOMPARE(status.de, HDRChecker::KDE);
    QVERIFY(status.isSupported);
}

QTEST_MAIN(TstKScreenDoctor)
#include "tst_kscreendoctor.moc"
