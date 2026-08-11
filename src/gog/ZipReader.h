#ifndef ZIPREADER_H
#define ZIPREADER_H

#include <QFile>
#include <QList>
#include <QString>

// A read-only ZIP reader, written for GOG's Linux offline installers.
//
// Those are not ordinary archives, and each way they differ has bitten someone:
//
//   They are self-extracting. The file is a shell script with a ZIP appended,
//   so every offset in the central directory is relative to where the ZIP
//   starts, not to the start of the file. Reading them as if the two were the
//   same is the classic SFX bug — it yields offsets that point into the shell
//   header and an archive that looks corrupt.
//
//   They are routinely larger than 4 GB, so ZIP64 is required rather than
//   optional. Without it the sizes and offsets read back as 0xFFFFFFFF.
//
//   Some are split across .bin segments. This reader does not join them, and
//   says so rather than extracting the first part and calling it done.
//
// Entry names come off the network, so every one of them goes through
// GogContentClient::sanitizeDepotPath before it becomes a path on disk — see
// safeName(). Nothing here writes outside the directory it is given.
class ZipReader
{
public:
    struct Entry {
        QString name;
        qint64 compressedSize = 0;
        qint64 uncompressedSize = 0;
        qint64 localHeaderOffset = 0;   // relative to the archive, not the file
        quint32 crc = 0;
        quint16 method = 0;             // 0 = stored, 8 = deflate
        quint32 unixMode = 0;
        bool isDirectory = false;
        bool isSymlink = false;

        bool isExecutable() const { return (unixMode & 0111) != 0; }
    };

    enum class Status {
        Ok,
        NotOpen,
        NotAZip,        // no end-of-central-directory record anywhere
        MultiPart,      // split across .bin segments; not supported
        Corrupt,
    };

    ZipReader() = default;
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    bool open(const QString& path);
    void close();

    Status status() const { return m_status; }
    QString errorString() const { return m_error; }

    // Where the ZIP begins inside the file. Non-zero for a self-extracting
    // archive — that offset is what every other offset has to be measured from.
    qint64 baseOffset() const { return m_baseOffset; }

    QList<Entry> entries() const { return m_entries; }

    // Whole entry in memory. For the small ones — a manifest, a .info file.
    QByteArray readEntry(const Entry& entry, QString* error = nullptr);

    // Streamed to disk, because a GOG installer's payload does not fit in RAM.
    // Creates parent directories, applies the executable bit, and verifies the
    // CRC — a truncated download that still unpacks is worse than one that
    // fails, because the game only breaks later.
    bool extractEntry(const Entry& entry, const QString& destPath, QString* error = nullptr);

    // Where this entry's bytes actually begin in the file. Public because the
    // local header's own name and extra lengths decide it, and nothing outside
    // this class can work that out.
    qint64 entryDataOffset(const Entry& entry, QString* error = nullptr);

    // The entry's name as a path safely under an install directory, or empty
    // when it escapes. Public so the caller can filter before extracting.
    static QString safeName(const QString& entryName);

    // GOG splits large installers into <name>.sh plus <name>-1.bin, -2.bin, …
    // Detected from the archive's own disk numbers, not from the file names,
    // so a renamed part is still caught.
    static bool namesASplitArchive(const QString& path);

private:
    // `cdEnd` is the file offset where the central directory stops — the EOCD
    // for a classic archive, the ZIP64 record for a ZIP64 one. It is what the
    // archive's base offset is derived from, and using the wrong one is how a
    // ZIP64 self-extracting archive reads as corrupt.
    bool findEndOfCentralDirectory(qint64* cdEnd, qint64* cdOffset, qint64* cdSize,
                                   qint64* entryCount);
    bool readCentralDirectory(qint64 cdStart, qint64 cdSize, qint64 entryCount);
    void fail(Status status, const QString& message);

    QFile m_file;
    QList<Entry> m_entries;
    qint64 m_baseOffset = 0;
    Status m_status = Status::NotOpen;
    QString m_error;
};

#endif // ZIPREADER_H
