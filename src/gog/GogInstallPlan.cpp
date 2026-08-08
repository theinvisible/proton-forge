#include "GogInstallPlan.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <QHash>
#include <QSet>
#include <algorithm>

namespace GogInstallPlan {

namespace {

const QString kSharedLanguage = QStringLiteral("*");

bool wantsLanguage(const GogContentClient::DepotRef& depot, const QStringList& languages)
{
    // No language tag at all means the depot is not language-specific.
    if (depot.languages.isEmpty()) {
        return true;
    }
    for (const QString& depotLanguage : depot.languages) {
        // "*" is shared content — the executable, the engine, the assets — and
        // is needed whichever language the user picked.
        if (depotLanguage == kSharedLanguage) {
            return true;
        }
        for (const QString& wanted : languages) {
            if (depotLanguage.compare(wanted, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
    }
    return false;
}

bool wantsBitness(const GogContentClient::DepotRef& depot, int bitness)
{
    if (depot.osBitness.isEmpty()) {
        return true;
    }
    return depot.osBitness.contains(QString::number(bitness));
}

} // namespace

QStringList availableLanguages(const GogContentClient::BuildMeta& meta)
{
    QStringList languages;
    for (const GogContentClient::DepotRef& depot : meta.depots) {
        for (const QString& language : depot.languages) {
            if (language != kSharedLanguage && !languages.contains(language)) {
                languages << language;
            }
        }
    }
    languages.sort();
    return languages;
}

QList<GogContentClient::DepotRef> selectDepots(const GogContentClient::BuildMeta& meta,
                                               const QStringList& languages,
                                               const QStringList& ownedDlcIds,
                                               int bitness)
{
    QList<GogContentClient::DepotRef> selected;

    for (const GogContentClient::DepotRef& depot : meta.depots) {
        // A depot belonging to a DLC the user does not own is not ours to
        // fetch — and GOG lists them in the base game's build regardless.
        const bool belongsToBase =
            depot.productId.isEmpty() || depot.productId == meta.baseProductId;
        if (!belongsToBase && !ownedDlcIds.contains(depot.productId)) {
            continue;
        }
        if (!wantsLanguage(depot, languages) || !wantsBitness(depot, bitness)) {
            continue;
        }
        selected.append(depot);
    }

    return selected;
}

Plan build(const GogContentClient::BuildMeta& meta,
           const QList<GogContentClient::DepotManifest>& manifestsInDepotOrder)
{
    Plan plan;
    if (!meta.valid) {
        return plan;
    }

    plan.installDirectory = meta.installDirectory;

    // Insertion-ordered by first sighting, value overwritten by later depots:
    // when two depots provide the same path the later one wins, which is how
    // GOG layers a language pack over the shared content.
    QHash<QString, FileTask> byPath;
    QSet<QString> directories;
    int rejectedPaths = 0;

    for (const GogContentClient::DepotManifest& manifest : manifestsInDepotOrder) {
        for (const GogContentClient::DepotItem& item : manifest.items) {
            const QString relPath = GogContentClient::sanitizeDepotPath(item.path);
            if (relPath.isEmpty()) {
                // Refused rather than repaired — see sanitizeDepotPath.
                ++rejectedPaths;
                continue;
            }

            if (item.type == QLatin1String("DepotDirectory")) {
                directories.insert(relPath);
                continue;
            }

            FileTask task;
            task.relPath = relPath;
            task.md5 = item.md5;
            task.chunks = item.chunks;
            task.executable = item.flags.contains(QLatin1String("executable"));
            task.sourceProductId =
                manifest.productId.isEmpty() ? meta.baseProductId : manifest.productId;

            if (item.type == QLatin1String("DepotLink")) {
                task.linkTarget = GogContentClient::sanitizeDepotPath(item.linkTarget);
                if (task.linkTarget.isEmpty()) {
                    ++rejectedPaths;   // a link pointing out of the tree
                    continue;
                }
            } else {
                for (const GogContentClient::Chunk& chunk : item.chunks) {
                    task.size += chunk.size;
                }
            }

            byPath.insert(relPath, task);
        }
    }

    if (rejectedPaths > 0) {
        plan.warnings << QStringLiteral(
            "%1 file(s) in this build named a location outside the install directory "
            "and were skipped.").arg(rejectedPaths);
    }

    // Galaxy would run the redistributables in dependencies[] at install time.
    // ProtonForge does not, because Proton ships its own equivalents for nearly
    // all of them — but say so, because "nearly" is not "all".
    if (!meta.dependencies.isEmpty()) {
        plan.warnings << QStringLiteral(
            "This build lists redistributables (%1). ProtonForge does not run them; "
            "Proton provides its own.").arg(meta.dependencies.join(", "));
    }

    plan.files = byPath.values();
    std::sort(plan.files.begin(), plan.files.end(), [](const FileTask& a, const FileTask& b) {
        return a.relPath < b.relPath;
    });

    plan.directories = QStringList(directories.constBegin(), directories.constEnd());
    plan.directories.sort();

    for (const FileTask& file : std::as_const(plan.files)) {
        plan.totalSize += file.size;
        for (const GogContentClient::Chunk& chunk : file.chunks) {
            plan.totalCompressedSize += chunk.compressedSize;
        }
    }

    plan.valid = true;
    return plan;
}

QString fingerprint(const FileTask& file)
{
    if (!file.md5.isEmpty()) {
        return file.md5;
    }
    // Content-addressed either way: two files with the same chunk list are the
    // same file.
    QString joined;
    for (const GogContentClient::Chunk& chunk : file.chunks) {
        joined += chunk.compressedMd5;
    }
    return joined;
}

QList<FileTask> diffAgainstFingerprints(const Plan& target,
                                        const QHash<QString, QString>& installed)
{
    QList<FileTask> changed;
    for (const FileTask& file : target.files) {
        const auto it = installed.constFind(file.relPath);
        if (it == installed.constEnd() || it.value() != fingerprint(file)) {
            changed.append(file);
        }
    }
    return changed;
}

QList<FileTask> diff(const Plan& target, const Plan& installed)
{
    QHash<QString, QString> fingerprints;
    for (const FileTask& file : installed.files) {
        fingerprints.insert(file.relPath, fingerprint(file));
    }
    return diffAgainstFingerprints(target, fingerprints);
}

QStringList removedPaths(const Plan& target, const QHash<QString, QString>& installed)
{
    QSet<QString> wanted;
    for (const FileTask& file : target.files) {
        wanted.insert(file.relPath);
    }

    QStringList gone;
    for (auto it = installed.constBegin(); it != installed.constEnd(); ++it) {
        if (!wanted.contains(it.key())) {
            gone << it.key();
        }
    }
    gone.sort();   // so two runs agree, and so a log of it is readable
    return gone;
}

QByteArray serializeFingerprints(const Plan& plan)
{
    QJsonObject files;
    for (const FileTask& file : plan.files) {
        files.insert(file.relPath, fingerprint(file));
    }

    QJsonObject root;
    root["version"] = 1;
    root["files"] = files;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QHash<QString, QString> parseFingerprints(const QByteArray& json)
{
    QHash<QString, QString> out;

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return out;
    }
    const QJsonObject files = doc.object().value("files").toObject();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        out.insert(it.key(), it.value().toString());
    }
    return out;
}

bool wouldCollideCaseInsensitively(const Plan& plan, QString* which)
{
    QHash<QString, QString> seen;
    for (const FileTask& file : plan.files) {
        const QString folded = file.relPath.toLower();
        const auto it = seen.constFind(folded);
        if (it != seen.constEnd() && it.value() != file.relPath) {
            if (which) {
                *which = file.relPath;
            }
            return true;
        }
        seen.insert(folded, file.relPath);
    }
    return false;
}

} // namespace GogInstallPlan
