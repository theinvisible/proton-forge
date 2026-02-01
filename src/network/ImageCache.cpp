#include "ImageCache.h"
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QPainter>

ImageCache& ImageCache::instance()
{
    static ImageCache instance;
    return instance;
}

ImageCache::ImageCache()
    : m_networkManager(new QNetworkAccessManager(this))
{
    // Ensure cache directory exists
    QDir().mkpath(cacheDir());
}

QString ImageCache::cacheDir()
{
    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cachePath + "/NvidiaAppLinux/images";
}

QString ImageCache::cacheFilePath(const QString& url) const
{
    // Use MD5 hash of URL as filename
    QByteArray hash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5);
    return cacheDir() + "/" + hash.toHex() + ".jpg";
}

QPixmap ImageCache::getImage(const QString& url, const QSize& size)
{
    if (url.isEmpty()) {
        return placeholderImage(size);
    }

    // Check memory cache first
    if (m_memoryCache.contains(url)) {
        QPixmap cached = m_memoryCache[url];
        if (!size.isEmpty()) {
            return cached.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return cached;
    }

    // Check disk cache
    QPixmap diskCached = loadFromDisk(url);
    if (!diskCached.isNull()) {
        m_memoryCache[url] = diskCached;
        if (!size.isEmpty()) {
            return diskCached.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return diskCached;
    }

    // Fetch from network
    fetchImage(url);

    return placeholderImage(size);
}

bool ImageCache::hasImage(const QString& url) const
{
    if (m_memoryCache.contains(url)) {
        return true;
    }
    return QFile::exists(cacheFilePath(url));
}

void ImageCache::clearCache()
{
    m_memoryCache.clear();

    QDir dir(cacheDir());
    QStringList files = dir.entryList(QDir::Files);
    for (const QString& file : files) {
        dir.remove(file);
    }
}

QPixmap ImageCache::loadFromDisk(const QString& url) const
{
    QString path = cacheFilePath(url);
    if (QFile::exists(path)) {
        QPixmap pixmap;
        if (pixmap.load(path)) {
            return pixmap;
        }
    }
    return QPixmap();
}

void ImageCache::saveToDisk(const QString& url, const QByteArray& data)
{
    QString path = cacheFilePath(url);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
    }
}

void ImageCache::fetchImage(const QString& url)
{
    if (m_pendingRequests.contains(url)) {
        return;
    }

    m_pendingRequests.insert(url);

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        m_pendingRequests.remove(url);
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit imageFailed(url);
            return;
        }

        QByteArray data = reply->readAll();
        QPixmap pixmap;
        if (pixmap.loadFromData(data)) {
            m_memoryCache[url] = pixmap;
            saveToDisk(url, data);
            emit imageReady(url);
        } else {
            emit imageFailed(url);
        }
    });
}

QPixmap ImageCache::placeholderImage(const QSize& size) const
{
    QSize actualSize = size.isEmpty() ? QSize(460, 215) : size;
    QPixmap placeholder(actualSize);
    placeholder.fill(QColor(40, 40, 40));

    QPainter painter(&placeholder);
    painter.setPen(QColor(100, 100, 100));
    painter.drawRect(0, 0, actualSize.width() - 1, actualSize.height() - 1);

    // Draw game controller icon placeholder
    painter.setPen(QColor(80, 80, 80));
    QFont font = painter.font();
    font.setPixelSize(actualSize.height() / 3);
    painter.setFont(font);
    painter.drawText(placeholder.rect(), Qt::AlignCenter, "🎮");

    return placeholder;
}
