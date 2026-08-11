#ifndef SECRETSTORE_H
#define SECRETSTORE_H

#include <QHash>
#include <QObject>
#include <QString>

// The one place credentials live: the GOG refresh token, the Steam Web API key,
// and the GitHub token that used to sit in QSettings in the clear.
//
// Loaded once at startup, asynchronously, then read synchronously from memory.
// That shape is not a preference — ProtonManager::applyGitHubHeaders() reads its
// token inline while building a QNetworkRequest, from five call sites, and an
// async read cannot be dropped into that without restructuring ProtonManager.
// Keeping the reads synchronous is also what lets a launcher's isAvailable()
// stay cheap enough to be called on every refresh.
//
// Writes stay asynchronous, because the keychain backend is. The cache is
// updated immediately so a read straight after a write sees the new value; if
// the write then fails, writeFailed() says so.
//
// Backend: QtKeychain when it was found at build time and a secret service
// answers at run time, otherwise a 0600 file next to settings.json. Setting
// PROTONFORGE_SECRET_STORE=file forces the file backend — useful on a desktop
// whose keyring misbehaves, and what the tests use so they never touch the
// developer's real keychain.
class SecretStore : public QObject
{
    Q_OBJECT

public:
    enum class Key {
        GogRefreshToken,
        SteamWebApiKey,
        GitHubToken,
    };
    Q_ENUM(Key)

    static SecretStore& instance();

    // Kicks off the one-time load. Safe to call more than once; only the first
    // call does anything. ready() follows, possibly before this returns.
    void load();
    bool isReady() const { return m_ready; }

    // Empty until ready(), and empty for anything never stored.
    QString value(Key key) const;

    void setValue(Key key, const QString& value);
    void clear(Key key);

    // Where the file backend keeps its data. Public so the tests and the docs
    // can name the same path the code uses.
    static QString filePath();

    // Which backend actually ended up holding the credentials — "keyring" or
    // "file". Decided at load time, not guessable from outside: a keyring that
    // did not answer looks identical to one that simply has nothing in it.
    QString backendName() const;

    // Test seam: forget everything, including that a load ever happened.
    void resetForTesting();

signals:
    void ready();
    void writeFailed(SecretStore::Key key, const QString& reason);

private:
    SecretStore();
    ~SecretStore() = default;
    SecretStore(const SecretStore&) = delete;
    SecretStore& operator=(const SecretStore&) = delete;

    enum class Backend { Keychain, File };

    void loadFromFile();
    bool saveToFile();
    void loadFromKeychain();
    void writeToKeychain(Key key, const QString& value);
    void finishLoad();
    void migrateGitHubTokenFromSettings();

    Backend m_backend;
    bool m_loadStarted = false;
    bool m_ready = false;
    int m_pendingReads = 0;
    QHash<Key, QString> m_values;
};

#endif // SECRETSTORE_H
