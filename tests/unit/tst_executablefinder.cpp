// Which file in an install directory is the game. The executable selector and
// a launch without a saved executable used to answer that with two different
// searches — different junk filters, different name matching — so the panel
// could suggest one file while a launch started another. ExecutableFinder is
// the one answer now; a temporary tree stands in for the install directory.

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "utils/ExecutableFinder.h"

class TstExecutableFinder : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void windowsLeavesOutHelpers();
    void shallowestFirst();
    void matchesTheGameName();
    void matchesTheInstallDirectory();
    void ignoresSuffixesInParentheses();
    void halfTheNameIsEnough();
    void launcherIsNotChosenAutomatically();
    void launcherWhenThereIsNothingElse();
    void prefers64BitBuildsBeforeFallingBack();
    void detectsElfExecutables();
    void linuxCandidates();
    void electronGame();
    void cancelStopsTheWalk();

private:
    QTemporaryDir m_root;
    QString m_game;
    int m_counter = 0;

    // Creates `relative` under the game directory; `executable` sets the mode.
    QString touch(const QString& relative, const QByteArray& content = "x",
                  bool executable = false)
    {
        const QString path = m_game + "/" + relative;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return {};
        f.write(content);
        f.close();
        if (executable)
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        return path;
    }

    // A minimal ELF header with e_type `type` in little-endian order.
    static QByteArray elf(unsigned char type)
    {
        QByteArray h(20, '\0');
        h[0] = 0x7f; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
        h[16] = static_cast<char>(type);
        return h;
    }

    static QStringList names(const QStringList& paths)
    {
        QStringList out;
        for (const QString& p : paths) out << QFileInfo(p).fileName();
        return out;
    }
};

void TstExecutableFinder::init()
{
    QVERIFY(m_root.isValid());
    // A fresh directory per test, named like a Steam install folder.
    m_game = m_root.path() + QString("/%1/Stellaris").arg(++m_counter);
    QVERIFY(QDir().mkpath(m_game));
}

void TstExecutableFinder::windowsLeavesOutHelpers()
{
    touch("stellaris.exe");
    touch("unins000.exe");
    touch("UnityCrashHandler64.exe");
    touch("_CommonRedist/vcredist/vc_redist.x64.exe");
    touch("EasyAntiCheat/EasyAntiCheat_EOS_Setup.exe");
    touch("System/ucc.exe");
    touch("readme.txt");

    QCOMPARE(names(ExecutableFinder::windowsExecutables(m_game)),
             QStringList{"stellaris.exe"});
}

void TstExecutableFinder::shallowestFirst()
{
    touch("bin/win64/deep.exe");
    touch("top.exe");
    touch("bin/b.exe");
    touch("bin/a.exe");

    QCOMPARE(names(ExecutableFinder::windowsExecutables(m_game)),
             (QStringList{"top.exe", "a.exe", "b.exe", "deep.exe"}));
}

void TstExecutableFinder::matchesTheGameName()
{
    const QStringList c = {touch("dowser.exe"), touch("bin/Stellaris.exe")};
    QCOMPARE(QFileInfo(ExecutableFinder::bestMatch("Stellaris", m_game, c)).fileName(),
             QString("Stellaris.exe"));
}

void TstExecutableFinder::matchesTheInstallDirectory()
{
    // "Half-Life 2" in /common/hl2: neither name is the other, the folder is.
    const QString dir = m_root.path() + "/common/hl2";
    const QStringList c = {dir + "/bin/other.exe", dir + "/hl2.exe"};
    QCOMPARE(ExecutableFinder::bestMatch("Half-Life 2", dir, c), dir + "/hl2.exe");
}

void TstExecutableFinder::ignoresSuffixesInParentheses()
{
    const QStringList c = {touch("Tools.exe"), touch("Agent64-SpiesNeverDie.exe")};
    QCOMPARE(QFileInfo(ExecutableFinder::bestMatch("Agent 64: Spies Never Die (Demo)",
                                                   m_game, c)).fileName(),
             QString("Agent64-SpiesNeverDie.exe"));
}

void TstExecutableFinder::halfTheNameIsEnough()
{
    // "corsaircove" -> "corsa" is inside "CorsairCove-Win64-Shipping".
    const QStringList c = {touch("Tools.exe"), touch("bin/CorsairCove-Win64-Shipping.exe")};
    QCOMPARE(QFileInfo(ExecutableFinder::bestMatch("Corsair Cove", m_game, c)).fileName(),
             QString("CorsairCove-Win64-Shipping.exe"));
}

void TstExecutableFinder::launcherIsNotChosenAutomatically()
{
    // Listed — someone may need it — but a launch without a choice starts the
    // game, not its launcher, even when the launcher sits higher up.
    const QStringList c = ExecutableFinder::windowsExecutables(
        (touch("X4Launcher.exe"), touch("bin/X4.exe"), m_game));
    QCOMPARE(names(c), (QStringList{"X4Launcher.exe", "X4.exe"}));
    QCOMPARE(QFileInfo(ExecutableFinder::bestMatch("X4: Foundations", m_game, c)).fileName(),
             QString("X4.exe"));
}

void TstExecutableFinder::launcherWhenThereIsNothingElse()
{
    const QStringList c = {touch("GameLauncher.exe")};
    QCOMPARE(ExecutableFinder::bestMatch("Something Else", m_game, c), c.first());
}

void TstExecutableFinder::prefers64BitBuildsBeforeFallingBack()
{
    const QStringList c = {m_game + "/game.x86", m_game + "/game.x86_64"};
    QCOMPARE(ExecutableFinder::bestMatch("Unrelated Title", m_game, c), c.at(1));
    QCOMPARE(ExecutableFinder::bestMatch("Unrelated Title", m_game, {c.at(0)}), c.at(0));
    QCOMPARE(ExecutableFinder::bestMatch("Anything", m_game, {}), QString());
}

void TstExecutableFinder::detectsElfExecutables()
{
    QVERIFY(ExecutableFinder::isElfExecutable(touch("exec", elf(2))));      // ET_EXEC
    QVERIFY(ExecutableFinder::isElfExecutable(touch("pie", elf(3))));       // ET_DYN (PIE)
    QVERIFY(!ExecutableFinder::isElfExecutable(touch("object", elf(1))));   // ET_REL
    QVERIFY(!ExecutableFinder::isElfExecutable(touch("script", "#!/bin/sh\nexec x\n")));
    QVERIFY(!ExecutableFinder::isElfExecutable(touch("short", "\x7f" "ELF")));
    QVERIFY(!ExecutableFinder::isElfExecutable(m_game + "/missing"));
}

void TstExecutableFinder::linuxCandidates()
{
    touch("Stellaris", elf(3), true);
    touch("run_game", "#!/bin/sh\nexec ./Stellaris\n", true);   // extensionless launcher script
    touch("start.sh", "#!/bin/sh\n", true);                       // scripts with an extension: no
    touch("libsteam_api.so", elf(3), true);
    touch("notes", "plain text", true);                           // executable but neither
    touch("data/levels.bin", elf(2), false);                      // ELF but not executable
    touch("crashpad_handler", elf(2), true);

    QCOMPARE(names(ExecutableFinder::linuxExecutables(m_game)),
             (QStringList{"Stellaris", "run_game"}));
    QCOMPARE(QFileInfo(ExecutableFinder::find("Stellaris", m_game, true)).fileName(),
             QString("Stellaris"));
}

void TstExecutableFinder::electronGame()
{
    // Rise of the Merchants, as installed: the binary is "rotm", next to
    // Chromium's helpers, shared libraries and a node_modules tree with a
    // "linux-x64" addon — which a path-based 64-bit preference used to pick.
    touch("rotm", elf(3), true);
    touch("chrome-sandbox", elf(3), true);
    touch("chrome_crashpad_handler", elf(3), true);
    touch("libvulkan.so.1", elf(3), true);
    touch("resources/app.asar.unpacked/node_modules/steamworks.js/dist/linux64/"
          "steamworksjs.linux-x64-gnu.node", elf(3), true);
    touch("resources/app.asar.unpacked/node_modules/@img/sharp-win32-x64/package.json",
          "{}", true);

    QCOMPARE(names(ExecutableFinder::linuxExecutables(m_game)), QStringList{"rotm"});
    QCOMPARE(QFileInfo(ExecutableFinder::find("Rise of the Merchants Playtest", m_game, true))
                 .fileName(),
             QString("rotm"));
}

void TstExecutableFinder::cancelStopsTheWalk()
{
    touch("stellaris.exe");
    auto cancel = std::make_shared<std::atomic<bool>>(true);
    QVERIFY(ExecutableFinder::candidates(m_game, false, cancel).isEmpty());
    QCOMPARE(ExecutableFinder::candidates(m_game, false).size(), 1);
}

QTEST_MAIN(TstExecutableFinder)
#include "tst_executablefinder.moc"
