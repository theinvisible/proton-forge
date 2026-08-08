#ifndef JSONDISKCACHE_H
#define JSONDISKCACHE_H

#include <QByteArray>
#include <QString>

// A JSON response cached on disk under ~/.cache/ProtonForge, with a TTL.
//
// Lifted out of ProtonDBClient, which had the only copy. GOG needs the same
// thing for two more clients — an owned library that would otherwise be ten
// paginated requests every time a dialog opens, and content-system metadata
// that is immutable once published. Three users is where a private helper
// stops being private.
//
// Deliberately not a class: there is no state worth holding, and a namespace
// keeps the call sites reading the same as the code they replaced.
//
// (ProtonDBClient still has its own copy. Moving it across is a follow-up, not
// part of adding GOG — it has behaviour tests that should not ride along with
// an unrelated feature.)
namespace JsonDiskCache {

// <CacheLocation>/<area>, created on demand. `area` names the client: "gog",
// "steam". See the note in the .cpp about the path shape.
QString directory(const QString& area);

// A stable file name for a key. Anything unusual in `key` is escaped, so a
// product id, a URL or a content hash are all safe to pass.
QString filePath(const QString& area, const QString& key);

// True when the file exists and is younger than maxAgeSecs. `out` is untouched
// on a miss, and an empty file counts as a miss — a truncated write should not
// be served as if it were an answer.
bool load(const QString& path, QByteArray& out, int maxAgeSecs);

// Best effort: a cache that cannot be written is not an error worth failing a
// request over.
void save(const QString& path, const QByteArray& data);

// Forget one entry, e.g. after the server said it was stale.
void remove(const QString& path);

} // namespace JsonDiskCache

#endif // JSONDISKCACHE_H
