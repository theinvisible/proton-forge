#include "ZipReader.h"
#include "gog/GogContentClient.h"

#include <QDir>
#include <QFileInfo>

#include <zlib.h>

namespace {

constexpr quint32 kSigLocalHeader   = 0x04034b50;
constexpr quint32 kSigCentralHeader = 0x02014b50;
constexpr quint32 kSigEocd          = 0x06054b50;
constexpr quint32 kSigZip64Eocd     = 0x06064b50;
constexpr quint32 kSigZip64Locator  = 0x07064b50;

constexpr int kEocdFixedSize = 22;
constexpr int kCentralHeaderFixedSize = 46;
constexpr int kLocalHeaderFixedSize = 30;

// A ZIP comment is a 16-bit length, so the record can be at most this far from
// the end of the file.
constexpr int kMaxCommentSize = 0xFFFF;

// The sentinels that mean "the real value is in the ZIP64 extra field".
constexpr quint32 kZip64Marker32 = 0xFFFFFFFFu;
constexpr quint16 kZip64Marker16 = 0xFFFFu;

constexpr int kStreamBufferSize = 256 * 1024;

quint16 readU16(const QByteArray& data, int offset)
{
    if (offset + 2 > data.size()) {
        return 0;
    }
    const auto* p = reinterpret_cast<const quint8*>(data.constData() + offset);
    return static_cast<quint16>(p[0] | (p[1] << 8));
}

quint32 readU32(const QByteArray& data, int offset)
{
    if (offset + 4 > data.size()) {
        return 0;
    }
    const auto* p = reinterpret_cast<const quint8*>(data.constData() + offset);
    return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8)
           | (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
}

quint64 readU64(const QByteArray& data, int offset)
{
    if (offset + 8 > data.size()) {
        return 0;
    }
    return static_cast<quint64>(readU32(data, offset))
           | (static_cast<quint64>(readU32(data, offset + 4)) << 32);
}

// The ZIP64 extra field (header id 0x0001) carries whichever of the four values
// were too large for their 32-bit slots, in a fixed order and only when the
// slot held the sentinel. Reading them unconditionally is a common bug: the
// field is shorter than the full 28 bytes when only some values overflowed.
void applyZip64Extra(const QByteArray& extra, bool needUncompressed, bool needCompressed,
                     bool needOffset, qint64* uncompressed, qint64* compressed, qint64* offset)
{
    int pos = 0;
    while (pos + 4 <= extra.size()) {
        const quint16 headerId = readU16(extra, pos);
        const quint16 size = readU16(extra, pos + 2);
        const int body = pos + 4;
        if (body + size > extra.size()) {
            return;
        }

        if (headerId == 0x0001) {
            int cursor = body;
            if (needUncompressed && cursor + 8 <= body + size) {
                *uncompressed = static_cast<qint64>(readU64(extra, cursor));
                cursor += 8;
            }
            if (needCompressed && cursor + 8 <= body + size) {
                *compressed = static_cast<qint64>(readU64(extra, cursor));
                cursor += 8;
            }
            if (needOffset && cursor + 8 <= body + size) {
                *offset = static_cast<qint64>(readU64(extra, cursor));
            }
            return;
        }
        pos = body + size;
    }
}

} // namespace

ZipReader::~ZipReader()
{
    close();
}

void ZipReader::close()
{
    if (m_file.isOpen()) {
        m_file.close();
    }
    m_entries.clear();
    m_baseOffset = 0;
    m_status = Status::NotOpen;
    m_error.clear();
}

void ZipReader::fail(Status status, const QString& message)
{
    m_status = status;
    m_error = message;
}

QString ZipReader::safeName(const QString& entryName)
{
    // The same rules a depot item gets, and for the same reason: this name came
    // out of a downloaded file, and "../../.bashrc" is a one-line exploit.
    return GogContentClient::sanitizeDepotPath(entryName);
}

bool ZipReader::open(const QString& path)
{
    close();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        fail(Status::NotOpen, QStringLiteral("cannot open %1: %2").arg(path, m_file.errorString()));
        return false;
    }

    qint64 cdEnd = 0;
    qint64 cdOffset = 0;
    qint64 cdSize = 0;
    qint64 entryCount = 0;
    if (!findEndOfCentralDirectory(&cdEnd, &cdOffset, &cdSize, &entryCount)) {
        return false;
    }

    // The whole SFX trick, in one line. The central directory physically ends
    // at cdEnd, so its real start is cdEnd - cdSize; the archive therefore
    // begins that many bytes before the offset the ZIP claims it does.
    m_baseOffset = cdEnd - cdSize - cdOffset;
    if (m_baseOffset < 0) {
        fail(Status::Corrupt, QStringLiteral("the central directory is larger than the file"));
        return false;
    }

    return readCentralDirectory(m_baseOffset + cdOffset, cdSize, entryCount);
}

bool ZipReader::findEndOfCentralDirectory(qint64* cdEnd, qint64* cdOffset, qint64* cdSize,
                                          qint64* entryCount)
{
    const qint64 fileSize = m_file.size();
    const qint64 searchLen = qMin<qint64>(fileSize, kMaxCommentSize + kEocdFixedSize);
    if (searchLen < kEocdFixedSize) {
        fail(Status::NotAZip, QStringLiteral("the file is too small to be a ZIP archive"));
        return false;
    }

    m_file.seek(fileSize - searchLen);
    const QByteArray tail = m_file.read(searchLen);

    // Backwards, so a file whose *content* happens to contain the signature does
    // not win over the real record at the end.
    int found = -1;
    for (int i = tail.size() - kEocdFixedSize; i >= 0; --i) {
        if (readU32(tail, i) == kSigEocd) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        fail(Status::NotAZip, QStringLiteral("no end-of-central-directory record found"));
        return false;
    }

    const qint64 eocdPos = fileSize - searchLen + found;
    *cdEnd = eocdPos;

    quint16 diskNumber   = readU16(tail, found + 4);
    quint16 cdDisk       = readU16(tail, found + 6);
    quint16 entriesHere  = readU16(tail, found + 8);
    quint16 entriesTotal = readU16(tail, found + 10);
    quint32 size32       = readU32(tail, found + 12);
    quint32 offset32     = readU32(tail, found + 16);

    // ZIP64: the locator sits immediately before the EOCD and points at the real
    // record. Looked for whenever it is there, not only when a sentinel appears
    // — some writers emit both forms.
    const int locatorPos = found - 20;
    if (locatorPos >= 0 && readU32(tail, locatorPos) == kSigZip64Locator) {
        const qint64 zip64Pos = static_cast<qint64>(readU64(tail, locatorPos + 8));
        const quint32 totalDisks = readU32(tail, locatorPos + 16);

        // The locator's offset is absolute within the archive; for an SFX the
        // shell header shifts it, and the base is not known yet. Both readings
        // are tried, so neither a plain nor an appended archive is refused.
        for (const qint64 candidate : {zip64Pos, eocdPos - 20 - 56}) {
            if (candidate < 0 || candidate + 56 > fileSize) {
                continue;
            }
            m_file.seek(candidate);
            const QByteArray record = m_file.read(56);
            if (readU32(record, 0) != kSigZip64Eocd) {
                continue;
            }

            diskNumber   = static_cast<quint16>(readU32(record, 16));
            cdDisk       = static_cast<quint16>(readU32(record, 20));
            *entryCount  = static_cast<qint64>(readU64(record, 24));
            const qint64 totalEntries = static_cast<qint64>(readU64(record, 32));
            *cdSize      = static_cast<qint64>(readU64(record, 40));
            *cdOffset    = static_cast<qint64>(readU64(record, 48));

            // The directory stops here, not at the classic EOCD: the ZIP64
            // record and its locator sit between the two.
            *cdEnd = candidate;

            if (diskNumber != 0 || cdDisk != 0 || totalDisks > 1 || *entryCount != totalEntries) {
                fail(Status::MultiPart,
                     QStringLiteral("this installer is split across several files, which "
                                    "ProtonForge cannot join. Download it from GOG's website "
                                    "instead."));
                return false;
            }
            return true;
        }
    }

    if (diskNumber != 0 || cdDisk != 0 || entriesHere != entriesTotal) {
        fail(Status::MultiPart,
             QStringLiteral("this installer is split across several files, which ProtonForge "
                            "cannot join. Download it from GOG's website instead."));
        return false;
    }

    if (size32 == kZip64Marker32 || offset32 == kZip64Marker32
        || entriesTotal == kZip64Marker16) {
        fail(Status::Corrupt,
             QStringLiteral("the archive says it is ZIP64 but carries no ZIP64 record"));
        return false;
    }

    *cdSize = size32;
    *cdOffset = offset32;
    *entryCount = entriesTotal;
    return true;
}

bool ZipReader::readCentralDirectory(qint64 cdStart, qint64 cdSize, qint64 entryCount)
{
    if (cdStart < 0 || cdSize < 0 || cdStart + cdSize > m_file.size()) {
        fail(Status::Corrupt, QStringLiteral("the central directory lies outside the file"));
        return false;
    }

    m_file.seek(cdStart);
    const QByteArray cd = m_file.read(cdSize);
    if (cd.size() != cdSize) {
        fail(Status::Corrupt, QStringLiteral("the central directory is truncated"));
        return false;
    }

    int pos = 0;
    for (qint64 i = 0; i < entryCount; ++i) {
        if (pos + kCentralHeaderFixedSize > cd.size() || readU32(cd, pos) != kSigCentralHeader) {
            fail(Status::Corrupt, QStringLiteral("the central directory is malformed"));
            return false;
        }

        const quint16 nameLen    = readU16(cd, pos + 28);
        const quint16 extraLen   = readU16(cd, pos + 30);
        const quint16 commentLen = readU16(cd, pos + 32);
        const quint32 externals  = readU32(cd, pos + 38);

        Entry entry;
        entry.method           = readU16(cd, pos + 10);
        entry.crc              = readU32(cd, pos + 16);
        entry.compressedSize   = readU32(cd, pos + 20);
        entry.uncompressedSize = readU32(cd, pos + 24);
        entry.localHeaderOffset = readU32(cd, pos + 42);
        entry.name = QString::fromUtf8(cd.mid(pos + kCentralHeaderFixedSize, nameLen));

        const QByteArray extra = cd.mid(pos + kCentralHeaderFixedSize + nameLen, extraLen);
        applyZip64Extra(extra,
                        entry.uncompressedSize == static_cast<qint64>(kZip64Marker32),
                        entry.compressedSize == static_cast<qint64>(kZip64Marker32),
                        entry.localHeaderOffset == static_cast<qint64>(kZip64Marker32),
                        &entry.uncompressedSize, &entry.compressedSize,
                        &entry.localHeaderOffset);

        // The high 16 bits of the external attributes are the Unix mode, but
        // only when the archive was made on a Unix-like system.
        entry.unixMode = externals >> 16;
        entry.isSymlink = (entry.unixMode & 0170000) == 0120000;
        entry.isDirectory = entry.name.endsWith('/')
                            || (entry.unixMode & 0170000) == 0040000;

        m_entries.append(entry);
        pos += kCentralHeaderFixedSize + nameLen + extraLen + commentLen;
    }

    m_status = Status::Ok;
    return true;
}

qint64 ZipReader::entryDataOffset(const Entry& entry, QString* error)
{
    const qint64 headerPos = m_baseOffset + entry.localHeaderOffset;
    if (headerPos < 0 || headerPos + kLocalHeaderFixedSize > m_file.size()) {
        if (error) {
            *error = QStringLiteral("%1: its local header lies outside the file").arg(entry.name);
        }
        return -1;
    }

    m_file.seek(headerPos);
    const QByteArray header = m_file.read(kLocalHeaderFixedSize);
    if (readU32(header, 0) != kSigLocalHeader) {
        if (error) {
            *error = QStringLiteral("%1: no local header where the directory said").arg(entry.name);
        }
        return -1;
    }

    // The local header's name and extra lengths are authoritative here and
    // frequently differ from the central directory's — the extra field in
    // particular is often padded differently. Assuming a fixed 30 bytes reads
    // the entry's own name back as its content.
    const quint16 nameLen = readU16(header, 26);
    const quint16 extraLen = readU16(header, 28);
    return headerPos + kLocalHeaderFixedSize + nameLen + extraLen;
}

QByteArray ZipReader::readEntry(const Entry& entry, QString* error)
{
    if (m_status != Status::Ok) {
        if (error) {
            *error = m_error;
        }
        return QByteArray();
    }

    const qint64 dataOffset = entryDataOffset(entry, error);
    if (dataOffset < 0) {
        return QByteArray();
    }

    m_file.seek(dataOffset);
    const QByteArray raw = m_file.read(entry.compressedSize);
    if (raw.size() != entry.compressedSize) {
        if (error) {
            *error = QStringLiteral("%1: the archive ends mid-entry").arg(entry.name);
        }
        return QByteArray();
    }

    if (entry.method == 0) {
        return raw;
    }
    if (entry.method != 8) {
        if (error) {
            *error = QStringLiteral("%1: unsupported compression method %2")
                         .arg(entry.name).arg(entry.method);
        }
        return QByteArray();
    }

    // Raw deflate, window bits -15 — a ZIP stores the deflate stream with no
    // zlib wrapper around it, unlike the content system's bodies.
    const QByteArray out = GogContentClient::inflateData(raw, -15);
    if (out.isNull()) {
        if (error) {
            *error = QStringLiteral("%1: could not be decompressed").arg(entry.name);
        }
    }
    return out;
}

bool ZipReader::extractEntry(const Entry& entry, const QString& destPath, QString* error)
{
    if (m_status != Status::Ok) {
        if (error) {
            *error = m_error;
        }
        return false;
    }
    if (entry.method != 0 && entry.method != 8) {
        if (error) {
            *error = QStringLiteral("%1: unsupported compression method %2")
                         .arg(entry.name).arg(entry.method);
        }
        return false;
    }

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    const qint64 dataOffset = entryDataOffset(entry, error);
    if (dataOffset < 0) {
        return false;
    }

    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("cannot write %1: %2").arg(destPath, out.errorString());
        }
        return false;
    }

    m_file.seek(dataOffset);
    qint64 remaining = entry.compressedSize;
    quint32 crc = crc32(0L, nullptr, 0);
    bool ok = true;
    QString failure;

    if (entry.method == 0) {
        QByteArray buffer;
        while (remaining > 0) {
            buffer = m_file.read(qMin<qint64>(remaining, kStreamBufferSize));
            if (buffer.isEmpty()) {
                ok = false;
                failure = QStringLiteral("%1: the archive ends mid-entry").arg(entry.name);
                break;
            }
            crc = crc32(crc, reinterpret_cast<const Bytef*>(buffer.constData()), buffer.size());
            if (out.write(buffer) != buffer.size()) {
                ok = false;
                failure = QStringLiteral("cannot write %1: %2").arg(destPath, out.errorString());
                break;
            }
            remaining -= buffer.size();
        }
    } else {
        // Streamed rather than read whole: a GOG installer's payload runs to
        // gigabytes, and buffering it is the difference between working and
        // being killed by the OOM reaper.
        z_stream stream = {};
        if (inflateInit2(&stream, -15) != Z_OK) {
            if (error) {
                *error = QStringLiteral("%1: could not start decompression").arg(entry.name);
            }
            return false;
        }

        QByteArray inBuffer;
        QByteArray outBuffer(kStreamBufferSize, Qt::Uninitialized);
        int status = Z_OK;

        while (status != Z_STREAM_END) {
            if (stream.avail_in == 0) {
                if (remaining <= 0) {
                    ok = false;
                    failure = QStringLiteral("%1: the archive ends mid-entry").arg(entry.name);
                    break;
                }
                inBuffer = m_file.read(qMin<qint64>(remaining, kStreamBufferSize));
                if (inBuffer.isEmpty()) {
                    ok = false;
                    failure = QStringLiteral("%1: the archive ends mid-entry").arg(entry.name);
                    break;
                }
                remaining -= inBuffer.size();
                stream.next_in = reinterpret_cast<Bytef*>(inBuffer.data());
                stream.avail_in = static_cast<uInt>(inBuffer.size());
            }

            stream.next_out = reinterpret_cast<Bytef*>(outBuffer.data());
            stream.avail_out = static_cast<uInt>(outBuffer.size());

            status = inflate(&stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
                ok = false;
                failure = QStringLiteral("%1: could not be decompressed").arg(entry.name);
                break;
            }

            const qint64 produced = outBuffer.size() - static_cast<qint64>(stream.avail_out);
            if (produced > 0) {
                crc = crc32(crc, reinterpret_cast<const Bytef*>(outBuffer.constData()),
                            static_cast<uInt>(produced));
                if (out.write(outBuffer.constData(), produced) != produced) {
                    ok = false;
                    failure = QStringLiteral("cannot write %1: %2").arg(destPath, out.errorString());
                    break;
                }
            }
            if (status == Z_BUF_ERROR && produced == 0 && stream.avail_in == 0 && remaining <= 0) {
                ok = false;
                failure = QStringLiteral("%1: the archive ends mid-entry").arg(entry.name);
                break;
            }
        }
        inflateEnd(&stream);
    }

    out.close();

    // A truncated download that still unpacks is worse than one that fails: the
    // game breaks later, somewhere unrelated, with nothing pointing back here.
    if (ok && entry.crc != 0 && crc != entry.crc) {
        ok = false;
        failure = QStringLiteral("%1: checksum mismatch — the download is damaged").arg(entry.name);
    }

    if (!ok) {
        out.remove();
        if (error) {
            *error = failure;
        }
        return false;
    }

    if (entry.isExecutable()) {
        QFile written(destPath);
        written.setPermissions(written.permissions() | QFileDevice::ExeOwner
                               | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    }
    return true;
}

bool ZipReader::namesASplitArchive(const QString& path)
{
    ZipReader reader;
    reader.open(path);
    return reader.status() == Status::MultiPart;
}
