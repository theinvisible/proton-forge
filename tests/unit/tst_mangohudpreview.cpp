// The preview's job is to answer "what will the overlay look like with these
// options ticked", and the half that can be wrong without anybody noticing is
// which rows appear at all — a screenshot review would not catch a row that is
// merely in the wrong order, or a sub-option that silently does nothing.
//
// So rows() is pure and asserted here; draw() only paints what this returns.
// Nothing below asserts pixels or absolute widths, only the mapping: master
// toggles gate their sub-options the way MangoHud itself does, labels can be
// overridden, and the order is MangoHud's.

#include <QTest>

#include "ui/MangoHudPreview.h"

using MangoHudPreview::Row;
using MangoHudPreview::State;

class TstMangoHudPreview : public QObject
{
    Q_OBJECT

private slots:
    void nothingEnabledDrawsNothing();
    void gpuStatsGatesItsOwnSubOptions();
    void cpuStatsGatesItsOwnSubOptions();
    void labelFieldsRenameTheStatRows();
    void gpuNameIsItsOwnRowAndSurvivesNoDetection();
    void fpsLimitRowNeedsALimitToShow();
    void graphAppearsForEitherPlotOption();
    void rowOrderFollowsMangoHud();
    void statColoursAreMangoHudsOwn();
    void fontSizeChangesTheReportedBoxSize();
    void labellessRowsAreMeasuredFromTheLeftEdge();
    void scaleToFitOnlyShrinks();

private:
    static QStringList labels(const QList<Row>& rows)
    {
        QStringList out;
        for (const Row& row : rows) {
            out << (row.label.isEmpty() ? row.value : row.label);
        }
        return out;
    }

    static Row rowWithLabel(const QList<Row>& rows, const QString& label)
    {
        for (const Row& row : rows) {
            if (row.label == label) {
                return row;
            }
        }
        return {};
    }
};

void TstMangoHudPreview::nothingEnabledDrawsNothing()
{
    // The canvas keys its "the overlay would be empty" hint off exactly this.
    QVERIFY(MangoHudPreview::rows(State{}).isEmpty());
}

void TstMangoHudPreview::gpuStatsGatesItsOwnSubOptions()
{
    // MangoHud draws temperature, clocks and power as fields of the GPU line, so
    // without gpu_stats there is no line to put them on. Ticking only
    // "GPU Temperature" therefore has to show nothing — that is the feedback the
    // preview exists to give.
    State s;
    s.gpuTemp = true;
    s.gpuPower = true;
    QVERIFY(MangoHudPreview::rows(s).isEmpty());

    s.gpuStats = true;
    const Row gpu = rowWithLabel(MangoHudPreview::rows(s), "GPU");
    QCOMPARE(gpu.kind, Row::Stat);
    QVERIFY(gpu.value.contains("°C"));
    QVERIFY(gpu.value.contains(" W"));
    QVERIFY(!gpu.value.contains("MHz"));   // neither clock was asked for
}

void TstMangoHudPreview::cpuStatsGatesItsOwnSubOptions()
{
    State s;
    s.cpuTemp = true;
    s.cpuMhz = true;
    QVERIFY(MangoHudPreview::rows(s).isEmpty());

    s.cpuStats = true;
    const Row cpu = rowWithLabel(MangoHudPreview::rows(s), "CPU");
    QVERIFY(cpu.value.contains("°C"));
    QVERIFY(cpu.value.contains("MHz"));
    QVERIFY(!cpu.value.contains(" W"));
}

void TstMangoHudPreview::labelFieldsRenameTheStatRows()
{
    // cpu_text/gpu_text replace the label on the stat line in MangoHud; they are
    // not extra rows.
    State s;
    s.gpuStats = true;
    s.cpuStats = true;
    s.gpuLabel = "RTX 4090";
    s.cpuLabel = "Ryzen 9 7950X";

    const QList<Row> rows = MangoHudPreview::rows(s);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(labels(rows), QStringList({"RTX 4090", "Ryzen 9 7950X"}));
}

void TstMangoHudPreview::gpuNameIsItsOwnRowAndSurvivesNoDetection()
{
    State s;
    s.gpuName = true;
    s.detectedGpu = "GeForce RTX 4090";
    QList<Row> rows = MangoHudPreview::rows(s);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().kind, Row::Plain);
    QCOMPARE(rows.first().value, QString("GeForce RTX 4090"));

    // A machine we could not read still has to show the row, or the checkbox
    // would look broken.
    s.detectedGpu.clear();
    rows = MangoHudPreview::rows(s);
    QCOMPARE(rows.size(), 1);
    QVERIFY(!rows.first().value.isEmpty());
}

void TstMangoHudPreview::fpsLimitRowNeedsALimitToShow()
{
    State s;
    s.showFpsLimit = true;
    QVERIFY(MangoHudPreview::rows(s).isEmpty());   // nothing to report yet

    s.fpsLimit = "60";
    QCOMPARE(rowWithLabel(MangoHudPreview::rows(s), "Limit").value, QString("60"));

    // And the row belongs to show_fps_limit, not to the limit itself.
    s.showFpsLimit = false;
    QVERIFY(MangoHudPreview::rows(s).isEmpty());
}

void TstMangoHudPreview::graphAppearsForEitherPlotOption()
{
    State s;
    s.frameTiming = true;
    QCOMPARE(MangoHudPreview::rows(s).size(), 1);
    QCOMPARE(MangoHudPreview::rows(s).first().kind, Row::Graph);

    // histogram=1 turns the same plot into bars rather than adding a second one.
    s.histogram = true;
    QCOMPARE(MangoHudPreview::rows(s).size(), 1);

    s.frameTiming = false;
    QCOMPARE(MangoHudPreview::rows(s).size(), 1);
}

void TstMangoHudPreview::rowOrderFollowsMangoHud()
{
    State s;
    s.customText = "Witcher 3";
    s.gpuStats = true;
    s.cpuStats = true;
    s.vram = true;
    s.ram = true;
    s.fps = true;
    s.frameTiming = true;
    s.time = true;

    const QList<Row> rows = MangoHudPreview::rows(s);
    QCOMPARE(rows.first().kind, Row::Header);
    QCOMPARE(labels(rows),
             QStringList({"Witcher 3", "GPU", "CPU", "VRAM", "RAM", "FPS", "", "14:32"}));
}

void TstMangoHudPreview::statColoursAreMangoHudsOwn()
{
    State s;
    s.gpuStats = true;
    s.cpuStats = true;
    s.vram = true;

    const QList<Row> rows = MangoHudPreview::rows(s);
    QCOMPARE(rowWithLabel(rows, "GPU").color, QColor(MangoHudPreview::ColorGpu));
    QCOMPARE(rowWithLabel(rows, "CPU").color, QColor(MangoHudPreview::ColorCpu));
    QCOMPARE(rowWithLabel(rows, "VRAM").color, QColor(MangoHudPreview::ColorVram));
}

void TstMangoHudPreview::fontSizeChangesTheReportedBoxSize()
{
    // The panel tells the user how many game pixels the overlay costs, so the
    // measurement has to move with font_size. Font metrics differ per system, so
    // this asserts the relationship, not the numbers.
    State small;
    small.fps = true;
    small.fontSize = 12;

    State large = small;
    large.fontSize = 48;

    QVERIFY(MangoHudPreview::boxSize(large).width() > MangoHudPreview::boxSize(small).width());
    QVERIFY(MangoHudPreview::boxSize(large).height() > MangoHudPreview::boxSize(small).height());
}

void TstMangoHudPreview::labellessRowsAreMeasuredFromTheLeftEdge()
{
    // The panel reports the overlay's size in game pixels, so the measurement has
    // to match where things are actually drawn: a Plain row starts at the padding,
    // not behind the label column. Measuring it as a value made the box wider than
    // the widest row.
    State s;
    s.gpuStats = true;                 // a stat row, so there is a label column
    s.gpuName = true;
    s.detectedGpu = "GPU";             // shorter than the stat row: no influence
    const qreal narrow = MangoHudPreview::boxSize(s).width();

    State wide = s;
    wide.detectedGpu = "NVIDIA GeForce RTX 4090 Founders Edition";
    QVERIFY(MangoHudPreview::boxSize(wide).width() > narrow);

    // And the long name alone, without any label column, must not be padded by one.
    State plainOnly;
    plainOnly.gpuName = true;
    plainOnly.detectedGpu = wide.detectedGpu;
    QVERIFY(MangoHudPreview::boxSize(plainOnly).width()
            <= MangoHudPreview::boxSize(wide).width());
}

void TstMangoHudPreview::scaleToFitOnlyShrinks()
{
    State s;
    s.fps = true;
    s.fontSize = 24;

    const QSizeF box = MangoHudPreview::boxSize(s);
    // Plenty of room: never magnify, because then font_size would stop meaning
    // anything in the preview.
    QCOMPARE(MangoHudPreview::scaleToFit(s, box * 4), 1.0);
    QVERIFY(MangoHudPreview::scaleToFit(s, box / 2) < 1.0);
    // A canvas with no size yet must not produce a zero or negative scale.
    QCOMPARE(MangoHudPreview::scaleToFit(s, QSizeF()), 1.0);
}

QTEST_MAIN(TstMangoHudPreview)
#include "tst_mangohudpreview.moc"
