#ifndef GOGINSTALLREGISTRY_H
#define GOGINSTALLREGISTRY_H

#include <QDateTime>
#include <QList>
#include <QRecursiveMutex>
#include <QString>
#include <QStringList>

// What ProtonForge has installed from GOG — and the only source of truth for it.
//
// Deliberately not a filesystem scan. A scan would also find Heroic's and
// Lutris's installs, and then two tools would each believe they own the same
// directory's updates and its uninstall. One registry means uninstall always
// knows exactly what it may delete, which matters a great deal given that
// deleting is what uninstall does.
//
// A sibling of settings.json rather than a section inside it: this is
// machine-written bookkeeping, and settings.json is a file users hand-edit.
//
// Thread-safe, and not optionally: ILauncher::refreshGameState() is called from
// GameListWidget's QtConcurrent worker while the store dialog writes the newest
// build id from the GUI thread. Recursive because the public methods compose
// (contains() reads through entry(), put() writes through save()) and splitting
// each into a locked and an unlocked half would double the surface for no gain
// at this call volume.
class GogInstallRegistry
{
public:
    struct Entry {
        QString productId;
        QString title;
        QString installPath;

        // Opaque strings, both of them. Never ordered, only compared — see
        // hasUpdate().
        QString buildId;
        QString versionName;

        QString platform;         // "windows" | "linux"
        QStringList languages;
        QStringList dlcIds;
        qint64 size = 0;

        QDateTime installedAt;
        QDateTime updatedAt;

        // False while a download is still running. Such an entry is written
        // before the first byte arrives, which is what makes an interrupted
        // install resumable instead of orphaned.
        bool complete = false;

        // Resolved once at install time from goggame-<id>.info, so launching
        // never has to parse anything.
        QString executablePath;      // absolute
        QString workingDirectory;    // absolute
        QStringList launchArgs;
        bool nativeLinux = false;

        // What the build asked for that ProtonForge did not do — chiefly the
        // Windows redistributables Galaxy would run and we do not. Kept with
        // the install rather than shown once during it: "this game needs the
        // 2019 C++ runtime" is the answer to a question asked days later, when
        // the game will not start.
        QStringList warnings;

        // Filled in by whoever last asked the content system what the newest
        // build is. Kept here so discovery can answer "update available"
        // without touching the network from a worker thread.
        QString latestBuildId;
        QDateTime latestCheckedAt;

        bool valid = false;
    };

    static GogInstallRegistry& instance();

    // --- where things go ---

    static QString filePath();

    // Where a completed install's file fingerprints are kept, so the next
    // update can be a delta rather than a full re-download. Outside the game
    // directory on purpose: deleting the game by hand should not leave
    // ProtonForge believing a stale manifest describes what is there.
    static QString manifestPath(const QString& productId);
    static QString defaultInstallRoot();          // ~/Games/ProtonForge
    static QString installRoot();                 // the configured one, or the default
    static void setInstallRoot(const QString& path);

    // <root>/GOG/<installDirectory> and <root>/prefixes/GOG/<productId>.
    // Store-partitioned so a second store can join without a migration.
    static QString storeDirectory(const QString& root = QString());
    static QString prefixPathFor(const QString& productId, const QString& root = QString());

    // --- pure, so the format is testable without a filesystem ---

    static QList<Entry> parse(const QByteArray& json);
    static QByteArray serialize(const QList<Entry>& entries);

    // Inequality, not ordering. Build ids are opaque, and a rollback published
    // by GOG is still "not what you have installed".
    static bool hasUpdate(const Entry& entry);

    // --- state ---

    void load();
    bool save();

    QList<Entry> entries() const;
    QList<Entry> completeEntries() const;
    Entry entry(const QString& productId) const;
    bool contains(const QString& productId) const;
    bool isEmpty() const;

    // Insert or replace, then save. Timestamps are stamped here so no caller
    // has to remember to.
    bool put(Entry entry);
    bool remove(const QString& productId);

    // Record what the content system said the newest build is. A no-op for a
    // product that is not installed.
    bool setLatestBuild(const QString& productId, const QString& buildId);

private:
    GogInstallRegistry() = default;
    ~GogInstallRegistry() = default;
    GogInstallRegistry(const GogInstallRegistry&) = delete;
    GogInstallRegistry& operator=(const GogInstallRegistry&) = delete;

    QList<Entry> m_entries;
    bool m_loaded = false;
    mutable QRecursiveMutex m_mutex;
};

#endif // GOGINSTALLREGISTRY_H
