// HostEnvironment undoes what an AppImage's AppRun did to our environment,
// before any of it reaches a child process. What makes it worth a test of its
// own is that it runs on every launch of every game on every packaging format,
// while the case it exists for — being inside an AppImage — is the one a
// developer never runs. So the AppImage side is simulated here by hand: APPDIR
// plus the PROTONFORGE_HOST_ copies are all the function looks at, which is
// exactly why it takes its input as a parameter.

#include <QTest>
#include <QProcessEnvironment>

#include "utils/HostEnvironment.h"
#include "utils/EnvBuilder.h"
#include "core/DLSSSettings.h"

class TstHostEnv : public QObject
{
    Q_OBJECT

private slots:
    void withoutAppDirNothingChanges();
    void restoresTheSavedHostValue();
    void removesWhatWasUnsetBefore();
    void filtersValuesNobodySaved();
    void dropsItsOwnBookkeeping();
    void stripsTheBundleFromPath();
    void keepsTheHostValueWhenItIsAList();
    void everyManagedVarIsHandled();
    void envBuilderStartsFromTheHostEnvironment();
};

namespace {

// The shape AppRun leaves behind: APPDIR set, the managed variables pointing at
// the bundle, and the previous values saved alongside.
QProcessEnvironment insideAppImage()
{
    QProcessEnvironment env;
    env.insert("APPDIR", "/tmp/.mount_PFabc");
    env.insert("PATH", "/tmp/.mount_PFabc/usr/bin:/usr/bin:/bin");
    env.insert("LD_LIBRARY_PATH", "/tmp/.mount_PFabc/usr/lib");
    env.insert("QT_PLUGIN_PATH", "/tmp/.mount_PFabc/usr/plugins");
    env.insert("XDG_DATA_DIRS", "/tmp/.mount_PFabc/usr/share:/usr/share");
    return env;
}

} // namespace

void TstHostEnv::withoutAppDirNothingChanges()
{
    // The .deb and the Flatpak take this path, i.e. almost every run there is.
    QProcessEnvironment env;
    env.insert("LD_LIBRARY_PATH", "/opt/weird/lib");
    env.insert("PATH", "/usr/bin");

    const QProcessEnvironment out = HostEnvironment::forChildProcess(env);
    QCOMPARE(out.value("LD_LIBRARY_PATH"), QStringLiteral("/opt/weird/lib"));
    QCOMPARE(out.keys(), env.keys());
}

void TstHostEnv::restoresTheSavedHostValue()
{
    QProcessEnvironment env = insideAppImage();
    env.insert("PROTONFORGE_HOST_LD_LIBRARY_PATH", "/opt/host/lib");
    env.insert("PROTONFORGE_HOST_XDG_DATA_DIRS", "/usr/share");

    const QProcessEnvironment out = HostEnvironment::forChildProcess(env);
    QCOMPARE(out.value("LD_LIBRARY_PATH"), QStringLiteral("/opt/host/lib"));
    QCOMPARE(out.value("XDG_DATA_DIRS"), QStringLiteral("/usr/share"));
}

void TstHostEnv::removesWhatWasUnsetBefore()
{
    // The common case, and the one where "restore the saved value" is wrong:
    // hardly any desktop sets LD_LIBRARY_PATH, so the saved copy is empty. An
    // empty LD_LIBRARY_PATH is not the same as an unset one — the loader reads it
    // as the current directory — so the variable has to go entirely.
    QProcessEnvironment env = insideAppImage();
    env.insert("PROTONFORGE_HOST_LD_LIBRARY_PATH", "");
    env.insert("PROTONFORGE_HOST_QT_PLUGIN_PATH", "");

    const QProcessEnvironment out = HostEnvironment::forChildProcess(env);
    QVERIFY(!out.contains("LD_LIBRARY_PATH"));
    QVERIFY(!out.contains("QT_PLUGIN_PATH"));
}

void TstHostEnv::filtersValuesNobodySaved()
{
    // No saved copy does not mean nothing touched it. --custom-apprun makes
    // linuxdeploy generate a wrapper that sources its plugin hooks and only then
    // execs ours, so a hook that prepends a bundle path runs before there is
    // anywhere to save the old value. The AppDir entries come out either way; the
    // rest of the list is left exactly as it was.
    QProcessEnvironment env = insideAppImage();
    env.insert("PROTONFORGE_HOST_LD_LIBRARY_PATH", "");
    // XDG_DATA_DIRS has no saved copy in this environment.

    const QProcessEnvironment out = HostEnvironment::forChildProcess(env);
    QCOMPARE(out.value("XDG_DATA_DIRS"), QStringLiteral("/usr/share"));
}

void TstHostEnv::dropsItsOwnBookkeeping()
{
    QProcessEnvironment env = insideAppImage();
    env.insert("PROTONFORGE_HOST_LD_LIBRARY_PATH", "/opt/host/lib");

    const QProcessEnvironment out = HostEnvironment::forChildProcess(env);
    for (const QString& key : out.keys())
        QVERIFY2(!key.startsWith(HostEnvironment::hostPrefix()),
                 qPrintable(QStringLiteral("leaked: ") + key));
}

void TstHostEnv::stripsTheBundleFromPath()
{
    // PATH is not restored from a saved copy — ProcessRunner needs the host's
    // PATH to find kscreen-doctor, gsettings, flatpak and tar, and AppRun only
    // prepends to it. The bundle's own bin directory still has to go: it holds
    // exactly one binary, ours, and it stops existing when we exit.
    QProcessEnvironment env = insideAppImage();

    const QProcessEnvironment out = HostEnvironment::forChildProcess(env);
    QCOMPARE(out.value("PATH"), QStringLiteral("/usr/bin:/bin"));
}

void TstHostEnv::keepsTheHostValueWhenItIsAList()
{
    // Filtering is per entry, not "starts with the AppDir, drop the lot".
    QProcessEnvironment env = insideAppImage();
    env.insert("PROTONFORGE_HOST_XDG_DATA_DIRS", "/usr/local/share:/usr/share:/opt/x/share");

    const QProcessEnvironment out = HostEnvironment::forChildProcess(env);
    QCOMPARE(out.value("XDG_DATA_DIRS"),
             QStringLiteral("/usr/local/share:/usr/share:/opt/x/share"));
}

void TstHostEnv::everyManagedVarIsHandled()
{
    // The list in HostEnvironment.cpp and the one in packaging/appimage/AppRun
    // are the same list. This cannot read the script, but it can assert that
    // every name the header advertises is actually acted on — a variable added
    // to the array and forgotten in the loop would pass every test above.
    const QStringList vars = HostEnvironment::managedVars();
    QVERIFY(!vars.isEmpty());

    QProcessEnvironment env;
    env.insert("APPDIR", "/tmp/.mount_PFabc");
    for (const QString& var : vars) {
        env.insert(var, "/tmp/.mount_PFabc/bundle");
        env.insert(HostEnvironment::hostPrefix() + var, "/host/" + var);
    }

    const QProcessEnvironment out = HostEnvironment::forChildProcess(env);
    for (const QString& var : vars)
        QCOMPARE(out.value(var), QStringLiteral("/host/") + var);
}

void TstHostEnv::envBuilderStartsFromTheHostEnvironment()
{
    // The wiring, not the function: buildEnvironment() is what GameRunner uses
    // for both its launch paths, and it has to be the sanitising one. Asserted
    // through the real process environment, because that is what it reads.
    qputenv("APPDIR", "/tmp/.mount_PFabc");
    qputenv("LD_LIBRARY_PATH", "/tmp/.mount_PFabc/usr/lib");
    qputenv("PROTONFORGE_HOST_LD_LIBRARY_PATH", "/opt/host/lib");

    const QProcessEnvironment env = EnvBuilder::buildEnvironment(DLSSSettings());

    qunsetenv("APPDIR");
    qunsetenv("LD_LIBRARY_PATH");
    qunsetenv("PROTONFORGE_HOST_LD_LIBRARY_PATH");

    QCOMPARE(env.value("LD_LIBRARY_PATH"), QStringLiteral("/opt/host/lib"));
    QVERIFY(!env.contains("PROTONFORGE_HOST_LD_LIBRARY_PATH"));
}

QTEST_MAIN(TstHostEnv)
#include "tst_hostenv.moc"
