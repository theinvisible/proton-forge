#include "JsonDiskCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace JsonDiskCache {

namespace {

// Keys arrive as product ids, URLs and content hashes. Anything that is not
// plainly safe in a file name becomes '_', and an over-long key is truncated —
// a GOG manifest hash is 32 characters, but a URL used as a key is not.
QString sanitizeKey(const QString& key)
{
    QString safe = key;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (safe.size() > 100) {
        safe.truncate(100);
    }
    return safe.isEmpty() ? QStringLiteral("_") : safe;
}

} // namespace

QString directory(const QString& area)
{
    // CacheLocation is already <XDG cache>/<organisation>/<application>, which
    // for this app is .cache/ProtonForge/ProtonForge. Appending the app name a
    // third time is what ProtonDBClient does, and it is a wart rather than a
    // convention — not worth carrying forward into new code. (ProtonDBClient
    // keeps its own path until it moves onto this helper, so the two layouts
    // differ in the meantime.)
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/" + area;
    QDir().mkpath(dir);
    return dir;
}

QString filePath(const QString& area, const QString& key)
{
    return directory(area) + "/" + sanitizeKey(key) + ".json";
}

bool load(const QString& path, QByteArray& out, int maxAgeSecs)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return false;
    }
    if (info.lastModified().secsTo(QDateTime::currentDateTime()) > maxAgeSecs) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        return false;   // a truncated write is a miss, not an empty answer
    }
    out = data;
    return true;
}

void save(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
    }
}

void remove(const QString& path)
{
    QFile::remove(path);
}

} // namespace JsonDiskCache
