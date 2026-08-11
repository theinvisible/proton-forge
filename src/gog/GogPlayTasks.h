#ifndef GOGPLAYTASKS_H
#define GOGPLAYTASKS_H

#include <QList>
#include <QString>
#include <QStringList>

// `goggame-<productId>.info` — the file a GOG install drops next to the game
// saying how to start it.
//
// This is the only thing standing between an installed depot and a launchable
// game, and guessing instead is worse than it sounds: GameRunner's executable
// heuristic skips any filename containing "launcher", which is exactly what
// several legitimate GOG entry points are called. Because
// GameRunner::findGameExecutable short-circuits when executablePath is already
// set, reading this file means that heuristic never runs for a GOG game.
//
// Pure except where marked. The one function that touches the disk says so.
namespace GogPlayTasks {

struct PlayTask {
    QString name;
    QString type;        // FileTask | URLTask
    QString category;    // game | tool | document | launcher | ...
    QString path;        // relative, Windows-style separators in the file
    QString workingDir;  // relative; frequently *not* the executable's directory
    QStringList arguments;
    QStringList languages;
    QStringList osBitness;
    bool isPrimary = false;
    bool isHidden = false;
};

struct Info {
    QString gameId;
    QString rootGameId;
    QString name;
    QString buildId;
    // The info file's own schema version ("1"), not the game's. GOG puts the
    // game version in the build listing, not here — GogContentClient::Build
    // carries it.
    QString formatVersion;
    QStringList languages;
    QStringList osBitness;
    QList<PlayTask> playTasks;
    bool valid = false;
};

Info parseInfoFile(const QByteArray& json);

// The name of the file inside an install directory.
QString infoFileName(const QString& productId);

// Startable entries only — FileTask, category "game", not hidden. This is what
// an executable picker would offer when there is more than one.
QList<PlayTask> gameTasks(const Info& info);

// The one to launch. In order: the primary game task, else the first game task,
// else the first task with a path at all. An invalid PlayTask (empty path) when
// the file names nothing startable — which is a real state for a DLC-only
// product, not a parse failure.
PlayTask primaryTask(const Info& info);

// A single `arguments` string split the way a shell would: quotes group, a
// backslash escapes the quote after it. GOG also writes this field as an array
// sometimes, which parseInfoFile handles before this is ever reached.
QStringList splitArguments(const QString& raw);

// installPath + a task path, with the same refusal rules as depot paths — these
// come out of a file that was itself downloaded. Empty when the path would
// escape the install directory.
QString resolveExecutable(const QString& installPath, const QString& taskPath);

// resolveExecutable, then a case-insensitive retry against what is actually on
// disk. Touches the filesystem, and exists because a depot and the info file
// that describes it disagree about case often enough to matter — on Windows
// that is invisible, here it is a game that will not start. Empty when nothing
// matches.
QString resolveExecutableOnDisk(const QString& installPath, const QString& taskPath);

// Whether this entry point is a Linux binary rather than a Windows one. Used to
// decide between the native and the Proton launch path.
bool looksNativeLinux(const QString& taskPath);

} // namespace GogPlayTasks

#endif // GOGPLAYTASKS_H
