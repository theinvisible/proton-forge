#ifndef GOGOFFLINECLIENT_H
#define GOGOFFLINECLIENT_H

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QFile;
class QNetworkReply;

// GOG's offline installers — the other way a game is distributed.
//
// Most Linux builds never reach the Galaxy content system at all: GOG ships
// them as self-extracting `.sh` installers on the account page instead. So a
// product that says it supports Linux usually has no generation-2 Linux build,
// and the choice is this or the Windows build under Proton.
//
// Two things separate this from the content-system path. The download is a
// single multi-gigabyte file rather than chunks, so it is streamed straight to
// disk — buffering it the way ProtonManager buffers a Proton release would mean
// holding 20 GB in memory. And the URL is behind a redirect that only resolves
// with a bearer token, so the request has to be authenticated even though the
// file it lands on is not.
class GogOfflineClient : public QObject
{
    Q_OBJECT

public:
    struct Installer {
        QString id;
        QString name;
        QString os;           // "linux" | "windows" | "mac"
        QString language;     // "English", as GOG writes it
        QString version;
        QString manualUrl;    // relative to embed.gog.com; needs a token to resolve
        qint64 size = 0;      // GOG reports this as human text; parsed, 0 when unclear
    };

    static GogOfflineClient& instance();

    // --- pure ---

    // `downloads` in gameDetails is a nested array-of-arrays keyed by language,
    // which is why this is not a two-line parse.
    static QList<Installer> parseGameDetails(const QByteArray& json);

    // Picks the installer to use: the wanted language if it is on offer, else
    // English, else the first. Returns an Installer with an empty manualUrl
    // when there is nothing for that platform.
    static Installer selectInstaller(const QList<Installer>& installers,
                                     const QString& os,
                                     const QString& language = QStringLiteral("English"));

    // "3.4 GB" / "512 MB" / "" — GOG gives sizes as display text. Returns 0 for
    // anything it does not recognise rather than a guess, so a preflight check
    // can tell "too big" from "unknown".
    static qint64 parseSize(const QString& text);

    static QString downloadUrl(const QString& manualUrl);

    // --- async ---

    void fetchInstallers(const QString& productId);

    // Streams to `destPath`, resuming from whatever is already there when the
    // server allows it. Emits downloadProgress throttled to 100 ms.
    void download(const QString& productId, const Installer& installer, const QString& destPath);
    void cancel();

signals:
    void installersReady(const QString& productId, const QList<GogOfflineClient::Installer>& list);
    void installersFailed(const QString& productId, const QString& reason);

    void downloadProgress(const QString& productId, qint64 received, qint64 total);
    void downloadFinished(const QString& productId, const QString& path);
    void downloadFailed(const QString& productId, const QString& reason);

private:
    GogOfflineClient();
    ~GogOfflineClient() override = default;
    GogOfflineClient(const GogOfflineClient&) = delete;
    GogOfflineClient& operator=(const GogOfflineClient&) = delete;

    void startTransfer(const QString& url, const QString& token);
    void abortDownload();

    QNetworkAccessManager* m_networkManager;
    QNetworkReply* m_reply = nullptr;
    QFile* m_sink = nullptr;
    QString m_productId;
    QString m_destPath;
    qint64 m_resumeFrom = 0;
    bool m_rangeHonoured = true;
};

Q_DECLARE_METATYPE(GogOfflineClient::Installer)

#endif // GOGOFFLINECLIENT_H
