#include "utils/CompatToolResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

#include "parsers/VDFParser.h"

namespace CompatToolResolver {

namespace {

// "Proton 11.0" -> 11.0, "Proton 9.0 (Beta)" -> 9.0, "Proton 5.13" -> 5.13.
QVersionNumber valveDirVersion(const QString& dirName)
{
    static const QRegularExpression re(R"(^Proton (\d+)(?:\.(\d+))?)");
    const QRegularExpressionMatch m = re.match(dirName);
    if (!m.hasMatch()) return {};
    return QVersionNumber(m.captured(1).toInt(), m.captured(2).toInt());
}

// What "newest" compares: the full version where known, else what the
// directory name says (a Valve build without a readable version file).
QVersionNumber sortKey(const ToolInfo& t)
{
    if (!t.version.isNull()) return t.version;
    if (t.kind == Kind::Valve) return valveDirVersion(t.dirName);
    return {};
}

bool newerThan(const ToolInfo& a, const ToolInfo& b)
{
    const QVersionNumber ka = sortKey(a), kb = sortKey(b);
    if (ka != kb) return ka > kb;
    return a.dirName > b.dirName;   // deterministic tie-break only
}

QString readFirstLine(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

// Tools registered by one compatibilitytool.vdf, resolved to absolute paths.
QList<ToolInfo> toolsFromManifest(const QString& manifestPath)
{
    QList<ToolInfo> out;
    VDFParser parser;
    if (!parser.parseFile(manifestPath)) return out;

    const VDFNode root = parser.root();
    if (!root.hasChild("compatibilitytools")) return out;
    const VDFNode tools = root.child("compatibilitytools").child("compat_tools");

    const QDir base = QFileInfo(manifestPath).absoluteDir();
    const QMap<QString, VDFNode> entries = tools.children();
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const QString installPath = it.value().getString("install_path", ".");
        const QString toolPath = QDir::cleanPath(base.filePath(installPath));
        const ToolInfo info = inspect(toolPath, it.key());
        if (info.isValid()) out << info;
    }
    return out;
}

void appendUnique(QList<ToolInfo>& out, QSet<QString>& seen, const ToolInfo& info)
{
    const QString canonical = QFileInfo(info.path).canonicalFilePath();
    const QString key = canonical.isEmpty() ? info.path : canonical;
    if (seen.contains(key)) return;
    seen.insert(key);
    out << info;
}

}  // namespace

FeatureGate::Fork forkOf(Kind kind)
{
    switch (kind) {
        case Kind::CachyOS: return FeatureGate::Fork::CachyOS;
        case Kind::GE:      return FeatureGate::Fork::GE;
        case Kind::Valve:   return FeatureGate::Fork::Valve;
        case Kind::ValveExperimental:
        case Kind::ValveHotfix:
        case Kind::Other:   break;
    }
    return FeatureGate::Fork::Unknown;
}

QVersionNumber parseCachyOS(const QString& name)
{
    static const QRegularExpression re(R"(proton-cachyos-(\d+)\.(\d+)-(\d+))",
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(name);
    if (!m.hasMatch()) return {};
    return QVersionNumber(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
}

QVersionNumber parseGE(const QString& name)
{
    static const QRegularExpression re(R"(GE-Proton(\d+)-(\d+))",
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(name);
    if (!m.hasMatch()) return {};
    return QVersionNumber(m.captured(1).toInt(), m.captured(2).toInt(), 0);
}

QVersionNumber parseValveVersionFile(const QString& text)
{
    // "<build timestamp> proton-<major>.<minor>-<release><suffix>-<arch>",
    // e.g. "1788504981 proton-11.0-2c-x86_64" for 11.0-2c.
    static const QRegularExpression re(R"(\bproton-(\d+)\.(\d+)-(\d+))");
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return {};
    return QVersionNumber(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
}

Kind classify(const QString& dirName)
{
    if (dirName.startsWith("proton-cachyos", Qt::CaseInsensitive)) return Kind::CachyOS;
    if (dirName.startsWith("GE-Proton", Qt::CaseInsensitive)) return Kind::GE;
    if (dirName.compare("Proton - Experimental", Qt::CaseInsensitive) == 0)
        return Kind::ValveExperimental;
    if (dirName.compare("Proton Hotfix", Qt::CaseInsensitive) == 0) return Kind::ValveHotfix;
    if (!valveDirVersion(dirName).isNull()) return Kind::Valve;
    return Kind::Other;
}

ToolInfo inspect(const QString& path, const QString& internalName)
{
    ToolInfo info;
    if (!QFile::exists(path + "/proton")) return info;

    info.path = path;
    info.dirName = QFileInfo(path).fileName();
    info.internalName = internalName.isEmpty() ? info.dirName : internalName;
    info.kind = classify(info.dirName);

    switch (info.kind) {
        case Kind::CachyOS: info.version = parseCachyOS(info.dirName); break;
        case Kind::GE:      info.version = parseGE(info.dirName); break;
        case Kind::Valve:
            info.version = parseValveVersionFile(readFirstLine(path + "/version"));
            break;
        case Kind::ValveExperimental:
        case Kind::ValveHotfix:
        case Kind::Other:
            break;
    }
    return info;
}

QList<ToolInfo> scanCompatDirs(const QStringList& dirs)
{
    QList<ToolInfo> out;
    QSet<QString> seen;

    for (const QString& dirPath : dirs) {
        if (dirPath.isEmpty()) continue;
        const QDir dir(dirPath);
        if (!dir.exists()) continue;

        // A manifest may also sit directly in compatibilitytools.d.
        const QString topManifest = dir.filePath("compatibilitytool.vdf");
        if (QFile::exists(topManifest)) {
            for (const ToolInfo& t : toolsFromManifest(topManifest))
                appendUnique(out, seen, t);
        }

        for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString sub = dir.filePath(entry);
            const QString manifest = sub + "/compatibilitytool.vdf";
            QList<ToolInfo> registered;
            if (QFile::exists(manifest)) registered = toolsFromManifest(manifest);

            if (registered.isEmpty()) {
                // No (usable) manifest: the directory name is the only name.
                const ToolInfo info = inspect(sub);
                if (info.isValid()) appendUnique(out, seen, info);
            } else {
                for (const ToolInfo& t : registered) appendUnique(out, seen, t);
            }
        }
    }
    return out;
}

QList<ToolInfo> scanLibraries(const QStringList& libraryPaths)
{
    QList<ToolInfo> out;
    QSet<QString> seen;

    for (const QString& lib : libraryPaths) {
        if (lib.isEmpty()) continue;
        const QDir common(lib + "/common");
        if (!common.exists()) continue;
        for (const QString& entry : common.entryList({"Proton*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const ToolInfo info = inspect(common.filePath(entry));
            if (info.isValid()) appendUnique(out, seen, info);
        }
    }
    return out;
}

QStringList systemCompatDirs()
{
    QStringList dirs = {
        QStringLiteral("/usr/share/steam/compatibilitytools.d"),
        QStringLiteral("/usr/local/share/steam/compatibilitytools.d"),
    };
    const QString extra = qEnvironmentVariable("STEAM_EXTRA_COMPAT_TOOLS_PATHS");
    for (const QString& p : extra.split(':', Qt::SkipEmptyParts))
        dirs << p;
    return dirs;
}

ToolInfo newest(const QList<ToolInfo>& tools, Kind kind)
{
    ToolInfo best;
    for (const ToolInfo& t : tools) {
        if (t.kind != kind) continue;
        if (!best.isValid() || newerThan(t, best)) best = t;
    }
    return best;
}

ToolInfo resolveMappedName(const QString& name, const QList<ToolInfo>& tools)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return {};

    for (const ToolInfo& t : tools)
        if (t.internalName == n) return t;
    for (const ToolInfo& t : tools)
        if (t.dirName == n) return t;

    if (n == QLatin1String("proton_experimental")) return newest(tools, Kind::ValveExperimental);
    if (n == QLatin1String("proton_hotfix")) return newest(tools, Kind::ValveHotfix);

    // Both aliases name the current stable release (one alias list in Steam's
    // SteamPlay manifest carries "proton-stable,proton-next,proton_next").
    if (n == QLatin1String("proton-stable") || n == QLatin1String("proton-next") ||
        n == QLatin1String("proton_next")) {
        return newest(tools, Kind::Valve);
    }

    // proton_11 = "Proton 11.0", proton_5 = "Proton 5.0"; legacy names squash
    // the dot: proton_63 = 6.3, proton_513 = 5.13, proton_37 = 3.7.
    static const QRegularExpression numbered(R"(^proton_(\d+)$)");
    const QRegularExpressionMatch m = numbered.match(n);
    if (m.hasMatch()) {
        const QString digits = m.captured(1);
        int major = digits.toInt();
        int minor = 0;
        if (major > 20) {
            major = digits.left(1).toInt();
            minor = digits.mid(1).toInt();
        }
        ToolInfo best;
        for (const ToolInfo& t : tools) {
            if (t.kind != Kind::Valve) continue;
            const QVersionNumber dv = valveDirVersion(t.dirName);
            if (dv.majorVersion() != major) continue;
            if (dv.minorVersion() != minor) continue;
            if (!best.isValid() || newerThan(t, best)) best = t;
        }
        return best;
    }

    return {};
}

ToolInfo defaultChoice(const QList<ToolInfo>& tools)
{
    for (Kind kind : {Kind::CachyOS, Kind::GE, Kind::ValveExperimental, Kind::Valve,
                      Kind::ValveHotfix}) {
        const ToolInfo t = newest(tools, kind);
        if (t.isValid()) return t;
    }
    return {};
}

ToolInfo latestSteam(const QList<ToolInfo>& tools)
{
    const ToolInfo experimental = newest(tools, Kind::ValveExperimental);
    if (experimental.isValid()) return experimental;
    return newest(tools, Kind::Valve);
}

QList<ToolInfo> sortedNewestFirst(QList<ToolInfo> tools)
{
    std::stable_sort(tools.begin(), tools.end(), newerThan);
    return tools;
}

}  // namespace CompatToolResolver
