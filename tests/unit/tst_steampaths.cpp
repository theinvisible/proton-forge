// SteamPaths decides, once per process, whether there is a Steam installation
// and which of the two kinds it is. Everything downstream — game discovery,
// Proton lookup, the compat directory new Proton builds are written into —
// hangs off that one answer, and it is derived entirely from what exists on
// disk under $HOME. A fake $HOME is therefore enough to test all of it.

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include "utils/SteamPaths.h"

class TstSteamPaths : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void noSteamAtAll();
    void bootstrappedButNeverLoggedIn();
    void nativeInstall();
    void flatpakInstall();
    void nativeWinsWhenBothArePresent();
    void followsTheRootSymlink();
    void derivedPathsAreEmptyWithoutSteam();
    void pidFilePathFollowsTheVariant();
    void defaultInstallCompatPathFallbacks();

private:
    QTemporaryDir m_home;
    QByteArray m_realHome;

    QString home() const { return m_home.path(); }

    // The minimum that makes SteamPaths believe a directory is a Steam root:
    // steamapps/libraryfolders.vdf has to exist as a regular file.
    void makeSteamRoot(const QString& root) const
    {
        QDir().mkpath(root + "/steamapps");
        QDir().mkpath(root + "/compatibilitytools.d");
        QFile vdf(root + "/steamapps/libraryfolders.vdf");
        QVERIFY(vdf.open(QIODevice::WriteOnly | QIODevice::Text));
        vdf.write(QString(R"("libraryfolders" { "0" { "path" "%1" } })").arg(root).toUtf8());
    }

    QString nativeRoot() const  { return home() + "/.local/share/Steam"; }
    QString flatpakRoot() const {
        return home() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam";
    }
};

void TstSteamPaths::init()
{
    QVERIFY(m_home.isValid());
    m_realHome = qgetenv("HOME");
    qputenv("HOME", m_home.path().toUtf8());
    // The resolution is cached process-wide, so every case has to start clean.
    SteamPaths::invalidateCache();
}

void TstSteamPaths::cleanup()
{
    qputenv("HOME", m_realHome);
    SteamPaths::invalidateCache();

    // QTemporaryDir is reused across slots in this class; empty it by hand.
    QDir dir(m_home.path());
    for (const QString& entry : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        QFileInfo info(dir.filePath(entry));
        if (info.isDir() && !info.isSymLink()) {
            QDir(info.absoluteFilePath()).removeRecursively();
        } else {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

void TstSteamPaths::noSteamAtAll()
{
    QCOMPARE(SteamPaths::detectedVariant(), SteamPaths::Variant::None);
    QVERIFY(SteamPaths::steamRoot().isEmpty());
}

void TstSteamPaths::bootstrappedButNeverLoggedIn()
{
    // What Valve's bootstrap actually leaves behind: the directory and the
    // ~/.steam/steam symlink, but no steamapps and no libraryfolders.vdf —
    // that file is written by the client later. ProtonForge therefore does not
    // see this installation at all. Documented here because it looks like a
    // bug from the outside and is not one.
    QDir().mkpath(nativeRoot() + "/ubuntu12_32");
    QDir().mkpath(home() + "/.steam");
    QFile::link(nativeRoot(), home() + "/.steam/steam");

    QCOMPARE(SteamPaths::detectedVariant(), SteamPaths::Variant::None);
    QVERIFY(SteamPaths::steamRoot().isEmpty());
}

void TstSteamPaths::nativeInstall()
{
    makeSteamRoot(nativeRoot());
    QDir().mkpath(home() + "/.steam");
    QFile::link(nativeRoot(), home() + "/.steam/steam");

    QCOMPARE(SteamPaths::detectedVariant(), SteamPaths::Variant::Native);
    QCOMPARE(SteamPaths::steamRoot(), nativeRoot());
    QCOMPARE(SteamPaths::steamAppsPath(), nativeRoot() + "/steamapps");
    QCOMPARE(SteamPaths::compatibilityToolsPath(), nativeRoot() + "/compatibilitytools.d");
    QCOMPARE(SteamPaths::configVdfPath(), nativeRoot() + "/config/config.vdf");
    QCOMPARE(SteamPaths::userDataPath(), nativeRoot() + "/userdata");
    QCOMPARE(SteamPaths::steamRuntimePath(), nativeRoot() + "/ubuntu12_32/steam-runtime");
    QCOMPARE(SteamPaths::overlayLibPath(true), nativeRoot() + "/ubuntu12_64/gameoverlayrenderer.so");
    QCOMPARE(SteamPaths::overlayLibPath(false), nativeRoot() + "/ubuntu12_32/gameoverlayrenderer.so");
}

void TstSteamPaths::flatpakInstall()
{
    makeSteamRoot(flatpakRoot());

    QCOMPARE(SteamPaths::detectedVariant(), SteamPaths::Variant::Flatpak);
    QCOMPARE(SteamPaths::steamRoot(), flatpakRoot());
    QCOMPARE(SteamPaths::compatibilityToolsPath(), flatpakRoot() + "/compatibilitytools.d");
}

void TstSteamPaths::nativeWinsWhenBothArePresent()
{
    // Deliberate tie-break: existing users keep the path they already had.
    makeSteamRoot(nativeRoot());
    makeSteamRoot(flatpakRoot());

    QCOMPARE(SteamPaths::detectedVariant(), SteamPaths::Variant::Native);
    QCOMPARE(SteamPaths::steamRoot(), nativeRoot());
}

void TstSteamPaths::followsTheRootSymlink()
{
    // ~/.steam/root is a symlink on every real install and may point at either
    // variant; detection canonicalises before classifying, so a native root
    // reached through the symlink must still classify as native.
    makeSteamRoot(flatpakRoot());
    QDir().mkpath(home() + "/.steam");
    QFile::link(flatpakRoot(), home() + "/.steam/root");

    QCOMPARE(SteamPaths::detectedVariant(), SteamPaths::Variant::Flatpak);
    QCOMPARE(SteamPaths::steamRoot(), flatpakRoot());
}

void TstSteamPaths::derivedPathsAreEmptyWithoutSteam()
{
    // Callers check for an empty string; a half-built path like "/steamapps"
    // would send writes somewhere unexpected.
    QVERIFY(SteamPaths::steamAppsPath().isEmpty());
    QVERIFY(SteamPaths::compatibilityToolsPath().isEmpty());
    QVERIFY(SteamPaths::configVdfPath().isEmpty());
    QVERIFY(SteamPaths::userDataPath().isEmpty());
    QVERIFY(SteamPaths::steamRuntimePath().isEmpty());
    QVERIFY(SteamPaths::overlayLibPath(true).isEmpty());
    QVERIFY(SteamPaths::steamPidFilePath().isEmpty());
}

void TstSteamPaths::pidFilePathFollowsTheVariant()
{
    // The pid file lives under the variant's own $HOME, not under the root.
    makeSteamRoot(nativeRoot());
    QCOMPARE(SteamPaths::steamPidFilePath(), home() + "/.steam/steam.pid");

    cleanup();
    init();

    makeSteamRoot(flatpakRoot());
    QCOMPARE(SteamPaths::steamPidFilePath(),
             home() + "/.var/app/com.valvesoftware.Steam/.steam/steam.pid");
}

void TstSteamPaths::defaultInstallCompatPathFallbacks()
{
    // With Steam present it is just the detected directory.
    makeSteamRoot(nativeRoot());
    QCOMPARE(SteamPaths::defaultInstallCompatPath(), nativeRoot() + "/compatibilitytools.d");

    cleanup();
    init();

    // Without Steam, "Install Proton" still needs somewhere to go. Inside our
    // own Flatpak that has to be Flatpak Steam's directory.
    const QByteArray realFlatpakId = qgetenv("FLATPAK_ID");
    qputenv("FLATPAK_ID", "org.protonforge.ProtonForge");
    QCOMPARE(SteamPaths::defaultInstallCompatPath(),
             home() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/compatibilitytools.d");

    qputenv("FLATPAK_ID", QByteArray());
    QCOMPARE(SteamPaths::defaultInstallCompatPath(), home() + "/.steam/root/compatibilitytools.d");

    qputenv("FLATPAK_ID", realFlatpakId);
}

QTEST_MAIN(TstSteamPaths)
#include "tst_steampaths.moc"
