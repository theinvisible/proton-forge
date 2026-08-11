#include "SecretStore.h"
#include "SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>

#ifdef PROTONFORGE_HAVE_QTKEYCHAIN
#include <qt6keychain/keychain.h>
#endif

namespace {

// The service name the keychain entries are filed under, and the field names in
// the file backend. Both are persisted, so neither may be renamed casually.
const QString kService = QStringLiteral("org.protonforge.ProtonForge");

QString keyName(SecretStore::Key key)
{
    switch (key) {
    case SecretStore::Key::GogRefreshToken: return QStringLiteral("gog-refresh-token");
    case SecretStore::Key::SteamWebApiKey:  return QStringLiteral("steam-web-api-key");
    case SecretStore::Key::GitHubToken:     return QStringLiteral("github-token");
    }
    return QString();
}

QList<SecretStore::Key> allKeys()
{
    return {SecretStore::Key::GogRefreshToken,
            SecretStore::Key::SteamWebApiKey,
            SecretStore::Key::GitHubToken};
}

// Qt mirrors a file's owner bits into the *User* bits as well, so a correct
// 0600 file reads back as ReadOwner|WriteOwner|ReadUser|WriteUser. Comparing
// against ReadOwner|WriteOwner alone therefore never matches, and would have
// re-chmod'ed (and warned about) a perfectly good file on every single load.
// What actually matters is that nobody else can read it.
QFile::Permissions ownerOnly()
{
    return QFile::ReadOwner | QFile::WriteOwner;
}

bool readableByOthers(QFile::Permissions permissions)
{
    const QFile::Permissions others =
        QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
        QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    return (permissions & others) != QFile::Permissions();
}

bool fileBackendForced()
{
    return QProcessEnvironment::systemEnvironment()
               .value(QStringLiteral("PROTONFORGE_SECRET_STORE"))
               .compare(QStringLiteral("file"), Qt::CaseInsensitive) == 0;
}

} // namespace

SecretStore& SecretStore::instance()
{
    static SecretStore instance;
    return instance;
}

SecretStore::SecretStore()
{
#ifdef PROTONFORGE_HAVE_QTKEYCHAIN
    m_backend = fileBackendForced() ? Backend::File : Backend::Keychain;
#else
    m_backend = Backend::File;
#endif
}

QString SecretStore::filePath()
{
    // A sibling of settings.json rather than a part of it. settings.json is
    // user-editable and routinely pasted into bug reports; a credential must
    // not ride along with it.
    return SettingsManager::configDir() + QStringLiteral("/secrets.json");
}

QString SecretStore::backendName() const
{
    return m_backend == Backend::Keychain ? QStringLiteral("keyring") : QStringLiteral("file");
}

void SecretStore::load()
{
    if (m_loadStarted) {
        return;
    }
    m_loadStarted = true;

    if (m_backend == Backend::File) {
        loadFromFile();
        // Always report readiness through the event loop, so a caller that
        // connects to ready() straight after load() cannot miss it just because
        // this backend happens to be synchronous.
        QTimer::singleShot(0, this, [this]() { finishLoad(); });
        return;
    }

    loadFromKeychain();
}

void SecretStore::finishLoad()
{
    if (m_ready) {
        return;
    }
    // Only once the store is actually up: a keychain that failed to answer must
    // not be able to destroy a token that is still sitting in QSettings.
    migrateGitHubTokenFromSettings();
    m_ready = true;
    emit ready();
}

QString SecretStore::value(Key key) const
{
    return m_values.value(key);
}

void SecretStore::setValue(Key key, const QString& value)
{
    if (value.isEmpty()) {
        clear(key);
        return;
    }

    m_values.insert(key, value);

    if (m_backend == Backend::File) {
        if (!saveToFile()) {
            emit writeFailed(key, QStringLiteral("Could not write %1").arg(filePath()));
        }
        return;
    }
    writeToKeychain(key, value);
}

void SecretStore::clear(Key key)
{
    m_values.remove(key);

    if (m_backend == Backend::File) {
        if (!saveToFile()) {
            emit writeFailed(key, QStringLiteral("Could not write %1").arg(filePath()));
        }
        return;
    }
    writeToKeychain(key, QString());
}

// --- file backend ----------------------------------------------------------

void SecretStore::loadFromFile()
{
    QFile file(filePath());
    if (!file.exists()) {
        return;
    }

    // A credential file that anyone can read is worth complaining about, and
    // worth fixing rather than merely reporting.
    if (readableByOthers(file.permissions())) {
        qWarning("SecretStore: %s was not owner-only; tightening permissions",
                 qUtf8Printable(filePath()));
        file.setPermissions(ownerOnly());
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("SecretStore: could not read %s", qUtf8Printable(filePath()));
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject secrets = root.value(QStringLiteral("secrets")).toObject();
    for (const Key key : allKeys()) {
        const QString stored = secrets.value(keyName(key)).toString();
        if (!stored.isEmpty()) {
            m_values.insert(key, stored);
        }
    }
}

bool SecretStore::saveToFile()
{
    QDir().mkpath(SettingsManager::configDir());

    QJsonObject secrets;
    for (const Key key : allKeys()) {
        const QString stored = m_values.value(key);
        if (!stored.isEmpty()) {
            secrets.insert(keyName(key), stored);
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("secrets"), secrets);

    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return false;
    }

    // After commit, not before: QSaveFile writes through a temporary and the
    // permissions have to land on the file that survives.
    QFile::setPermissions(filePath(), ownerOnly());
    return true;
}

// --- keychain backend ------------------------------------------------------

#ifdef PROTONFORGE_HAVE_QTKEYCHAIN

void SecretStore::loadFromKeychain()
{
    const QList<Key> keys = allKeys();
    m_pendingReads = static_cast<int>(keys.size());

    for (const Key key : keys) {
        auto* job = new QKeychain::ReadPasswordJob(kService, this);
        job->setAutoDelete(true);
        job->setKey(keyName(key));

        connect(job, &QKeychain::Job::finished, this, [this, key, job]() {
            switch (job->error()) {
            case QKeychain::NoError:
                m_values.insert(key, job->textData());
                break;
            case QKeychain::EntryNotFound:
                break;   // simply never stored
            default:
                // No secret service, or it refused. That is the normal state on
                // a bare gaming session, so fall back for the whole session
                // rather than asking again per key.
                qWarning("SecretStore: keychain unavailable (%s); using %s",
                         qUtf8Printable(job->errorString()), qUtf8Printable(filePath()));
                m_backend = Backend::File;
                break;
            }

            if (--m_pendingReads > 0) {
                return;
            }
            if (m_backend == Backend::File) {
                m_values.clear();     // discard whatever partial answer we got
                loadFromFile();
            }
            finishLoad();
        });

        job->start();
    }
}

void SecretStore::writeToKeychain(Key key, const QString& value)
{
    if (value.isEmpty()) {
        auto* job = new QKeychain::DeletePasswordJob(kService, this);
        job->setAutoDelete(true);
        job->setKey(keyName(key));
        connect(job, &QKeychain::Job::finished, this, [this, key, job]() {
            // Deleting something that was never there is not a failure.
            if (job->error() != QKeychain::NoError && job->error() != QKeychain::EntryNotFound) {
                emit writeFailed(key, job->errorString());
            }
        });
        job->start();
        return;
    }

    auto* job = new QKeychain::WritePasswordJob(kService, this);
    job->setAutoDelete(true);
    job->setKey(keyName(key));
    job->setTextData(value);
    connect(job, &QKeychain::Job::finished, this, [this, key, job]() {
        if (job->error() != QKeychain::NoError) {
            emit writeFailed(key, job->errorString());
        }
    });
    job->start();
}

#else   // built without QtKeychain

void SecretStore::loadFromKeychain()
{
    loadFromFile();
    QTimer::singleShot(0, this, [this]() { finishLoad(); });
}

void SecretStore::writeToKeychain(Key key, const QString&)
{
    Q_UNUSED(key);
    saveToFile();
}

#endif

// --- migration -------------------------------------------------------------

void SecretStore::migrateGitHubTokenFromSettings()
{
    QSettings settings;
    const QString legacy = settings.value(QStringLiteral("github/apiToken")).toString().trimmed();
    if (legacy.isEmpty()) {
        return;
    }

    // One way, and only when the store came up: otherwise a keychain that is
    // merely asleep would take the user's token with it.
    if (value(Key::GitHubToken).isEmpty()) {
        setValue(Key::GitHubToken, legacy);
    }
    settings.remove(QStringLiteral("github/apiToken"));
}

void SecretStore::resetForTesting()
{
    m_values.clear();
    m_loadStarted = false;
    m_ready = false;
    m_pendingReads = 0;
#ifdef PROTONFORGE_HAVE_QTKEYCHAIN
    m_backend = fileBackendForced() ? Backend::File : Backend::Keychain;
#else
    m_backend = Backend::File;
#endif
}
