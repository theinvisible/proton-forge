#include "SteamPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QLoggingCategory>

namespace {

struct Cache {
    bool resolved = false;
    SteamPaths::Variant variant = SteamPaths::Variant::None;
    QString root;
};

QMutex g_cacheMutex;
Cache g_cache;

bool hasLibraryFolders(const QString& root)
{
    if (root.isEmpty()) {
        return false;
    }
    QFileInfo fi(root + "/steamapps/libraryfolders.vdf");
    return fi.exists() && fi.isFile();
}

QString canonical(const QString& path)
{
    QFileInfo fi(path);
    QString c = fi.canonicalFilePath();
    return c.isEmpty() ? path : c;
}

bool isFlatpakRoot(const QString& canonicalRoot)
{
    return canonicalRoot.contains("/.var/app/com.valvesoftware.Steam/");
}

void resolveLocked()
{
    if (g_cache.resolved) {
        return;
    }
    g_cache.resolved = true;

    const QString home = QDir::homePath();

    // Probe candidates in order. The first three are native; the fifth is
    // Flatpak. The fourth (~/.steam/root) is often a symlink that may resolve
    // to either variant — canonicalisation handles it.
    const QStringList candidates = {
        home + "/.steam/steam",
        home + "/.local/share/Steam",
        home + "/.steam/debian-installation",
        home + "/.steam/root",
        home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
    };

    QString nativeRoot;
    QString flatpakRoot;

    for (const QString& candidate : candidates) {
        if (!QDir(candidate).exists()) {
            continue;
        }
        const QString resolved = canonical(candidate);
        if (!hasLibraryFolders(resolved)) {
            continue;
        }
        if (isFlatpakRoot(resolved)) {
            if (flatpakRoot.isEmpty()) {
                flatpakRoot = resolved;
            }
        } else {
            if (nativeRoot.isEmpty()) {
                nativeRoot = resolved;
            }
        }
    }

    // Tie-breaking: prefer native to keep existing users on the same path.
    if (!nativeRoot.isEmpty()) {
        g_cache.variant = SteamPaths::Variant::Native;
        g_cache.root = nativeRoot;
        if (!flatpakRoot.isEmpty()) {
            qInfo("SteamPaths: both native and Flatpak Steam detected; using native (%s)",
                  qUtf8Printable(nativeRoot));
        }
    } else if (!flatpakRoot.isEmpty()) {
        g_cache.variant = SteamPaths::Variant::Flatpak;
        g_cache.root = flatpakRoot;
        qInfo("SteamPaths: using Flatpak Steam at %s", qUtf8Printable(flatpakRoot));
    } else {
        g_cache.variant = SteamPaths::Variant::None;
        g_cache.root.clear();
    }
}

QString rootLocked()
{
    resolveLocked();
    return g_cache.root;
}

} // namespace

namespace SteamPaths {

QString steamRoot()
{
    QMutexLocker lock(&g_cacheMutex);
    return rootLocked();
}

Variant detectedVariant()
{
    QMutexLocker lock(&g_cacheMutex);
    resolveLocked();
    return g_cache.variant;
}

void invalidateCache()
{
    QMutexLocker lock(&g_cacheMutex);
    g_cache.resolved = false;
    g_cache.variant = Variant::None;
    g_cache.root.clear();
}

QString steamAppsPath()
{
    const QString root = steamRoot();
    return root.isEmpty() ? QString() : root + "/steamapps";
}

QString compatibilityToolsPath()
{
    const QString root = steamRoot();
    return root.isEmpty() ? QString() : root + "/compatibilitytools.d";
}

QString configVdfPath()
{
    const QString root = steamRoot();
    return root.isEmpty() ? QString() : root + "/config/config.vdf";
}

QString steamRuntimePath()
{
    const QString root = steamRoot();
    return root.isEmpty() ? QString() : root + "/ubuntu12_32/steam-runtime";
}

QString overlayLibPath(bool is64)
{
    const QString root = steamRoot();
    if (root.isEmpty()) {
        return QString();
    }
    return root + (is64 ? "/ubuntu12_64/gameoverlayrenderer.so"
                        : "/ubuntu12_32/gameoverlayrenderer.so");
}

QString userDataPath()
{
    const QString root = steamRoot();
    return root.isEmpty() ? QString() : root + "/userdata";
}

QString defaultInstallCompatPath()
{
    const QString detected = compatibilityToolsPath();
    if (!detected.isEmpty()) {
        return detected;
    }

    // No Steam install detected — pick a sensible target so first-run
    // "Install Proton" still has somewhere to extract to.
    const QString flatpakId = QProcessEnvironment::systemEnvironment().value("FLATPAK_ID");
    const QString home = QDir::homePath();
    if (flatpakId == "org.protonforge.ProtonForge") {
        return home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/compatibilitytools.d";
    }
    return home + "/.steam/root/compatibilitytools.d";
}

} // namespace SteamPaths
