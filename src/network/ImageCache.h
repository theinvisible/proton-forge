#ifndef IMAGECACHE_H
#define IMAGECACHE_H

#include <QObject>
#include <QPixmap>
#include <QNetworkAccessManager>
#include <QMap>
#include <QSet>

class ImageCache : public QObject {
    Q_OBJECT

public:
    static ImageCache& instance();

    // Visible: the list shows the image as loading until the new one is in.
    // Silent: the old image stays on screen until it is replaced — for the
    // background refresh, which should not make the library flicker.
    enum class RefreshMode { Visible, Silent };

    // Get cached image or trigger fetch
    QPixmap getImage(const QString& url, const QSize& size = QSize());

    // Check if image is cached
    bool hasImage(const QString& url) const;

    // Download the image again even though it is cached, and retry one that
    // failed. The cached copy is replaced only once the new one is in: if the
    // download fails, the old image stays and the URL is not marked failed.
    // A download already running for the URL counts as this refresh.
    void refresh(const QString& url, RefreshMode mode = RefreshMode::Visible);

    // True when the cached file is older than maxAgeSecs. An image that is not
    // cached at all is not stale — getImage() fetches it anyway.
    bool isStale(const QString& url, qint64 maxAgeSecs) const;

    // True if a fetch for this URL failed this session (no retry until restart)
    bool hasFailed(const QString& url) const { return m_failedUrls.contains(url); }

    // Clear cache
    void clearCache();

    // Cache directory
    static QString cacheDir();

signals:
    void imageReady(const QString& url);
    void imageFailed(const QString& url);
    // Only for refresh(): started (Visible mode only) and finished, whether or
    // not it succeeded. A failed refresh with a cached copy emits no
    // imageFailed — the copy is still good.
    void refreshStarted(const QString& url);
    void refreshFinished(const QString& url, bool ok);

private:
    ImageCache();
    ~ImageCache() = default;
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    void fetchImage(const QString& url);
    QString cacheFilePath(const QString& url) const;
    QPixmap loadFromDisk(const QString& url) const;
    void saveToDisk(const QString& url, const QByteArray& data);
    QPixmap placeholderImage(const QSize& size) const;

    QNetworkAccessManager* m_networkManager;
    QMap<QString, QPixmap> m_memoryCache;
    QSet<QString> m_pendingRequests;
    QSet<QString> m_failedUrls;
    QSet<QString> m_refreshing;    // pending downloads a refresh() asked for
};

#endif // IMAGECACHE_H
