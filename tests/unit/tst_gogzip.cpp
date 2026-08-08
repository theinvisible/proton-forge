// Reading GOG's Linux offline installers.
//
// These are not ordinary ZIPs, and each difference has its own quiet failure:
//
//   Self-extracting. A shell script with the archive appended, so every offset
//     in the central directory is relative to where the ZIP starts. Reading
//     them as absolute points into the shell header — the classic SFX bug, and
//     it presents as "the archive is corrupt" rather than as an offset error.
//   ZIP64. Real installers exceed 4 GB, so the 32-bit fields hold sentinels and
//     the true values live in the ZIP64 records. Ignoring them yields sizes of
//     0xFFFFFFFF.
//   Split across .bin segments. Extracting the first part and stopping would
//     produce a half-installed game with no error at all.
//   Raw deflate, window bits -15. The zlib wrapper the content system uses is
//     absent here; using the wrong one fails on every compressed entry.
//   And the entry names come off the network, so a name that escapes the
//     install directory has to be refused.

#include <QTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "gog/ZipReader.h"

class TstGogZip : public QObject
{
    Q_OBJECT

private slots:
    void readsASelfExtractingInstaller();
    void findsTheArchiveInsideTheShellScript();
    void listsEveryEntryWithItsMode();

    void readsADeflatedEntry();
    void readsAStoredEntry();
    void extractsToDiskAndKeepsTheExecutableBit();
    void extractedBytesMatchTheOriginal();

    void readsZip64Records();
    void refusesASplitArchive();

    void refusesNamesThatEscape_data();
    void refusesNamesThatEscape();

    void rejectsAnArchiveWhoseEntryIsTruncated();
    void rejectsDataThatUnpacksCleanlyButIsWrong();
    void refusesThingsThatAreNotArchives_data();
    void refusesThingsThatAreNotArchives();

private:
    static QString fixture(const QString& name)
    {
        return QStringLiteral(PROTONFORGE_FIXTURES_DIR) + "/gog/" + name;
    }

    static ZipReader::Entry entryNamed(const ZipReader& reader, const QString& name)
    {
        for (const ZipReader::Entry& entry : reader.entries()) {
            if (entry.name == name) {
                return entry;
            }
        }
        return {};
    }
};

void TstGogZip::readsASelfExtractingInstaller()
{
    ZipReader reader;
    QVERIFY2(reader.open(fixture("sfx-installer.sh")), qPrintable(reader.errorString()));
    QCOMPARE(reader.status(), ZipReader::Status::Ok);
    QCOMPARE(reader.entries().size(), 8);
}

void TstGogZip::findsTheArchiveInsideTheShellScript()
{
    ZipReader reader;
    QVERIFY(reader.open(fixture("sfx-installer.sh")));

    // The whole point: the archive does not start at byte zero, and every
    // offset in it has to be measured from where it does start.
    QVERIFY2(reader.baseOffset() > 0,
             "the shell header was not accounted for — offsets would point into it");

    QFile file(fixture("sfx-installer.sh"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray head = file.read(reader.baseOffset());
    QVERIFY(head.startsWith("#!/bin/sh"));
    // And the ZIP really does begin there.
    QCOMPARE(file.read(4), QByteArray("PK\x03\x04", 4));
}

void TstGogZip::listsEveryEntryWithItsMode()
{
    ZipReader reader;
    QVERIFY(reader.open(fixture("sfx-installer.sh")));

    const ZipReader::Entry launcher = entryNamed(reader, "data/noarch/start.sh");
    QVERIFY(launcher.isExecutable());
    QVERIFY(!launcher.isDirectory);
    QVERIFY(!launcher.isSymlink);

    // A trailing slash is a directory even when the mode says nothing.
    QVERIFY(entryNamed(reader, "data/noarch/").isDirectory);

    const ZipReader::Entry link = entryNamed(reader, "data/noarch/game/latest");
    QVERIFY2(link.isSymlink, "mode 0120000 is a symlink, not a regular file");

    QVERIFY(!entryNamed(reader, "data/noarch/game/data/stored.dat").isExecutable());
}

void TstGogZip::readsADeflatedEntry()
{
    ZipReader reader;
    QVERIFY(reader.open(fixture("sfx-installer.sh")));

    const ZipReader::Entry entry = entryNamed(reader, "data/noarch/start.sh");
    QCOMPARE(entry.method, quint16(8));

    QString error;
    const QByteArray content = reader.readEntry(entry, &error);
    QVERIFY2(!content.isNull(), qPrintable(error));
    QCOMPARE(content, QByteArray("#!/bin/sh\nexec ./game/bin/game \"$@\"\n"));
}

void TstGogZip::readsAStoredEntry()
{
    // Method 0 skips inflate entirely — a separate path, and one that silently
    // returns nothing if it is routed through the decompressor by mistake.
    ZipReader reader;
    QVERIFY(reader.open(fixture("sfx-installer.sh")));

    const ZipReader::Entry entry = entryNamed(reader, "data/noarch/game/data/stored.dat");
    QCOMPARE(entry.method, quint16(0));

    QString error;
    QCOMPARE(reader.readEntry(entry, &error), QByteArray("stored bytes, not deflated"));
}

void TstGogZip::extractsToDiskAndKeepsTheExecutableBit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ZipReader reader;
    QVERIFY(reader.open(fixture("sfx-installer.sh")));

    const ZipReader::Entry entry = entryNamed(reader, "data/noarch/start.sh");
    const QString dest = dir.path() + "/deep/nested/start.sh";

    QString error;
    QVERIFY2(reader.extractEntry(entry, dest, &error), qPrintable(error));

    // Parent directories are created rather than assumed.
    QVERIFY(QFile::exists(dest));
    // Without this the launcher is unrunnable and the game simply does nothing.
    QVERIFY2(QFile::permissions(dest) & QFileDevice::ExeOwner,
             "the executable bit was lost — start.sh would not run");
}

void TstGogZip::extractedBytesMatchTheOriginal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ZipReader reader;
    QVERIFY(reader.open(fixture("sfx-installer.sh")));

    // The largest entry, so the streaming path runs more than one buffer's
    // worth through the decompressor.
    const ZipReader::Entry entry = entryNamed(reader, "data/noarch/game/bin/game");
    QCOMPARE(entry.uncompressedSize, 4003LL);
    QVERIFY2(entry.compressedSize < entry.uncompressedSize,
             "the fixture should be genuinely compressed, or this proves nothing");

    const QString dest = dir.path() + "/game";
    QString error;
    QVERIFY2(reader.extractEntry(entry, dest, &error), qPrintable(error));

    QFile written(dest);
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray bytes = written.readAll();

    QCOMPARE(bytes.size(), 4003);
    QCOMPARE(bytes, reader.readEntry(entry));
    QVERIFY(bytes.startsWith("ELF"));
}

void TstGogZip::readsZip64Records()
{
    // The classic EOCD in this fixture is all sentinels, so a reader that skips
    // the ZIP64 records cannot get a single offset right.
    ZipReader reader;
    QVERIFY2(reader.open(fixture("zip64.zip")), qPrintable(reader.errorString()));
    QCOMPARE(reader.status(), ZipReader::Status::Ok);
    QCOMPARE(reader.entries().size(), 8);

    const ZipReader::Entry entry = entryNamed(reader, "data/noarch/start.sh");
    QString error;
    QCOMPARE(reader.readEntry(entry, &error), QByteArray("#!/bin/sh\nexec ./game/bin/game \"$@\"\n"));
}

void TstGogZip::refusesASplitArchive()
{
    // Extracting the first segment and stopping would leave a half-installed
    // game and report success.
    ZipReader reader;
    QVERIFY(!reader.open(fixture("multipart.zip")));
    QCOMPARE(reader.status(), ZipReader::Status::MultiPart);
    QVERIFY2(reader.errorString().contains("split"), qPrintable(reader.errorString()));

    QVERIFY(ZipReader::namesASplitArchive(fixture("multipart.zip")));
    QVERIFY(!ZipReader::namesASplitArchive(fixture("sfx-installer.sh")));
}

void TstGogZip::refusesNamesThatEscape_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("allowed");

    QTest::newRow("normal")     << "data/noarch/start.sh" << true;
    QTest::newRow("nested")     << "data/noarch/game/bin/game" << true;
    QTest::newRow("traversal")  << "data/noarch/../../escaped.txt" << false;
    QTest::newRow("leading")    << "../escaped.txt" << false;
    QTest::newRow("absolute")   << "/etc/passwd" << false;
    QTest::newRow("drive")      << "C:\\Windows\\cmd.exe" << false;
    QTest::newRow("empty")      << "" << false;
}

void TstGogZip::refusesNamesThatEscape()
{
    QFETCH(QString, name);
    QFETCH(bool, allowed);

    const QString safe = ZipReader::safeName(name);
    if (!allowed) {
        QVERIFY2(safe.isEmpty(), qPrintable("accepted: " + safe));
        return;
    }
    QVERIFY(!safe.isEmpty());
    QVERIFY(!safe.contains(".."));
    QVERIFY(!safe.startsWith('/'));
}

void TstGogZip::rejectsAnArchiveWhoseEntryIsTruncated()
{
    // A download that stopped early. The archive's directory still describes
    // the full entry, so only reading it reveals the damage — and it has to
    // fail rather than write a short file.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile source(fixture("sfx-installer.sh"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray whole = source.readAll();

    ZipReader intact;
    QVERIFY(intact.open(fixture("sfx-installer.sh")));
    const ZipReader::Entry entry = entryNamed(intact, "data/noarch/game/bin/game");

    // Keep the tail (so the directory is still readable) but blank the entry's
    // payload. The offset comes from entryDataOffset, not from a fixed 30 bytes
    // past the local header — the header's name and extra field sit in between,
    // and assuming otherwise damages the name instead of the data, which is a
    // different bug and would not test this one.
    QByteArray damaged = whole;
    const qint64 dataStart = intact.entryDataOffset(entry);
    QVERIFY(dataStart > 0);
    for (qint64 i = dataStart; i < dataStart + entry.compressedSize && i < damaged.size(); ++i) {
        damaged[static_cast<int>(i)] = '\0';
    }

    const QString path = dir.path() + "/damaged.sh";
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(damaged);
    out.close();

    ZipReader reader;
    QVERIFY(reader.open(path));
    QString error;
    const QString dest = dir.path() + "/game";
    QVERIFY2(!reader.extractEntry(entry, dest, &error), "damaged data was accepted");
    QVERIFY(!error.isEmpty());
    // And it leaves nothing half-written behind.
    QVERIFY2(!QFile::exists(dest), "a failed extraction left a partial file on disk");
}

void TstGogZip::rejectsDataThatUnpacksCleanlyButIsWrong()
{
    // The case the CRC exists for, and the only one it can catch. A stored
    // entry has no compression to fail: flip a byte and the extraction still
    // "succeeds", producing a file that is the right length and the wrong
    // content. Nothing downstream would ever notice — the game just misbehaves.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile source(fixture("sfx-installer.sh"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QByteArray damaged = source.readAll();

    ZipReader intact;
    QVERIFY(intact.open(fixture("sfx-installer.sh")));
    const ZipReader::Entry entry = entryNamed(intact, "data/noarch/game/data/stored.dat");
    QCOMPARE(entry.method, quint16(0));   // no inflate to fail on our behalf

    const qint64 dataStart = intact.entryDataOffset(entry);
    QVERIFY(dataStart > 0);
    damaged[static_cast<int>(dataStart)] = static_cast<char>(damaged.at(dataStart) ^ 0x01);

    const QString path = dir.path() + "/damaged.sh";
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(damaged);
    out.close();

    ZipReader reader;
    QVERIFY(reader.open(path));
    QString error;
    const QString dest = dir.path() + "/stored.dat";
    QVERIFY2(!reader.extractEntry(entry, dest, &error),
             "a single flipped byte in a stored entry was accepted");
    QVERIFY2(error.contains("checksum"), qPrintable(error));
    QVERIFY(!QFile::exists(dest));
}

void TstGogZip::refusesThingsThatAreNotArchives_data()
{
    QTest::addColumn<QByteArray>("content");

    QTest::newRow("empty")      << QByteArray();
    QTest::newRow("text")       << QByteArray("#!/bin/sh\necho hello\n");
    QTest::newRow("html")       << QByteArray("<html>404 Not Found</html>");
    QTest::newRow("short")      << QByteArray("PK");
    QTest::newRow("signature only") << QByteArray("PK\x03\x04", 4);
}

void TstGogZip::refusesThingsThatAreNotArchives()
{
    QFETCH(QByteArray, content);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/not-a-zip";

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();

    ZipReader reader;
    QVERIFY(!reader.open(path));
    QVERIFY(reader.status() != ZipReader::Status::Ok);
    QVERIFY(!reader.errorString().isEmpty());
    QVERIFY(reader.entries().isEmpty());
}

QTEST_MAIN(TstGogZip)
#include "tst_gogzip.moc"
