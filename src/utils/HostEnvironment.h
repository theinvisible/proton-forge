#ifndef HOSTENVIRONMENT_H
#define HOSTENVIRONMENT_H

#include <QProcessEnvironment>
#include <QStringList>

// The environment a child process should see, which is not the same as ours.
//
// An AppImage runs the application through an AppRun that prepends its own
// bundle to LD_LIBRARY_PATH, QT_PLUGIN_PATH, XDG_DATA_DIRS and friends. Those
// are right for us and wrong for everything we spawn: a game started with our
// LD_LIBRARY_PATH loads the bundle's libstdc++ and libglib instead of its own,
// `gsettings` and `kscreen-doctor` are host binaries and get the same treatment,
// and the whole bundle is unmounted the moment ProtonForge exits — while the
// game it launched is still running.
//
// So packaging/appimage/AppRun saves each value it is about to change as
// PROTONFORGE_HOST_<VAR> *before* changing it, and this is the other half:
// forChildProcess() puts those values back. The variable list here and the one
// in that script are the same list and have to stay that way.
//
// Outside an AppImage this is the identity function — nothing sets APPDIR, so
// the .deb and the Flatpak keep the environment they always had. One code path,
// not a second one for one packaging format.
namespace HostEnvironment {

// Our environment with the AppImage's additions undone.
//
// Returns `base` unchanged when APPDIR is absent. Otherwise each managed variable
// is restored from its PROTONFORGE_HOST_ copy, and whatever comes out is then
// filtered: any entry inside $APPDIR is dropped, whether it was in the saved copy
// or in a value nobody saved. That second step is not belt-and-braces. Passing
// --custom-apprun makes linuxdeploy generate a wrapper that sources every plugin
// hook and *then* execs ours, so something always runs before we can save
// anything, and a hook that prepends a path would otherwise reach every game.
//
// A variable that ends up empty is *removed* rather than set to "": an empty
// LD_LIBRARY_PATH is not "unset", it means the current directory to the loader.
// The PROTONFORGE_HOST_ variables themselves are dropped, so a child does not
// inherit our bookkeeping.
//
// PATH is not restored — AppRun only prepends $APPDIR/usr/bin to it and
// ProcessRunner needs the rest to find kscreen-doctor and gsettings — but it is
// filtered like the others, because a child has no use for a directory that stops
// existing when we exit.
QProcessEnvironment forChildProcess(
    const QProcessEnvironment& base = QProcessEnvironment::systemEnvironment());

// The variables forChildProcess() restores. Exists so a test can assert against
// the list rather than restating it.
QStringList managedVars();

// The prefix under which AppRun stashes the pre-AppImage values.
QString hostPrefix();

} // namespace HostEnvironment

#endif // HOSTENVIRONMENT_H
