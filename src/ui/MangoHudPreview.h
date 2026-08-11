#ifndef MANGOHUDPREVIEW_H
#define MANGOHUDPREVIEW_H

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QList>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <cmath>

// What the MangoHud overlay will look like with the options currently ticked in
// the dialog.
//
// Split in two on purpose, the same way BadgeRow.h is and for the same reason:
// rows() is pure and unit-tested — it decides what appears, in which order and in
// which colour — while draw() only puts that on a QPainter. The decision is the
// part that can be wrong in a way nobody notices.
//
// The readings are fixed samples, not live and not random. A preview whose
// numbers dance while you tick boxes is harder to read, and fixed values are what
// makes the test mean anything.
namespace MangoHudPreview {

// MangoHud's own defaults, from the colour block of its shipped
// MangoHud.conf.example. The dialog does not edit these yet, so they are the only
// truth the preview has; if it ever does, they become another State field.
inline constexpr const char* ColorText       = "#ffffff";
inline constexpr const char* ColorGpu        = "#2e9762";
inline constexpr const char* ColorCpu        = "#2e97cb";
inline constexpr const char* ColorVram       = "#ad64c1";
inline constexpr const char* ColorRam        = "#c26693";
inline constexpr const char* ColorEngine     = "#eb5b5b";
inline constexpr const char* ColorWine       = "#eb5b5b";
inline constexpr const char* ColorFrametime  = "#00ff00";
inline constexpr const char* ColorBackground = "#020202";

// Every option in the dialog that changes what the HUD looks like. Named after
// the MangoHud keys rather than after the widgets, because that is what the
// mapping has to stay true to.
struct State {
    // GPU. gpuStats is the master: MangoHud draws temperature, clocks and power
    // as fields *of the GPU line*, so without it none of them show up. Same for
    // the CPU. The preview inherits that, which is the point — it is how a user
    // finds out that ticking "GPU Temperature" alone does nothing.
    bool gpuStats = false;
    bool gpuTemp = false;
    bool gpuCoreClock = false;
    bool gpuMemClock = false;
    bool gpuPower = false;
    bool gpuName = false;
    bool vulkanDriver = false;
    QString gpuLabel;              // gpu_text: replaces the "GPU" label
    QString detectedGpu;           // what the gpu_name row shows
    QString vulkanDriverName;      // what the vulkan_driver row shows

    // CPU
    bool cpuStats = false;
    bool cpuTemp = false;
    bool cpuPower = false;
    bool cpuMhz = false;
    QString cpuLabel;              // cpu_text: replaces the "CPU" label

    // Metrics
    bool fps = false;
    bool frametime = false;
    bool frameTiming = false;
    bool histogram = false;
    bool showFpsLimit = false;
    QString fpsLimit;              // empty when no limit is active

    // System
    bool ram = false;
    bool swap = false;
    bool vram = false;
    bool resolution = false;
    bool time = false;
    bool arch = false;
    bool version = false;
    bool engineVersion = false;
    bool wine = false;

    // Appearance
    QString position = QStringLiteral("top-left");
    int fontSize = 24;
    double backgroundAlpha = 0.5;
    QString customText;            // custom_text_center
};

struct Row {
    // Header is the centred custom text, Stat is a coloured label plus a value,
    // Plain is one line of coloured text with no label, Graph is the frame-time
    // plot and carries no text at all.
    enum Kind { Header, Stat, Plain, Graph };

    Kind kind = Stat;
    QString label;
    QString value;
    QColor color;   // the label's colour, or the text's when there is no label
};

// The order follows MangoHud's own: header, GPU, CPU, memory, the frame counter
// and its graph, then the one-line facts at the bottom.
inline QList<Row> rows(const State& s)
{
    QList<Row> out;

    auto stat = [](const QString& label, const QString& value, const char* color) {
        return Row{Row::Stat, label, value, QColor(color)};
    };
    auto plain = [](const QString& text, const char* color) {
        return Row{Row::Plain, QString(), text, QColor(color)};
    };

    if (!s.customText.isEmpty()) {
        out += Row{Row::Header, QString(), s.customText, QColor(ColorText)};
    }

    if (s.gpuName) {
        out += plain(s.detectedGpu.isEmpty() ? QStringLiteral("unknown GPU") : s.detectedGpu,
                     ColorGpu);
    }
    if (s.gpuStats) {
        QStringList parts;
        parts << QStringLiteral("87%");
        if (s.gpuTemp)      parts << QStringLiteral("64°C");
        if (s.gpuCoreClock) parts << QStringLiteral("2610 MHz");
        if (s.gpuMemClock)  parts << QStringLiteral("10501 MHz");
        if (s.gpuPower)     parts << QStringLiteral("285 W");
        out += stat(s.gpuLabel.isEmpty() ? QStringLiteral("GPU") : s.gpuLabel,
                    parts.join(QStringLiteral("  ")), ColorGpu);
    }
    if (s.cpuStats) {
        QStringList parts;
        parts << QStringLiteral("34%");
        if (s.cpuTemp)  parts << QStringLiteral("58°C");
        if (s.cpuMhz)   parts << QStringLiteral("4425 MHz");
        if (s.cpuPower) parts << QStringLiteral("88 W");
        out += stat(s.cpuLabel.isEmpty() ? QStringLiteral("CPU") : s.cpuLabel,
                    parts.join(QStringLiteral("  ")), ColorCpu);
    }

    if (s.vram) out += stat(QStringLiteral("VRAM"), QStringLiteral("8.1 GiB"), ColorVram);
    if (s.ram)  out += stat(QStringLiteral("RAM"),  QStringLiteral("14.2 GiB"), ColorRam);
    if (s.swap) out += stat(QStringLiteral("SWAP"), QStringLiteral("0.4 GiB"), ColorRam);

    if (s.fps || s.frametime) {
        QStringList parts;
        if (s.fps)       parts << QStringLiteral("142 FPS");
        if (s.frametime) parts << QStringLiteral("7.04 ms");
        out += stat(QStringLiteral("FPS"), parts.join(QStringLiteral("  ")), ColorText);
    }
    if (s.frameTiming || s.histogram) {
        out += Row{Row::Graph, QString(), QString(), QColor(ColorFrametime)};
    }
    // MangoHud only has a limit to show once one is set; the dialog leaves
    // fpsLimit empty when its checkbox is off.
    if (s.showFpsLimit && !s.fpsLimit.isEmpty()) {
        out += stat(QStringLiteral("Limit"), s.fpsLimit, ColorText);
    }

    if (s.resolution)    out += plain(QStringLiteral("2560x1440"), ColorText);
    if (s.vulkanDriver)  out += plain(s.vulkanDriverName.isEmpty()
                                          ? QStringLiteral("vulkan driver")
                                          : s.vulkanDriverName, ColorEngine);
    if (s.engineVersion) out += plain(QStringLiteral("DXVK 2.4"), ColorEngine);
    if (s.wine)          out += plain(QStringLiteral("Proton 9.0-4"), ColorWine);
    if (s.arch)          out += plain(QStringLiteral("x86_64"), ColorText);
    if (s.version)       out += plain(QStringLiteral("MangoHud"), ColorText);
    if (s.time)          out += plain(QStringLiteral("14:32"), ColorText);

    return out;
}

namespace detail {

inline QFont hudFont(int pixelSize)
{
    // MangoHud aligns its values in columns, which only holds with a fixed pitch.
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(qMax(6, pixelSize));
    return font;
}

// The overlay's geometry, shared by the measuring and the drawing path so the
// two cannot disagree about how big it is.
struct Metrics {
    qreal pad = 0;
    qreal rowHeight = 0;
    qreal gapWidth = 0;
    qreal graphHeight = 0;
    qreal labelWidth = 0;
    qreal valueWidth = 0;   // widest value in the value column, stat rows only
    qreal plainWidth = 0;   // widest label-less row, measured from the left padding
    QSizeF box;
};

inline Metrics metrics(const State& state, const QList<Row>& content)
{
    const QFontMetricsF fm(hudFont(state.fontSize));

    Metrics m;
    m.pad = qMax(3.0, state.fontSize / 4.0);
    m.rowHeight = fm.height();
    m.gapWidth = fm.horizontalAdvance(QStringLiteral("  "));
    m.graphHeight = m.rowHeight * 2.0;

    qreal height = 0.0;
    for (const Row& row : content) {
        if (row.kind == Row::Graph) {
            height += m.graphHeight;
            continue;
        }
        height += m.rowHeight;
        if (row.kind == Row::Stat) {
            m.labelWidth = qMax(m.labelWidth, fm.horizontalAdvance(row.label));
            m.valueWidth = qMax(m.valueWidth, fm.horizontalAdvance(row.value));
        } else {
            // Header and Plain start at the padding, not at the value column, so
            // they must not be measured as if the label column were in front of
            // them — that overstated the box by a whole label width.
            m.plainWidth = qMax(m.plainWidth, fm.horizontalAdvance(row.value));
        }
    }

    m.box = QSizeF(m.pad * 2 + qMax(m.labelWidth + m.gapWidth + m.valueWidth, m.plainWidth),
                   m.pad * 2 + height);
    return m;
}

} // namespace detail

// The overlay's natural size in game pixels — what font_size actually costs.
inline QSizeF boxSize(const State& state)
{
    return detail::metrics(state, rows(state)).box;
}

// 1.0 when the overlay fits `frame` at its real pixel size, less when the preview
// has to shrink it. The panel reports this, because otherwise a font_size of 48
// would look exactly like one of 24.
inline qreal scaleToFit(const State& state, const QSizeF& frame)
{
    const QSizeF box = boxSize(state);
    if (box.isEmpty() || frame.isEmpty()) {
        return 1.0;
    }
    return qMin(1.0, qMin(frame.width() / box.width(), frame.height() / box.height()));
}

// Draws the overlay inside `frame`, which stands for the game window, honouring
// `position`. The HUD is drawn at its real pixel size — a font_size of 24 is 24
// pixels here too, so the preview says something about legibility in game — and
// only scaled down when it would not fit, which is what the return value reports
// (1.0 = shown at full size). Draws nothing when no option is ticked; the caller
// decides what to say about that.
inline qreal draw(QPainter* painter, const QRectF& frame, const State& state)
{
    const QList<Row> content = rows(state);
    if (content.isEmpty()) {
        return 1.0;
    }

    const QFont font = detail::hudFont(state.fontSize);
    const detail::Metrics m = detail::metrics(state, content);

    const qreal pad = m.pad;
    const qreal rowHeight = m.rowHeight;
    const qreal graphHeight = m.graphHeight;
    const qreal labelWidth = m.labelWidth;
    const qreal boxWidth = m.box.width();
    const qreal boxHeight = m.box.height();

    const qreal scale = scaleToFit(state, frame.size());

    // Corner placement happens in unscaled frame coordinates, so a scaled-down
    // HUD still sits flush in the corner the user picked.
    const qreal drawnWidth = boxWidth * scale;
    const qreal drawnHeight = boxHeight * scale;
    const QString& pos = state.position;
    qreal x = frame.left();
    if (pos.endsWith(QStringLiteral("-right"))) {
        x = frame.right() - drawnWidth;
    } else if (pos.endsWith(QStringLiteral("-center"))) {
        x = frame.left() + (frame.width() - drawnWidth) / 2;
    }
    const qreal y = pos.startsWith(QStringLiteral("bottom"))
                        ? frame.bottom() - drawnHeight
                        : frame.top();

    painter->save();
    painter->translate(x, y);
    painter->scale(scale, scale);

    QColor background(ColorBackground);
    background.setAlphaF(qBound(0.0, state.backgroundAlpha, 1.0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    painter->drawRect(QRectF(0, 0, boxWidth, boxHeight));

    painter->setFont(font);
    const qreal valueLeft = pad + labelWidth + m.gapWidth;
    qreal cursor = pad;

    for (const Row& row : content) {
        if (row.kind == Row::Graph) {
            // A deterministic wave rather than random noise: the shape carries no
            // information, and a preview that redraws differently every paint
            // looks broken.
            const QRectF plot(pad, cursor + rowHeight * 0.25,
                              boxWidth - pad * 2, graphHeight - rowHeight * 0.5);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 20));
            painter->drawRect(plot);

            const int samples = 24;
            QList<QPointF> points;
            for (int i = 0; i < samples; ++i) {
                const qreal t = qreal(i) / (samples - 1);
                const qreal wave = 0.5 + 0.35 * std::sin(t * 9.0) * std::cos(t * 3.0);
                points << QPointF(plot.left() + t * plot.width(),
                                  plot.bottom() - wave * plot.height());
            }
            if (state.histogram) {
                // histogram=1 turns MangoHud's frame-time plot into bars.
                painter->setBrush(row.color);
                const qreal barWidth = plot.width() / samples * 0.7;
                for (const QPointF& point : points) {
                    painter->drawRect(QRectF(point.x() - barWidth / 2, point.y(),
                                             barWidth, plot.bottom() - point.y()));
                }
            } else {
                painter->setBrush(Qt::NoBrush);
                painter->setPen(QPen(row.color, qMax(1.0, state.fontSize / 16.0)));
                for (int i = 1; i < points.size(); ++i) {
                    painter->drawLine(points[i - 1], points[i]);
                }
            }
            cursor += graphHeight;
            continue;
        }

        const QRectF line(pad, cursor, boxWidth - pad * 2, rowHeight);
        if (row.kind == Row::Header) {
            painter->setPen(row.color);
            painter->drawText(line, Qt::AlignHCenter | Qt::AlignVCenter, row.value);
        } else if (row.kind == Row::Plain) {
            painter->setPen(row.color);
            painter->drawText(line, Qt::AlignLeft | Qt::AlignVCenter, row.value);
        } else {
            painter->setPen(row.color);
            painter->drawText(QRectF(pad, cursor, labelWidth, rowHeight),
                              Qt::AlignLeft | Qt::AlignVCenter, row.label);
            painter->setPen(QColor(ColorText));
            painter->drawText(QRectF(valueLeft, cursor, boxWidth - pad - valueLeft, rowHeight),
                              Qt::AlignLeft | Qt::AlignVCenter, row.value);
        }
        cursor += rowHeight;
    }

    painter->restore();
    return scale;
}

} // namespace MangoHudPreview

#endif // MANGOHUDPREVIEW_H
