#ifndef EXECUTABLEFINDER_H
#define EXECUTABLEFINDER_H

#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

// Which file in a game's install directory is the game. One answer for the
// executable selector and for a launch without a chosen executable — they
// used to be two searches with different filters, so the selector could show
// one file while a launch started another.
//
// Pure: the directory is a parameter, so it runs against fixture trees.
namespace ExecutableFinder {

using CancelFlag = std::shared_ptr<std::atomic<bool>>;

// Every candidate below `dir`, shallowest first, then by path. Installers,
// redistributables, crash reporters and anti-cheat helpers are left out.
// A set cancel flag ends the walk early with what was found so far.
QStringList windowsExecutables(const QString& dir, const CancelFlag& cancel = {});
// ELF binaries without an extension or with an architecture suffix
// (game.x86_64), and extensionless scripts with a #! line. Documentation,
// data, libraries (also *.node, *.so.1), *.sh/*.py and Electron's sandbox
// helper are left out.
QStringList linuxExecutables(const QString& dir, const CancelFlag& cancel = {});
QStringList candidates(const QString& dir, bool nativeLinux, const CancelFlag& cancel = {});

// An ELF executable or position-independent executable (ET_EXEC / ET_DYN).
bool isElfExecutable(const QString& path);

// The candidate most likely to be the game: a name matching the game's or
// the install directory's (letters and digits only, case-insensitive), then
// one containing the first half of the game's name, then a 64-bit build,
// then the first candidate. A launcher is only chosen if nothing else is
// there. Empty if `candidates` is.
QString bestMatch(const QString& gameName, const QString& installPath,
                  const QStringList& candidates);

// candidates() + bestMatch(): what a launch uses when none was chosen.
QString find(const QString& gameName, const QString& installPath, bool nativeLinux);

}  // namespace ExecutableFinder

#endif // EXECUTABLEFINDER_H
