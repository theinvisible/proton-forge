#include "utils/HostEnvironment.h"

namespace HostEnvironment {

namespace {

// Everything packaging/appimage/AppRun prepends to. Keep in step with that file;
// a variable it changes and this list does not know about leaks into every child
// process, and one listed here that it never touches costs nothing.
const char* const kManagedVars[] = {
    "LD_LIBRARY_PATH",
    "QT_PLUGIN_PATH",
    "QT_QPA_PLATFORM_PLUGIN_PATH",
    "XDG_DATA_DIRS",
    "GSETTINGS_SCHEMA_DIR",
    "GDK_PIXBUF_MODULE_FILE",
    "PERLLIB",
    "PYTHONHOME",
};

const char* const kHostPrefix = "PROTONFORGE_HOST_";

} // namespace

QStringList managedVars()
{
    QStringList out;
    for (const char* var : kManagedVars)
        out << QLatin1String(var);
    return out;
}

QString hostPrefix()
{
    return QLatin1String(kHostPrefix);
}

namespace {

// A path list with every entry inside the AppDir taken out. Order and everything
// else is preserved, so a host value that happens to be a list survives intact.
QString withoutAppDirEntries(const QString& value, const QString& appDir)
{
    QStringList kept;
    for (const QString& entry : value.split(QLatin1Char(':'))) {
        if (entry == appDir || entry.startsWith(appDir + QLatin1Char('/')))
            continue;
        kept << entry;
    }
    return kept.join(QLatin1Char(':'));
}

} // namespace

QProcessEnvironment forChildProcess(const QProcessEnvironment& base)
{
    // APPDIR is what an AppImage's AppRun exports and nothing else does, so it
    // is the one signal that there is anything to undo. Asked of `base` rather
    // than of the real environment so the whole function stays testable.
    if (!base.contains(QStringLiteral("APPDIR")))
        return base;

    QProcessEnvironment env = base;
    const QString appDir = env.value(QStringLiteral("APPDIR"));

    for (const char* name : kManagedVars) {
        const QString var  = QLatin1String(name);
        const QString host = QLatin1String(kHostPrefix) + var;

        // The saved copy is the host's own value and the right answer when there
        // is one. Without one, AppRun did not touch this variable — but something
        // else in the chain may have, so the value is filtered rather than
        // trusted. That is not hypothetical: `--custom-apprun` makes linuxdeploy
        // generate a wrapper that sources every plugin hook and *then* execs our
        // AppRun, so a hook always runs first and a future one prepending a path
        // would otherwise reach every game we launch.
        const QString original = env.contains(host) ? env.value(host)
                                                    : env.value(var);
        const QString cleaned = withoutAppDirEntries(original, appDir);

        // Removed, not emptied. An empty LD_LIBRARY_PATH is not "unset" — the
        // loader reads it as the current directory.
        if (cleaned.isEmpty())
            env.remove(var);
        else
            env.insert(var, cleaned);
    }

    // PATH is not restored from a saved copy — AppRun only prepends
    // $APPDIR/usr/bin to it, and ProcessRunner needs the rest to find
    // kscreen-doctor, gsettings, flatpak and tar. It is still filtered: a child
    // has no use for a directory that stops existing when we exit.
    if (env.contains(QStringLiteral("PATH"))) {
        const QString path = withoutAppDirEntries(env.value(QStringLiteral("PATH")), appDir);
        if (!path.isEmpty())
            env.insert(QStringLiteral("PATH"), path);
    }

    // Our own bookkeeping is not a child's business.
    for (const QString& key : env.keys()) {
        if (key.startsWith(QLatin1String(kHostPrefix)))
            env.remove(key);
    }

    return env;
}

} // namespace HostEnvironment
