#include "utils/KdeDisplayProbe.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QMap>
#include <QStringList>

bool KdeDisplayProbe::available()
{
    return !QStandardPaths::findExecutable("kscreen-doctor").isEmpty();
}

void KdeDisplayProbe::enrich(QList<DisplayInfo>& displays)
{
    if (displays.isEmpty())
        return;

    QProcess process;
    process.start("kscreen-doctor", QStringList() << "-o");
    process.waitForFinished(3000);
    if (process.error() != QProcess::UnknownError)
        return;

    QString output = QString::fromUtf8(process.readAllStandardOutput());
    output.remove(QRegularExpression("\x1b\\[[0-9;]*m"));  // strip ANSI colour codes

    // Per-output values parsed from the `kscreen-doctor -o` text.
    struct Rec {
        int    w = 0, h = 0;
        double hz = 0.0;
        int    bpc = 0;
        double scale = 0.0;
        DisplayInfo::Vrr vrr = DisplayInfo::Vrr::Unknown;
        QString vrrRaw;
    };
    QMap<QString, Rec> byName;

    // Output blocks start with a line "Output: <id> <connector> <uuid>"; everything
    // up to the next such line belongs to that connector.
    static const QRegularExpression outRe("^Output:\\s*\\d+\\s+(\\S+)");
    // Current mode is the one flagged with '*', e.g. "1:2560x1600@165.00*!".
    static const QRegularExpression modeRe("(\\d+)x(\\d+)@([0-9.]+)\\*");
    static const QRegularExpression vrrRe("Vrr:\\s*(\\w+)");
    // e.g. "Color resolution: automatic (10), range: [6; 12] bits per color".
    static const QRegularExpression bpcRe("Color resolution:[^(]*\\((\\d+)\\)");
    // The compositor's configured scale, e.g. "Scale: 1.25" — more meaningful than
    // Qt's integer buffer scale (KWin hands legacy clients an integer devicePixelRatio).
    static const QRegularExpression scaleRe("Scale:\\s*([0-9.]+)");

    QString curName;
    QStringList curBlock;
    auto flush = [&]() {
        if (curName.isEmpty())
            return;
        const QString block = curBlock.join('\n');
        Rec r;
        const QRegularExpressionMatch mMode = modeRe.match(block);
        if (mMode.hasMatch()) {
            r.w  = mMode.captured(1).toInt();
            r.h  = mMode.captured(2).toInt();
            r.hz = mMode.captured(3).toDouble();
        }
        const QRegularExpressionMatch mVrr = vrrRe.match(block);
        if (mVrr.hasMatch()) {
            r.vrrRaw = mVrr.captured(1);
            const QString v = r.vrrRaw.toLower();
            if (v == "incapable")
                r.vrr = DisplayInfo::Vrr::Unsupported;
            else if (v == "automatic" || v == "always")
                r.vrr = DisplayInfo::Vrr::Supported;
        }
        const QRegularExpressionMatch mBpc = bpcRe.match(block);
        if (mBpc.hasMatch())
            r.bpc = mBpc.captured(1).toInt();
        const QRegularExpressionMatch mScale = scaleRe.match(block);
        if (mScale.hasMatch())
            r.scale = mScale.captured(1).toDouble();
        byName.insert(curName, r);
    };

    const QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        const QRegularExpressionMatch mOut = outRe.match(line);
        if (mOut.hasMatch()) {
            flush();
            curName = mOut.captured(1);
            curBlock.clear();
        } else {
            curBlock << line;
        }
    }
    flush();

    if (byName.isEmpty())
        return;

    for (DisplayInfo& d : displays) {
        Rec r;
        bool have = false;
        if (byName.contains(d.name)) {
            r = byName.value(d.name);
            have = true;
        } else if (displays.size() == 1 && byName.size() == 1) {
            // Single monitor: apply the sole parsed output even if the connector
            // name didn't match QScreen's naming.
            r = byName.first();
            have = true;
        }
        if (!have)
            continue;

        if (r.w > 0 && r.h > 0) {       // authoritative native mode
            d.width = r.w;
            d.height = r.h;
        }
        if (r.hz > 0.0)
            d.refreshRate = r.hz;
        if (r.bpc > 0)
            d.bitsPerColor = r.bpc;
        if (r.scale > 0.0)
            d.scaleFactor = r.scale;
        if (r.vrr != DisplayInfo::Vrr::Unknown) {
            d.vrr = r.vrr;
            d.vrrRaw = r.vrrRaw;
        }
    }
}
