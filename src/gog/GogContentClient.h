#ifndef GOGCONTENTCLIENT_H
#define GOGCONTENTCLIENT_H

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

// GOG's content system, version 2 — the half that says what a game is made of.
//
// Three things about this API are easy to get wrong and expensive to debug:
//
//   Every response body is zlib-encoded with no Content-Encoding header, so Qt
//   will not inflate it and qUncompress() cannot (it wants Qt's own 4-byte
//   length prefix). Everything goes through inflateData().
//
//   Chunk URLs are templated. secure_link hands back a format string plus a
//   parameter map, and the chunk's own path is appended to one of those
//   parameters before substitution.
//
//   The signed token in those parameters expires, independently of the OAuth
//   token. secureLinkExpiry() is what lets the downloader re-sign before that
//   happens rather than discovering it from a 403 an hour in.
//
// Everything that turns bytes into structure is a public static, so all of it is
// testable against a fixture with no socket.
class GogContentClient : public QObject
{
    Q_OBJECT

public:
    struct Build {
        QString buildId;
        QString legacyBuildId;
        QString link;          // where the build meta lives
        QString versionName;
        QString branch;        // empty = the public branch
        QString os;
        int generation = 0;
        bool isPublic = false;
        QDateTime datePublished;
    };

    struct DepotRef {
        QString manifestHash;
        QString productId;     // base game or a DLC
        QStringList languages; // "*" means shared by every language
        QStringList osBitness;
        qint64 size = 0;
        qint64 compressedSize = 0;
    };

    struct BuildMeta {
        QString baseProductId;
        QString buildId;
        QString installDirectory;
        QString platform;
        QList<DepotRef> depots;
        QStringList dependencies;   // redistributables; skipped, see GogInstallPlan
        bool valid = false;
    };

    struct Chunk {
        QString compressedMd5;   // names the chunk on the CDN, and verifies it
        QString md5;             // verifies it after inflating
        qint64 compressedSize = 0;
        qint64 size = 0;
    };

    struct DepotItem {
        QString path;
        QString type;        // DepotFile | DepotDirectory | DepotLink
        QString md5;         // whole-file, when the server bothered
        QString linkTarget;
        QStringList flags;   // executable, support, ...
        QList<Chunk> chunks;
    };

    struct DepotManifest {
        QString productId;
        QList<DepotItem> items;
        bool valid = false;
    };

    struct SecureEndpoint {
        QString endpointName;
        QString urlFormat;
        QMap<QString, QString> parameters;
        int priority = 0;
        qint64 maxFails = 0;
        bool fallbackOnly = false;   // only try this one once the others fail
    };

    struct SecureLink {
        // Priority order, with fallback-only endpoints last — the downloader
        // rotates through this list on a connection failure.
        QList<SecureEndpoint> endpoints;
        QDateTime fetchedAt;
        QDateTime expiresAt;   // null when the token shape was not recognised
        bool valid = false;
    };

    static GogContentClient& instance();

    // --- pure ---

    // windowBits follows zlib's convention: 15 for the zlib wrapper the content
    // system uses, -15 for the raw deflate inside a ZIP. Returns a null
    // QByteArray on anything that is not a valid stream.
    static QByteArray inflateData(const QByteArray& compressed, int windowBits = 15);

    // cdn.gog.com/content-system/v2/meta/<h[0:2]>/<h[2:4]>/<hash>
    static QString metaUrl(const QString& hash);

    // "abcd1234" -> "ab/cd/abcd1234". How every content-addressed object on the
    // CDN is filed.
    static QString galaxyPath(const QString& hash);

    static QList<Build> parseBuilds(const QByteArray& json);
    static Build newestPublicBuild(const QList<Build>& builds);
    static BuildMeta parseBuildMeta(const QByteArray& inflated);
    static DepotManifest parseDepotManifest(const QByteArray& inflated);
    static SecureLink parseSecureLink(const QByteArray& json, const QDateTime& now);

    // The unix timestamp inside the signed token, if it can be found. Returns a
    // null QDateTime otherwise — and that matters: a wrong guess would suppress
    // the conservative re-sign interval and put us back where gogdl is.
    static QDateTime secureLinkExpiry(const SecureEndpoint& endpoint);

    // url_format with every {key} substituted from parameters, and the chunk's
    // galaxy path appended to the "path" parameter first. Empty when a {key}
    // has no parameter — better to fail here than to send a URL with a literal
    // brace in it and get an opaque 403.
    static QString buildChunkUrl(const SecureEndpoint& endpoint, const QString& compressedMd5);

    // Security-critical: manifests come off the network, and an item path that
    // escapes the install directory must never be written. Returns empty for
    // anything it will not vouch for.
    static QString sanitizeDepotPath(const QString& depotPath);

    // --- async ---

    void fetchBuilds(const QString& productId, const QString& os);
    void fetchBuildMeta(const QString& productId, const QString& metaLink);
    void fetchDepotManifest(const QString& productId, const QString& manifestHash);
    void fetchSecureLink(const QString& productId, const QString& path = QStringLiteral("/"));

signals:
    void buildsReady(const QString& productId, const QList<GogContentClient::Build>& builds);
    void buildsFailed(const QString& productId, const QString& reason);
    void buildMetaReady(const QString& productId, const GogContentClient::BuildMeta& meta);
    void buildMetaFailed(const QString& productId, const QString& reason);
    void depotManifestReady(const QString& productId, const QString& manifestHash,
                            const GogContentClient::DepotManifest& manifest);
    void depotManifestFailed(const QString& productId, const QString& manifestHash,
                             const QString& reason);
    void secureLinkReady(const QString& productId, const GogContentClient::SecureLink& link);
    void secureLinkFailed(const QString& productId, const QString& reason);

private:
    GogContentClient();
    ~GogContentClient() = default;
    GogContentClient(const GogContentClient&) = delete;
    GogContentClient& operator=(const GogContentClient&) = delete;

    QNetworkAccessManager* m_networkManager;
};

Q_DECLARE_METATYPE(GogContentClient::Build)
Q_DECLARE_METATYPE(GogContentClient::BuildMeta)
Q_DECLARE_METATYPE(GogContentClient::DepotManifest)
Q_DECLARE_METATYPE(GogContentClient::SecureLink)

#endif // GOGCONTENTCLIENT_H
