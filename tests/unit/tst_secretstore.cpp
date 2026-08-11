// SecretStore is where three credentials live, so its failure modes are all
// quiet ones: a token that silently does not persist, a file anyone can read, a
// migration that runs twice and loses something.
//
// These cases drive the file backend only — forced with PROTONFORGE_SECRET_STORE
// — because a unit test must never touch the developer's real keyring, and
// because that fallback is the path most users on a bare gaming session will
// actually take. It is the one that has to be exercised, not merely written.

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include "core/SecretStore.h"
#include "core/SettingsManager.h"

class TstSecretStore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void valueIsEmptyBeforeTheStoreIsReady();
    void storesAndReloadsAcrossInstances();
    void writesAreOwnerOnly();
    void repairsPermissionsOnLoad();
    void clearingRemovesOnlyThatKey();
    void survivesAGarbageFile();
    void migratesTheGitHubTokenOutOfQSettings();
    void migrationDoesNotClobberAnExistingToken();

private:
    QTemporaryDir m_home;
    QByteArray m_realHome;
    QByteArray m_realBackend;

    SecretStore& store() { return SecretStore::instance(); }

    // Drive the one-time async load to completion.
    void loadAndWait()
    {
        QSignalSpy spy(&store(), &SecretStore::ready);
        store().load();
        if (!store().isReady()) {
            QVERIFY(spy.wait(2000));
        }
        QVERIFY(store().isReady());
    }

    // A second "process": forget everything and load again from disk.
    void reload()
    {
        store().resetForTesting();
        loadAndWait();
    }

    QString secretsPath() const { return SecretStore::filePath(); }

    // Qt mirrors the owner bits into the User bits on Unix, so a correct 0600
    // file reads back as ReadOwner|WriteOwner|ReadUser|WriteUser. Asserting on
    // an exact value would be asserting on that quirk; what matters is that
    // group and other have nothing.
    static void assertOwnerOnly(const QString& path)
    {
        const QFile::Permissions perms = QFile(path).permissions();
        QVERIFY(perms & QFile::ReadOwner);
        QVERIFY(perms & QFile::WriteOwner);
        QVERIFY2(!(perms & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
                            | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)),
                 "a credential file must not be readable by anyone else");
    }
};

void TstSecretStore::initTestCase()
{
    // QSettings and SettingsManager::configDir() both key off these.
    QCoreApplication::setOrganizationName("ProtonForgeTest");
    QCoreApplication::setApplicationName("ProtonForgeTest");
}

void TstSecretStore::init()
{
    QVERIFY(m_home.isValid());
    m_realHome = qgetenv("HOME");
    m_realBackend = qgetenv("PROTONFORGE_SECRET_STORE");
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/.config").toUtf8());
    qputenv("PROTONFORGE_SECRET_STORE", "file");

    store().resetForTesting();
    QSettings().clear();
}

void TstSecretStore::cleanup()
{
    QSettings().clear();
    store().resetForTesting();

    qputenv("HOME", m_realHome);
    qputenv("PROTONFORGE_SECRET_STORE", m_realBackend);
    qunsetenv("XDG_CONFIG_HOME");

    QDir(m_home.path()).removeRecursively();
    QDir().mkpath(m_home.path());
}

void TstSecretStore::valueIsEmptyBeforeTheStoreIsReady()
{
    // Callers read synchronously from the very first frame. Returning empty
    // rather than blocking is what keeps a launcher's isAvailable() cheap.
    QVERIFY(!store().isReady());
    QVERIFY(store().value(SecretStore::Key::GogRefreshToken).isEmpty());
}

void TstSecretStore::storesAndReloadsAcrossInstances()
{
    loadAndWait();
    store().setValue(SecretStore::Key::GogRefreshToken, "refresh-me");
    store().setValue(SecretStore::Key::SteamWebApiKey, "ABCDEF");

    // Visible immediately, before any write could have completed.
    QCOMPARE(store().value(SecretStore::Key::GogRefreshToken), QStringLiteral("refresh-me"));

    reload();
    QCOMPARE(store().value(SecretStore::Key::GogRefreshToken), QStringLiteral("refresh-me"));
    QCOMPARE(store().value(SecretStore::Key::SteamWebApiKey), QStringLiteral("ABCDEF"));
    QVERIFY(store().value(SecretStore::Key::GitHubToken).isEmpty());
}

void TstSecretStore::writesAreOwnerOnly()
{
    loadAndWait();
    store().setValue(SecretStore::Key::GogRefreshToken, "refresh-me");

    QVERIFY2(QFile::exists(secretsPath()), qPrintable(secretsPath()));
    assertOwnerOnly(secretsPath());

    // And it is deliberately not part of settings.json, which gets pasted into
    // bug reports.
    QVERIFY(secretsPath() != SettingsManager::configFilePath());
}

void TstSecretStore::repairsPermissionsOnLoad()
{
    loadAndWait();
    store().setValue(SecretStore::Key::GogRefreshToken, "refresh-me");

    QFile::setPermissions(secretsPath(),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);

    reload();

    assertOwnerOnly(secretsPath());
    QCOMPARE(store().value(SecretStore::Key::GogRefreshToken), QStringLiteral("refresh-me"));
}

void TstSecretStore::clearingRemovesOnlyThatKey()
{
    loadAndWait();
    store().setValue(SecretStore::Key::GogRefreshToken, "refresh-me");
    store().setValue(SecretStore::Key::SteamWebApiKey, "ABCDEF");

    store().clear(SecretStore::Key::GogRefreshToken);

    reload();
    QVERIFY(store().value(SecretStore::Key::GogRefreshToken).isEmpty());
    QCOMPARE(store().value(SecretStore::Key::SteamWebApiKey), QStringLiteral("ABCDEF"));
}

void TstSecretStore::survivesAGarbageFile()
{
    QDir().mkpath(SettingsManager::configDir());
    QFile file(secretsPath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not json at all");
    file.close();

    // An unreadable credential file must degrade to "nothing configured", never
    // to a crash or a hang on startup.
    loadAndWait();
    QVERIFY(store().value(SecretStore::Key::GogRefreshToken).isEmpty());

    // And it recovers: writing works and replaces the rubbish.
    store().setValue(SecretStore::Key::GogRefreshToken, "recovered");
    reload();
    QCOMPARE(store().value(SecretStore::Key::GogRefreshToken), QStringLiteral("recovered"));
}

void TstSecretStore::migratesTheGitHubTokenOutOfQSettings()
{
    QSettings().setValue("github/apiToken", "ghp_legacy");

    loadAndWait();

    QCOMPARE(store().value(SecretStore::Key::GitHubToken), QStringLiteral("ghp_legacy"));
    QVERIFY2(!QSettings().contains("github/apiToken"),
             "the plaintext copy has to go, or there are two sources of truth");

    // Idempotent: a second run has nothing left to move and must not undo it.
    reload();
    QCOMPARE(store().value(SecretStore::Key::GitHubToken), QStringLiteral("ghp_legacy"));
}

void TstSecretStore::migrationDoesNotClobberAnExistingToken()
{
    loadAndWait();
    store().setValue(SecretStore::Key::GitHubToken, "ghp_current");

    // A stale QSettings key left behind by a half-finished migration must not
    // overwrite the token the user has since set.
    QSettings().setValue("github/apiToken", "ghp_ancient");

    reload();
    QCOMPARE(store().value(SecretStore::Key::GitHubToken), QStringLiteral("ghp_current"));
    QVERIFY(!QSettings().contains("github/apiToken"));
}

QTEST_MAIN(TstSecretStore)
#include "tst_secretstore.moc"
