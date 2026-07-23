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

    // Get cached image or trigger fetch
    QPixmap getImage(const QString& url, const QSize& size = QSize());

    // Check if image is cached
    bool hasImage(const QString& url) const;

    // True if a fetch for this URL failed this session (no retry until restart)
    bool hasFailed(const QString& url) const { return m_failedUrls.contains(url); }

    // Clear cache
    void clearCache();

    // Cache directory
    static QString cacheDir();

signals:
    void imageReady(const QString& url);
    void imageFailed(const QString& url);

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
};

#endif // IMAGECACHE_H
