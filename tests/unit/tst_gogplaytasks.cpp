// Reading goggame-<id>.info — how a GOG install says what to start.
//
// Getting this wrong does not crash anything, it just launches the wrong thing:
//
//   The first playTask is frequently a hidden configuration tool. Taking it
//     gives the user a settings dialog where they expected a game.
//   isPrimary is the answer when it is present, and often it is not.
//   `arguments` is a string in most files and an array in some.
//   The path uses Windows separators and may disagree with the depot about
//     case — invisible on Windows, a game that will not start here.
//   And it is a downloaded file, so a path in it can try to escape the install
//     directory exactly like a depot item can.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "gog/GogPlayTasks.h"

class TstGogPlayTasks : public QObject
{
    Q_OBJECT

private slots:
    void readsAWindowsInfoFile();
    void picksThePrimaryGameTask();
    void skipsHiddenAndNonGameTasks();
    void fallsBackWhenNothingIsCategorised();
    void readsArgumentsGivenAsAnArray();

    void splitsArguments_data();
    void splitsArguments();

    void refusesPathsThatEscapeTheInstall_data();
    void refusesPathsThatEscapeTheInstall();

    void findsAnExecutableWhoseCaseDiffers();
    void reportsNothingWhenTheExecutableIsMissing();

    void tellsLinuxEntryPointsFromWindowsOnes_data();
    void tellsLinuxEntryPointsFromWindowsOnes();

    void survivesGarbage_data();
    void survivesGarbage();

private:
    static QByteArray fixture(const QString& name)
    {
        QFile file(QStringLiteral(PROTONFORGE_FIXTURES_DIR) + "/gog/" + name);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
};

void TstGogPlayTasks::readsAWindowsInfoFile()
{
    const GogPlayTasks::Info info = GogPlayTasks::parseInfoFile(fixture("goggame-windows.info"));

    QVERIFY(info.valid);
    QCOMPARE(info.gameId, QStringLiteral("1207658930"));
    QCOMPARE(info.name, QStringLiteral("The Witcher 2: Assassins of Kings"));
    QCOMPARE(info.buildId, QStringLiteral("56452082907692588"));
    QCOMPARE(info.playTasks.size(), 4);

    // "version": 1 in this file is the *info file's* schema version. It was
    // briefly wired through as the game's version, which made every GOG game
    // report "Version: 1"; the real one comes from the build listing.
    QCOMPARE(info.formatVersion, QStringLiteral("1"));
}

void TstGogPlayTasks::picksThePrimaryGameTask()
{
    const GogPlayTasks::Info info = GogPlayTasks::parseInfoFile(fixture("goggame-windows.info"));
    const GogPlayTasks::PlayTask task = GogPlayTasks::primaryTask(info);

    // Specifically not SystemSettings.exe, which comes first in the file.
    QCOMPARE(task.path, QStringLiteral("bin\\witcher2.exe"));
    QCOMPARE(task.workingDir, QStringLiteral("bin"));
    QCOMPARE(task.arguments, QStringList({"-launcher", "--skipintro", "Save Games"}));
}

void TstGogPlayTasks::skipsHiddenAndNonGameTasks()
{
    const GogPlayTasks::Info info = GogPlayTasks::parseInfoFile(fixture("goggame-windows.info"));
    const QList<GogPlayTasks::PlayTask> tasks = GogPlayTasks::gameTasks(info);

    // Two of the four: the hidden launcher and the URLTask are not games.
    QCOMPARE(tasks.size(), 2);
    for (const GogPlayTasks::PlayTask& task : tasks) {
        QVERIFY(!task.isHidden);
        QCOMPARE(task.type, QStringLiteral("FileTask"));
    }
}

void TstGogPlayTasks::fallsBackWhenNothingIsCategorised()
{
    // Older info files leave `category` off. Refusing to launch would be worse
    // than starting the only thing on offer.
    const QByteArray json = R"({
        "gameId": "1",
        "playTasks": [
            {"type": "URLTask", "link": "https://gog.com"},
            {"type": "FileTask", "path": "game.exe"}
        ]
    })";

    const GogPlayTasks::Info info = GogPlayTasks::parseInfoFile(json);
    QVERIFY(GogPlayTasks::gameTasks(info).isEmpty());
    QCOMPARE(GogPlayTasks::primaryTask(info).path, QStringLiteral("game.exe"));
}

void TstGogPlayTasks::readsArgumentsGivenAsAnArray()
{
    const GogPlayTasks::Info info = GogPlayTasks::parseInfoFile(fixture("goggame-linux.info"));
    const GogPlayTasks::PlayTask task = GogPlayTasks::primaryTask(info);

    QCOMPARE(task.arguments, QStringList({"--fullscreen", "--no-intro"}));
    QCOMPARE(task.path, QStringLiteral("start.sh"));
}

void TstGogPlayTasks::splitsArguments_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("empty")         << QString() << QStringList();
    QTest::newRow("spaces only")   << "   " << QStringList();
    QTest::newRow("plain")         << "-a -b" << QStringList({"-a", "-b"});
    QTest::newRow("collapses")     << "  -a    -b  " << QStringList({"-a", "-b"});
    QTest::newRow("double quotes") << "-path \"C:\\My Games\""
                                   << QStringList({"-path", "C:\\My Games"});
    QTest::newRow("single quotes") << "-name 'two words'" << QStringList({"-name", "two words"});
    QTest::newRow("empty quoted")  << "-name \"\"" << QStringList({"-name", ""});
    QTest::newRow("escaped quote") << "-say \\\"hi\\\"" << QStringList({"-say", "\"hi\""});
    QTest::newRow("glued")         << "a\"b c\"d" << QStringList({"ab cd"});
}

void TstGogPlayTasks::splitsArguments()
{
    QFETCH(QString, raw);
    QFETCH(QStringList, expected);
    QCOMPARE(GogPlayTasks::splitArguments(raw), expected);
}

void TstGogPlayTasks::refusesPathsThatEscapeTheInstall_data()
{
    QTest::addColumn<QString>("taskPath");
    QTest::addColumn<bool>("allowed");

    QTest::newRow("normal")        << "bin\\game.exe" << true;
    QTest::newRow("forward")       << "bin/game.exe" << true;
    QTest::newRow("parent")        << "../game.exe" << false;
    QTest::newRow("buried parent") << "bin/../../game.exe" << false;
    QTest::newRow("absolute")      << "/etc/passwd" << false;
    QTest::newRow("drive letter")  << "C:\\Windows\\system32\\cmd.exe" << false;
    QTest::newRow("just dotdot")   << ".." << false;
    QTest::newRow("empty")         << "" << false;
}

void TstGogPlayTasks::refusesPathsThatEscapeTheInstall()
{
    QFETCH(QString, taskPath);
    QFETCH(bool, allowed);

    const QString resolved = GogPlayTasks::resolveExecutable("/games/Witcher", taskPath);
    if (!allowed) {
        QVERIFY2(resolved.isEmpty(), qPrintable("accepted: " + resolved));
        return;
    }
    QVERIFY(resolved.startsWith("/games/Witcher/"));
    QVERIFY(!resolved.contains(".."));
}

void TstGogPlayTasks::findsAnExecutableWhoseCaseDiffers()
{
    // The depot wrote Bin/Witcher2.exe; the info file says bin\witcher2.exe.
    // On Windows that is the same file. Here it is a game that will not start
    // unless somebody goes looking.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("Bin"));
    QFile exe(dir.path() + "/Bin/Witcher2.exe");
    QVERIFY(exe.open(QIODevice::WriteOnly));
    exe.close();

    const QString resolved =
        GogPlayTasks::resolveExecutableOnDisk(dir.path(), "bin\\witcher2.exe");

    QCOMPARE(resolved, dir.path() + "/Bin/Witcher2.exe");
    QVERIFY(QFile::exists(resolved));
}

void TstGogPlayTasks::reportsNothingWhenTheExecutableIsMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Empty, not a path that does not exist — a caller that stored the latter
    // would hand GameRunner something to fail on much later.
    QVERIFY(GogPlayTasks::resolveExecutableOnDisk(dir.path(), "bin/nothing.exe").isEmpty());
}

void TstGogPlayTasks::tellsLinuxEntryPointsFromWindowsOnes_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("native");

    QTest::newRow("start.sh")   << "start.sh" << true;
    QTest::newRow("elf")        << "game/bin/FTL" << true;
    QTest::newRow("exe")        << "bin\\witcher2.exe" << false;
    QTest::newRow("exe caps")   << "BIN\\GAME.EXE" << false;
    QTest::newRow("bat")        << "run.bat" << false;
    QTest::newRow("empty")      << "" << false;
}

void TstGogPlayTasks::tellsLinuxEntryPointsFromWindowsOnes()
{
    QFETCH(QString, path);
    QFETCH(bool, native);
    QCOMPARE(GogPlayTasks::looksNativeLinux(path), native);
}

void TstGogPlayTasks::survivesGarbage_data()
{
    QTest::addColumn<QByteArray>("json");

    QTest::newRow("empty")     << QByteArray();
    QTest::newRow("not json")  << QByteArray("not json");
    QTest::newRow("array")     << QByteArray("[]");
    QTest::newRow("object")    << QByteArray("{}");
    QTest::newRow("html")      << QByteArray("<html>404</html>");
    QTest::newRow("truncated") << QByteArray("{\"gameId\": \"1\", \"playTasks\": [{");
    QTest::newRow("wrong types")
        << QByteArray("{\"gameId\": 5, \"playTasks\": \"nope\"}");
}

void TstGogPlayTasks::survivesGarbage()
{
    QFETCH(QByteArray, json);

    const GogPlayTasks::Info info = GogPlayTasks::parseInfoFile(json);
    QVERIFY(!info.valid);
    QVERIFY(GogPlayTasks::primaryTask(info).path.isEmpty());
    QVERIFY(GogPlayTasks::gameTasks(info).isEmpty());
}

QTEST_MAIN(TstGogPlayTasks)
#include "tst_gogplaytasks.moc"
