// Turning downloaded chunks into a file on disk.
//
// This is the narrow place where a bad byte becomes a broken game, and none of
// its failure modes announce themselves:
//
//   Offsets accumulate the *inflated* size. Using the compressed size instead
//     gives a file of exactly the right length, full of overlapping garbage.
//   The compressed md5 has to be checked before inflating, or a corrupt chunk
//     is spent decompressing before being thrown away — and if inflate happens
//     to succeed on it, silently accepted.
//   The stream is raw zlib, not Qt's. qUncompress()/qCompress() prepend a
//     four-byte length that GOG never sends, so a payload in Qt's format must
//     be rejected rather than half-read.
//   Chunks arrive out of order and are written concurrently. Whatever order
//     they land in, the file has to come out identical.
//   And a failure has to say whether fetching it again could help. A corrupt
//     body is worth another try; a disk that will not take the write is not,
//     and three rounds of downloading ten megabytes to fail identically is
//     worse than saying so at once.

#include <QTest>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtConcurrent>

#include "gog/GogDownloader.h"

using Chunk = GogContentClient::Chunk;

namespace {

QString md5Of(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

// A chunk as GOG serves one: raw zlib, with both checksums filled in.
QByteArray deflate(const QByteArray& plain)
{
    // qCompress prepends Qt's own 4-byte length. Everything after those four
    // bytes is exactly the zlib stream GOG sends.
    return qCompress(plain, 9).mid(4);
}

Chunk chunkFor(const QByteArray& plain)
{
    const QByteArray compressed = deflate(plain);
    Chunk chunk;
    chunk.md5 = md5Of(plain);
    chunk.compressedMd5 = md5Of(compressed);
    chunk.size = plain.size();
    chunk.compressedSize = compressed.size();
    return chunk;
}

} // namespace

class TstGogChunks : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void placesChunksAtInflatedOffsets();
    void journalKeysAreStableAndPerChunk();
    void placesNothingForAChunklessFile();

    void assemblesAFileFromItsChunks();
    void assemblesTheSameFileWhateverTheOrder();
    void assemblesCorrectlyWhenWrittenConcurrently();

    void rejectsACorruptChunkBeforeInflating();
    void rejectsAChunkThatInflatesToTheWrongBytes();
    void rejectsQtsOwnCompressionFormat();
    void rejectsGarbage();
    void reportsAFileItCannotOpen();

private:
    QTemporaryDir m_dir;
    QString m_file;
};

void TstGogChunks::init()
{
    QVERIFY(m_dir.isValid());
    m_file = m_dir.path() + "/game.dat";
    QFile::remove(m_file);
}

void TstGogChunks::placesChunksAtInflatedOffsets()
{
    GogInstallPlan::FileTask file;
    file.relPath = "bin/game.exe";
    file.chunks = {chunkFor(QByteArray(1000, 'a')),
                   chunkFor(QByteArray(900, 'b')),
                   chunkFor(QByteArray(700, 'c'))};

    // Every chunk here compresses to a tiny fraction of its size, so an offset
    // computed from compressedSize would be obviously — and only here, testably
    // — wrong. (Sizes are kept well clear of zlib's fixed overhead, below which
    // a "compressed" chunk is larger than its input and the check proves
    // nothing.)
    for (const Chunk& chunk : file.chunks) {
        QVERIFY(chunk.compressedSize < chunk.size);
    }

    const QList<GogDownloader::ChunkPlacement> placements =
        GogDownloader::chunkPlacements(file);

    QCOMPARE(placements.size(), 3);
    QCOMPARE(placements.at(0).offset, 0LL);
    QCOMPARE(placements.at(1).offset, 1000LL);
    QCOMPARE(placements.at(2).offset, 1900LL);
}

void TstGogChunks::journalKeysAreStableAndPerChunk()
{
    GogInstallPlan::FileTask file;
    file.relPath = "data/voice.pak";
    file.chunks = {chunkFor("one"), chunkFor("two")};

    const QList<GogDownloader::ChunkPlacement> placements =
        GogDownloader::chunkPlacements(file);

    QCOMPARE(placements.at(0).journalKey, QStringLiteral("data/voice.pak#0"));
    QCOMPARE(placements.at(1).journalKey, QStringLiteral("data/voice.pak#1"));

    // Two files that share a chunk must not share a journal entry, or resuming
    // would skip one of them.
    GogInstallPlan::FileTask other = file;
    other.relPath = "data/other.pak";
    QVERIFY(GogDownloader::chunkPlacements(other).at(0).journalKey
            != placements.at(0).journalKey);
}

void TstGogChunks::placesNothingForAChunklessFile()
{
    // Empty files and symlinks both come through here; neither has anything to
    // fetch, and a placement for them would queue a download of nothing.
    GogInstallPlan::FileTask file;
    file.relPath = "empty.txt";
    QVERIFY(GogDownloader::chunkPlacements(file).isEmpty());
}

void TstGogChunks::assemblesAFileFromItsChunks()
{
    const QByteArray first(4096, 'x');
    const QByteArray second(1024, 'y');

    QFile create(m_file);
    QVERIFY(create.open(QIODevice::WriteOnly));
    QVERIFY(create.resize(first.size() + second.size()));
    create.close();

    const Chunk a = chunkFor(first);
    const Chunk b = chunkFor(second);

    QVERIFY(GogDownloader::writeChunk(deflate(first), a.compressedMd5, a.md5, m_file, 0).ok());
    QVERIFY(GogDownloader::writeChunk(deflate(second), b.compressedMd5, b.md5, m_file,
                                      first.size()).ok());

    QFile written(m_file);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), first + second);
}

void TstGogChunks::assemblesTheSameFileWhateverTheOrder()
{
    const QByteArray first(4096, 'x');
    const QByteArray second(1024, 'y');
    const Chunk a = chunkFor(first);
    const Chunk b = chunkFor(second);

    QFile create(m_file);
    QVERIFY(create.open(QIODevice::WriteOnly));
    QVERIFY(create.resize(first.size() + second.size()));
    create.close();

    // Chunks finish in whatever order the CDN answers, which is not the order
    // they were requested in.
    QVERIFY(GogDownloader::writeChunk(deflate(second), b.compressedMd5, b.md5, m_file,
                                      first.size()).ok());
    QVERIFY(GogDownloader::writeChunk(deflate(first), a.compressedMd5, a.md5, m_file, 0).ok());

    QFile written(m_file);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), first + second);
}

void TstGogChunks::assemblesCorrectlyWhenWrittenConcurrently()
{
    // Four chunks of one file, written from four threads at once — which is
    // exactly what the downloader does, and the reason writeChunk opens its own
    // handle instead of sharing one.
    QList<QByteArray> parts;
    for (int i = 0; i < 4; ++i) {
        parts << QByteArray(8192, static_cast<char>('a' + i));
    }

    QByteArray expected;
    for (const QByteArray& part : parts) {
        expected += part;
    }

    QFile create(m_file);
    QVERIFY(create.open(QIODevice::WriteOnly));
    QVERIFY(create.resize(expected.size()));
    create.close();

    QList<QFuture<GogDownloader::ChunkResult>> futures;
    for (int i = 0; i < parts.size(); ++i) {
        const Chunk chunk = chunkFor(parts.at(i));
        futures << QtConcurrent::run(&GogDownloader::writeChunk, deflate(parts.at(i)),
                                     chunk.compressedMd5, chunk.md5, m_file,
                                     static_cast<qint64>(i) * 8192);
    }
    for (QFuture<GogDownloader::ChunkResult>& future : futures) {
        future.waitForFinished();
        QVERIFY2(future.result().ok(), qPrintable(future.result().error));
    }

    QFile written(m_file);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), expected);
}

void TstGogChunks::rejectsACorruptChunkBeforeInflating()
{
    const QByteArray plain(2048, 'z');
    const Chunk chunk = chunkFor(plain);

    QByteArray damaged = deflate(plain);
    damaged[damaged.size() / 2] = static_cast<char>(damaged.at(damaged.size() / 2) ^ 0xFF);

    const GogDownloader::ChunkResult result =
        GogDownloader::writeChunk(damaged, chunk.compressedMd5, chunk.md5, m_file, 0);

    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains("corrupt"), qPrintable(result.error));
    // The CDN may well hand over a good copy next time, so this one is worth
    // asking for again.
    QVERIFY(result.retriable);
    // And it stopped before touching the file — a rejected chunk must not have
    // written anything.
    QVERIFY(!QFile::exists(m_file));
}

void TstGogChunks::rejectsAChunkThatInflatesToTheWrongBytes()
{
    // Intact on the wire, wrong underneath: the compressed checksum matches but
    // the content is not what the manifest promised.
    const QByteArray plain(2048, 'z');
    const QByteArray compressed = deflate(plain);

    const GogDownloader::ChunkResult result = GogDownloader::writeChunk(
        compressed, md5Of(compressed), md5Of(QByteArray(2048, 'q')), m_file, 0);

    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains("checksum"), qPrintable(result.error));
    QVERIFY(result.retriable);
}

void TstGogChunks::rejectsQtsOwnCompressionFormat()
{
    // The one that would look like it worked: qCompress output is a valid zlib
    // stream behind a 4-byte length prefix GOG never sends. Accepting it would
    // mean we had quietly agreed with Qt instead of with the CDN.
    const QByteArray plain(512, 'k');
    const QByteArray qtFormat = qCompress(plain, 9);

    const GogDownloader::ChunkResult result =
        GogDownloader::writeChunk(qtFormat, QString(), QString(), m_file, 0);

    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains("decompress"), qPrintable(result.error));
}

void TstGogChunks::rejectsGarbage()
{
    for (const QByteArray& body : {QByteArray(), QByteArray("not compressed at all"),
                                   QByteArray("<html>403 Forbidden</html>")}) {
        const GogDownloader::ChunkResult result =
            GogDownloader::writeChunk(body, QString(), QString(), m_file, 0);
        QVERIFY2(!result.ok(), qPrintable("accepted: " + body));
    }
}

void TstGogChunks::reportsAFileItCannotOpen()
{
    const QByteArray plain(64, 'p');
    const QString missing = m_dir.path() + "/no/such/directory/game.dat";

    const GogDownloader::ChunkResult result =
        GogDownloader::writeChunk(deflate(plain), QString(), QString(), missing, 0);

    // Named, so the failure says which file rather than "install failed".
    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains(missing), qPrintable(result.error));
    // And *not* retriable: the directory will still not exist on the third
    // attempt, so re-downloading ten megabytes twice more proves nothing.
    QVERIFY(!result.retriable);
}

QTEST_MAIN(TstGogChunks)
#include "tst_gogchunks.moc"
