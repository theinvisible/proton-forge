#include "utils/ExecutableFinder.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace ExecutableFinder {

namespace {

bool cancelled(const CancelFlag& cancel)
{
    return cancel && cancel->load();
}

// Shallower first — the main binary rarely hides deeper than its helpers —
// then by path, so the order does not depend on the filesystem.
void sortByDepth(QStringList& paths)
{
    std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
        const int depthA = a.count('/');
        const int depthB = b.count('/');
        if (depthA != depthB) return depthA < depthB;
        return a < b;
    });
}

bool isWindowsJunk(const QString& lowerName)
{
    static const char* const parts[] = {
        "unins", "setup", "install", "crash", "report", "redist", "vcredist",
        "directx", "dotnet", "dxsetup", "easyanticheat", "battleye",
        "regroup",   // log utilities
        "tts",       // text-to-speech helpers
        "voice",
    };
    for (const char* part : parts) {
        if (lowerName.contains(QLatin1String(part))) return true;
    }
    return lowerName == QLatin1String("ucc.exe");   // UT2004's compiler, not the game
}

bool isLinuxJunk(const QString& lowerName)
{
    static const char* const extensions[] = {
        ".txt", ".log", ".md", ".json", ".xml", ".cfg", ".ini", ".conf", ".yaml",
        ".dat", ".pak", ".csv", ".sh", ".py", ".pl", ".so", ".a", ".o",
        ".png", ".jpg", ".jpeg", ".bmp", ".svg", ".ico", ".ttf", ".otf", ".woff",
        ".mp3", ".ogg", ".wav", ".mp4", ".avi", ".mkv",
    };
    for (const char* ext : extensions) {
        if (lowerName.endsWith(QLatin1String(ext))) return true;
    }
    static const char* const parts[] = {
        "uninstall", "setup", "install", "update", "crash", "report",
        "readme", "license", "changelog",
        "chrome-sandbox",   // Electron / CEF helper next to the real binary
    };
    for (const char* part : parts) {
        if (lowerName.contains(QLatin1String(part))) return true;
    }
    return false;
}

// What a native game binary is called: no extension at all, or one of the
// architecture/packaging suffixes. Everything else that happens to be ELF and
// executable — Node addons (*.node), versioned libraries (libvulkan.so.1) —
// is a library, not the game.
bool hasBinarySuffix(const QFileInfo& info)
{
    static const QStringList suffixes = {
        QString(), "x86_64", "x86", "x64", "bin", "appimage", "elf",
    };
    return suffixes.contains(info.suffix().toLower());
}

// A script without an extension (`run_game`, `start`) is how some native
// games are started; one with an extension was already filtered out above.
bool isExtensionlessScript(const QFileInfo& info)
{
    if (!info.suffix().isEmpty()) return false;
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) return false;
    return file.read(2) == "#!";
}

QString alnumLower(QString s)
{
    static const QRegularExpression notAlnum(QStringLiteral("[^a-z0-9]"));
    return s.toLower().remove(notAlnum);
}

bool isLauncher(const QString& path)
{
    return QFileInfo(path).fileName().contains(QLatin1String("launcher"), Qt::CaseInsensitive);
}

}  // namespace

QStringList windowsExecutables(const QString& dir, const CancelFlag& cancel)
{
    QStringList out;
    QDirIterator it(dir, {QStringLiteral("*.exe")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancelled(cancel)) break;
        const QString path = it.next();
        if (!isWindowsJunk(QFileInfo(path).fileName().toLower())) out << path;
    }
    sortByDepth(out);
    return out;
}

QStringList linuxExecutables(const QString& dir, const CancelFlag& cancel)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancelled(cancel)) break;
        const QString path = it.next();
        const QFileInfo info(path);
        if (isLinuxJunk(info.fileName().toLower())) continue;
        if (!info.isExecutable() || !hasBinarySuffix(info)) continue;
        if (isElfExecutable(path) || isExtensionlessScript(info)) out << path;
    }
    sortByDepth(out);
    return out;
}

QStringList candidates(const QString& dir, bool nativeLinux, const CancelFlag& cancel)
{
    return nativeLinux ? linuxExecutables(dir, cancel) : windowsExecutables(dir, cancel);
}

bool isElfExecutable(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray header = file.read(20);
    if (header.size() < 20) return false;
    if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F')
        return false;

    // e_type at offset 16, either byte order: ET_EXEC (2), or ET_DYN (3),
    // which is what a position-independent executable is. Shared libraries
    // are ET_DYN as well; they were filtered out by extension already.
    const auto lo = static_cast<unsigned char>(header[16]);
    const auto hi = static_cast<unsigned char>(header[17]);
    const unsigned type = lo != 0 ? lo : hi;
    return (lo == 0 || hi == 0) && (type == 2 || type == 3);
}

QString bestMatch(const QString& gameName, const QString& installPath,
                  const QStringList& candidates)
{
    if (candidates.isEmpty()) return {};

    // "Agent 64: Spies Never Die (Demo)" -> "agent64spiesneverdie".
    QString game = gameName;
    game.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    const QString cleanGame = alnumLower(game);
    const QString cleanDir = alnumLower(QFileInfo(installPath).fileName());

    QStringList preferred;
    for (const QString& path : candidates) {
        if (!isLauncher(path)) preferred << path;
    }
    if (preferred.isEmpty()) preferred = candidates;   // nothing but launchers

    for (const QString& path : preferred) {
        const QString exe = alnumLower(QFileInfo(path).baseName());
        if (!exe.isEmpty() && (exe == cleanGame || exe == cleanDir)) return path;
    }
    if (cleanGame.length() > 3) {
        const QString half = cleanGame.left(cleanGame.length() / 2);
        for (const QString& path : preferred) {
            if (alnumLower(QFileInfo(path).baseName()).contains(half)) return path;
        }
    }
    for (const QString& path : preferred) {
        // The file name, not the path: a dependency's "linux-x64" directory
        // says nothing about which binary is the game.
        const QString name = QFileInfo(path).fileName().toLower();   // game.x86_64
        if (name.contains(QLatin1String("x86_64")) || name.contains(QLatin1String("x64")))
            return path;
    }
    return preferred.first();
}

QString find(const QString& gameName, const QString& installPath, bool nativeLinux)
{
    return bestMatch(gameName, installPath, candidates(installPath, nativeLinux));
}

}  // namespace ExecutableFinder
