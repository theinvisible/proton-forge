// ImageCache keeps every banner it ever downloaded, which is what made it
// impossible to pick up a store's new artwork — and a download that failed was
// not retried until the next start. refresh() is the way out of both, with one
// promise worth pinning: the picture on screen is only ever replaced by a
// better one, never by nothing. QNetworkAccessManager reads file:// URLs, so
// none of this touches the network.

#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QImage>

#include "network/ImageCache.h"

class TstImageCache : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void cachedImageIsNotFetchedAgain();
    void refreshReplacesTheImage();
    void failedRefreshKeepsTheOldImage();
    void refreshRetriesAFailedImage();
    void secondRefreshJoinsTheFirst();
    void staleness();
    void silentRefreshShowsNoLoading();

private:
    QString writeImage(const QString& name, const QColor& color);
    static QColor colorOf(const QPixmap& pixmap)
    {
        return pixmap.toImage().pixelColor(0, 0);
    }
    QString urlFor(const QString& path) const { return QUrl::fromLocalFile(path).toString(); }

    QTemporaryDir m_dir;
    int m_counter = 0;
};

void TstImageCache::initTestCase()
{
    // Before the singleton exists: its cache directory comes from here.
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_dir.isValid());
}

void TstImageCache::init()
{
    ImageCache::instance().clearCache();
}

void TstImageCache::cleanupTestCase()
{
    // Test mode puts the cache under ~/.qttest/cache/<test>; leave nothing there.
    ImageCache::instance().clearCache();
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
}

QString TstImageCache::writeImage(const QString& name, const QColor& color)
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(color);
    const QString path = m_dir.filePath(name);
    if (!image.save(path, "PNG"))
        return QString();
    return path;
}

void TstImageCache::cachedImageIsNotFetchedAgain()
{
    ImageCache& cache = ImageCache::instance();
    const QString path = writeImage("a.png", Qt::red);
    const QString url = urlFor(path);

    QSignalSpy ready(&cache, &ImageCache::imageReady);
    cache.getImage(url);
    QVERIFY(ready.wait());
    QCOMPARE(colorOf(cache.getImage(url)), QColor(Qt::red));

    // The source changes; without a refresh the cache does not notice.
    writeImage("a.png", Qt::blue);
    QCOMPARE(colorOf(cache.getImage(url)), QColor(Qt::red));
}

void TstImageCache::refreshReplacesTheImage()
{
    ImageCache& cache = ImageCache::instance();
    const QString path = writeImage("b.png", Qt::red);
    const QString url = urlFor(path);

    QSignalSpy ready(&cache, &ImageCache::imageReady);
    cache.getImage(url);
    QVERIFY(ready.wait());

    writeImage("b.png", Qt::blue);
    QSignalSpy started(&cache, &ImageCache::refreshStarted);
    QSignalSpy finished(&cache, &ImageCache::refreshFinished);
    cache.refresh(url);
    QCOMPARE(started.count(), 1);
    QVERIFY(finished.wait());
    QCOMPARE(finished.first().at(0).toString(), url);
    QCOMPARE(finished.first().at(1).toBool(), true);
    QCOMPARE(colorOf(cache.getImage(url)), QColor(Qt::blue));
}

void TstImageCache::failedRefreshKeepsTheOldImage()
{
    ImageCache& cache = ImageCache::instance();
    const QString path = writeImage("c.png", Qt::green);
    const QString url = urlFor(path);

    QSignalSpy ready(&cache, &ImageCache::imageReady);
    cache.getImage(url);
    QVERIFY(ready.wait());

    QFile::remove(path);
    QSignalSpy failed(&cache, &ImageCache::imageFailed);
    QSignalSpy finished(&cache, &ImageCache::refreshFinished);
    cache.refresh(url);
    QVERIFY(finished.wait());
    QCOMPARE(finished.first().at(1).toBool(), false);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!cache.hasFailed(url));
    QVERIFY(cache.hasImage(url));
    QCOMPARE(colorOf(cache.getImage(url)), QColor(Qt::green));
}

void TstImageCache::refreshRetriesAFailedImage()
{
    ImageCache& cache = ImageCache::instance();
    const QString path = m_dir.filePath("d.png");
    const QString url = urlFor(path);

    QSignalSpy failed(&cache, &ImageCache::imageFailed);
    cache.getImage(url);
    QVERIFY(failed.wait());
    QVERIFY(cache.hasFailed(url));

    // getImage alone does not try again this session; refresh does.
    writeImage("d.png", Qt::yellow);
    cache.getImage(url);
    QVERIFY(cache.hasFailed(url));

    QSignalSpy finished(&cache, &ImageCache::refreshFinished);
    cache.refresh(url);
    QVERIFY(!cache.hasFailed(url));
    QVERIFY(finished.wait());
    QCOMPARE(finished.first().at(1).toBool(), true);
    QCOMPARE(colorOf(cache.getImage(url)), QColor(Qt::yellow));
}

void TstImageCache::secondRefreshJoinsTheFirst()
{
    ImageCache& cache = ImageCache::instance();
    const QString url = urlFor(writeImage("e.png", Qt::cyan));

    QSignalSpy ready(&cache, &ImageCache::imageReady);
    QSignalSpy finished(&cache, &ImageCache::refreshFinished);
    cache.refresh(url);
    cache.refresh(url);
    QVERIFY(finished.wait());
    QTest::qWait(100);   // a second download would have finished by now
    QCOMPARE(finished.count(), 1);
    QCOMPARE(ready.count(), 1);
}

void TstImageCache::staleness()
{
    ImageCache& cache = ImageCache::instance();
    const QString url = urlFor(writeImage("f.png", Qt::magenta));
    QVERIFY(!cache.isStale(url, 24 * 3600));   // nothing cached yet

    QSignalSpy ready(&cache, &ImageCache::imageReady);
    cache.getImage(url);
    QVERIFY(ready.wait());
    QVERIFY(!cache.isStale(url, 24 * 3600));   // just downloaded

    // The cache file is named after the URL; age the one that is there.
    QDir dir(ImageCache::cacheDir());
    const QStringList files = dir.entryList(QDir::Files);
    QCOMPARE(files.size(), 1);
    QFile file(dir.filePath(files.first()));
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-25 * 3600),
                             QFileDevice::FileModificationTime));
    file.close();
    QVERIFY(cache.isStale(url, 24 * 3600));

    // A refresh writes a new file, and the clock starts again.
    QSignalSpy finished(&cache, &ImageCache::refreshFinished);
    cache.refresh(url, ImageCache::RefreshMode::Silent);
    QVERIFY(finished.wait());
    QVERIFY(!cache.isStale(url, 24 * 3600));
}

void TstImageCache::silentRefreshShowsNoLoading()
{
    ImageCache& cache = ImageCache::instance();
    const QString url = urlFor(writeImage("g.png", Qt::gray));

    QSignalSpy started(&cache, &ImageCache::refreshStarted);
    QSignalSpy ready(&cache, &ImageCache::imageReady);
    QSignalSpy finished(&cache, &ImageCache::refreshFinished);
    cache.refresh(url, ImageCache::RefreshMode::Silent);
    QVERIFY(finished.wait());
    QCOMPARE(started.count(), 0);
    QCOMPARE(ready.count(), 1);
}

QTEST_MAIN(TstImageCache)
#include "tst_imagecache.moc"
