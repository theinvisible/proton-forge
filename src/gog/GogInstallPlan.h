#ifndef GOGINSTALLPLAN_H
#define GOGINSTALLPLAN_H

#include <QList>
#include <QString>
#include <QStringList>

#include "gog/GogContentClient.h"

// Turning a build's depots into a list of files to write.
//
// Pure, and separate from the downloader on purpose: the selection rules are
// where the silent mistakes live. Choosing the wrong language depots yields a
// game with no audio; missing the overlay rule yields a file from the wrong
// depot; failing to reject a path yields a write outside the install directory.
// None of those surface as a crash, and all of them are testable without a byte
// of network.
namespace GogInstallPlan {

struct FileTask {
    QString relPath;        // already sanitized
    qint64 size = 0;
    QString md5;            // whole-file, when the manifest gave one
    QList<GogContentClient::Chunk> chunks;
    bool executable = false;
    QString linkTarget;     // non-empty => a symlink, and no chunks
    QString sourceProductId;
};

struct Plan {
    QString installDirectory;
    QList<FileTask> files;       // sorted by path, so two runs agree
    QStringList directories;
    qint64 totalSize = 0;
    qint64 totalCompressedSize = 0;
    QStringList warnings;
    bool valid = false;
};

// Every language this build has depots for, "*" excluded — that one is shared
// and always taken.
QStringList availableLanguages(const GogContentClient::BuildMeta& meta);

// Which depots to actually fetch. `languages` are the wanted ones; "*" depots
// come along regardless. A depot belonging to a DLC is only taken when that DLC
// is in `ownedDlcIds`.
QList<GogContentClient::DepotRef> selectDepots(const GogContentClient::BuildMeta& meta,
                                               const QStringList& languages,
                                               const QStringList& ownedDlcIds,
                                               int bitness = 64);

// `manifests` must be in the same order selectDepots() returned, because that
// order is what decides which depot wins when two provide the same path.
Plan build(const GogContentClient::BuildMeta& meta,
           const QList<GogContentClient::DepotManifest>& manifestsInDepotOrder);

// What an update actually has to fetch: files that are new or whose content
// changed. Everything else is already on disk.
QList<FileTask> diff(const Plan& target, const Plan& installed);

// Two paths differing only in case cannot both exist on NTFS or exFAT, which is
// what a second game drive usually is. Worth saying before the download, not
// after. `which` receives the offending path.
bool wouldCollideCaseInsensitively(const Plan& plan, QString* which);

} // namespace GogInstallPlan

#endif // GOGINSTALLPLAN_H
