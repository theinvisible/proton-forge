#ifndef GOGAPICLIENT_H
#define GOGAPICLIENT_H

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

// GOG's account and catalogue endpoints — what the user owns, and what each
// product is.
//
// Everything that turns a response into data is a public static, so the tests
// can drive it without a socket. The async half is the ProtonDBClient idiom:
// paired ready/failed signals that echo the key back first.
class GogApiClient : public QObject
{
    Q_OBJECT

public:
    struct Product {
        QString id;
        QString title;
        QString slug;
        QString imageUrl;
        bool supportsWindows = false;
        bool supportsLinux = false;
        bool supportsMac = false;
    };

    struct ProductDetail {
        QString id;
        QString title;
        QString slug;
        QString imageUrl;
        // Whether the Galaxy content system serves this platform at all. Linux
        // is true for only a minority of products even when the store page
        // offers a Linux build, because those ship as .sh installers instead.
        bool contentSystemWindows = false;
        bool contentSystemLinux = false;
        QStringList dlcIds;
        bool valid = false;
    };

    static GogApiClient& instance();

    // --- pure ---

    static QStringList parseOwnedIds(const QByteArray& json);
    // `totalPages` receives what the server reported, so the caller knows
    // whether to ask for more.
    static QList<Product> parseFilteredProducts(const QByteArray& json, int* totalPages);
    static ProductDetail parseProduct(const QByteArray& json);

    // GOG serves artwork protocol-relative ("//images.gog-statics.com/<hash>")
    // and without a size suffix. Both have to be repaired before the URL is
    // usable, and an empty or already-absolute one must survive untouched.
    static QString normalizeImageUrl(const QString& raw, const QString& variant);

    static QString storeUrl(const QString& slug);

    // --- async ---

    void fetchLibrary();
    void fetchProduct(const QString& productId);
    void clearCache();

signals:
    void libraryReady(const QList<GogApiClient::Product>& products);
    void libraryFailed(const QString& reason);
    void productReady(const QString& productId, const GogApiClient::ProductDetail& detail);
    void productFailed(const QString& productId, const QString& reason);

private:
    GogApiClient();
    ~GogApiClient() = default;
    GogApiClient(const GogApiClient&) = delete;
    GogApiClient& operator=(const GogApiClient&) = delete;

    void fetchLibraryPage(int page, QList<Product> collected);

    QNetworkAccessManager* m_networkManager;
};

Q_DECLARE_METATYPE(GogApiClient::Product)
Q_DECLARE_METATYPE(GogApiClient::ProductDetail)

#endif // GOGAPICLIENT_H
