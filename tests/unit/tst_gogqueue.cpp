// What the downloader tells the world, and when.
//
// Everything watching an install reacts to installFinished/installFailed by
// asking what is installing *now* — the store dialog rebuilds its rows from
// isInstalling(), which reads the downloader's queue. So the queue has to be
// right before the announcement goes out, not after it. Emitting first and
// tearing the job down afterwards leaves the finished game wearing its
// "installing" badge and its button still saying "Cancel", with nothing left to
// come along later and correct it.
//
// The same ordering decides when the *next* queued job starts: begun inside the
// teardown, its own progress — and its own failure, if it fails while resolving
// — would reach the listener ahead of the news about the job that just ended.
//
// And a job that never started still has to be announced when it is cancelled.
// queueChanged says the queue moved, not which game left it, so it is not
// something a row can act on.
//
// No network. Both content-system lookups are pre-seeded into the disk cache
// with a generation-1-only answer, which is a real GOG shape and one ProtonForge
// cannot install; with no session, the native-installer detour that sits between
// them fails at the token and never reaches a socket either.

#include <QCoreApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "gog/GogDownloader.h"
#include "gog/GogStoreService.h"
#include "network/JsonDiskCache.h"

class TstGogQueue : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void isNoLongerInstallingWhenItSaysItFailed();
    void keepsWhatIsStillQueuedBehindIt();
    void announcesInTheOrderTheJobsRan();
    void announcesACancellationItNeverStarted();

private:
    // A build listing with nothing generation-2 in it, which is what makes the
    // whole exchange resolvable from cache and terminal.
    void seedNoBuildsFor(const QString& productId);

    // The other kind of terminal answer, and the one that arrives *without*
    // going through the event loop: a real generation-2 build whose metadata is
    // cached too, and lists no depots.
    void seedDepotlessBuildFor(const QString& productId);

    QTemporaryDir m_home;
};

void TstGogQueue::initTestCase()
{
    QVERIFY(m_home.isValid());
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/.config").toUtf8());
    qputenv("XDG_CACHE_HOME", (m_home.path() + "/.cache").toUtf8());
    // Never the developer's real keyring: no session must be restorable here,
    // or the offline-installer detour would go looking for one on the network.
    qputenv("PROTONFORGE_SECRET_STORE", "file");
    QStandardPaths::setTestModeEnabled(true);

    QCoreApplication::setOrganizationName("ProtonForgeTest");
    QCoreApplication::setApplicationName("ProtonForgeTest");
}

void TstGogQueue::seedNoBuildsFor(const QString& productId)
{
    const QByteArray body = QStringLiteral(R"({
      "total_count": 1,
      "count": 1,
      "items": [
        {
          "build_id": "12000000000000000",
          "product_id": "%1",
          "os": "windows",
          "branch": null,
          "version_name": "1.0",
          "public": true,
          "date_published": "2015-05-19T13:32:22+0000",
          "generation": 1,
          "link": "https://cdn.gog.com/content-system/v1/manifests/%1/windows/12/repository.json"
        }
      ]
    })").arg(productId).toUtf8();

    // Linux is tried first and Windows after the native detour gives up, so both
    // have to be there or the second one reaches for a socket.
    for (const QString& os : {QStringLiteral("linux"), QStringLiteral("windows")}) {
        JsonDiskCache::save(
            JsonDiskCache::filePath(QStringLiteral("gog"),
                                    QStringLiteral("builds-%1-%2").arg(productId, os)),
            body);
    }
}

void TstGogQueue::seedDepotlessBuildFor(const QString& productId)
{
    const QString hash = QStringLiteral("00000000000000000000000000000%1").arg(productId.right(3));
    const QString link =
        QStringLiteral("https://cdn.gog.com/content-system/v2/meta/00/00/%1").arg(hash);

    const QByteArray builds = QStringLiteral(R"({
      "items": [
        {
          "build_id": "48000000000000001",
          "product_id": "%1",
          "os": "linux",
          "branch": null,
          "version_name": "1.0",
          "public": true,
          "date_published": "2021-05-19T13:32:22+0000",
          "generation": 2,
          "link": "%2"
        }
      ]
    })").arg(productId, link).toUtf8();

    JsonDiskCache::save(
        JsonDiskCache::filePath(QStringLiteral("gog"),
                                QStringLiteral("builds-%1-linux").arg(productId)),
        builds);

    // Cached metadata is stored inflated, so this is the form the parser reads.
    const QByteArray meta = QStringLiteral(R"({
      "baseProductId": "%1",
      "buildId": "48000000000000001",
      "installDirectory": "Test Game",
      "platform": "linux",
      "depots": []
    })").arg(productId).toUtf8();

    JsonDiskCache::save(
        JsonDiskCache::filePath(QStringLiteral("gog"), QStringLiteral("meta-") + hash), meta);
}

void TstGogQueue::isNoLongerInstallingWhenItSaysItFailed()
{
    const QString id = QStringLiteral("1207658930");
    seedNoBuildsFor(id);

    GogDownloader& downloader = GogDownloader::instance();
    GogStoreService service;   // the predicate the badge actually asks

    bool saidInstalling = true;
    bool saidActive = true;
    bool saidBusy = true;
    // Scoped to this slot: the downloader is a process-wide singleton, so a
    // connection made against the test object itself would outlive the stack it
    // captures and fire again during the next case.
    QObject context;
    connect(&downloader, &GogDownloader::installFailed, &context,
            [&](const QString& failedId, const QString&) {
        if (failedId != id) {
            return;
        }
        saidInstalling = service.isInstalling(id);
        saidActive = downloader.isActive(id);
        saidBusy = downloader.isBusy();
    });

    QSignalSpy failed(&downloader, &GogDownloader::installFailed);
    GogDownloader::Request request;
    request.productId = id;
    downloader.enqueue(request);

    QVERIFY2(failed.wait(10000), "the install never came to an end");
    QCOMPARE(failed.first().at(0).toString(), id);

    // The three ways the same question gets asked, all answered while the
    // failure was being announced rather than afterwards.
    QVERIFY(!saidInstalling);
    QVERIFY(!saidActive);
    QVERIFY(!saidBusy);
}

void TstGogQueue::keepsWhatIsStillQueuedBehindIt()
{
    const QString first = QStringLiteral("1207658931");
    const QString second = QStringLiteral("1207658932");
    seedNoBuildsFor(first);
    seedNoBuildsFor(second);

    GogDownloader& downloader = GogDownloader::instance();
    GogStoreService service;

    // Read while the *first* job is being announced: it is off the queue, and
    // the one behind it is untouched. Asserting only "nothing is installing"
    // would pass just as well on an implementation that dropped the rest of the
    // queue along with the job that ended.
    bool firstStillInstalling = true;
    bool secondStillQueued = false;
    QObject context;
    connect(&downloader, &GogDownloader::installFailed, &context,
            [&](const QString& failedId, const QString&) {
        if (failedId != first) {
            return;
        }
        firstStillInstalling = service.isInstalling(first);
        secondStillQueued = service.isInstalling(second);
    });

    QSignalSpy failed(&downloader, &GogDownloader::installFailed);
    for (const QString& id : {first, second}) {
        GogDownloader::Request request;
        request.productId = id;
        downloader.enqueue(request);
    }

    QTRY_VERIFY_WITH_TIMEOUT(failed.size() == 2, 15000);

    QVERIFY(!firstStillInstalling);
    QVERIFY(secondStillQueued);
}

void TstGogQueue::announcesInTheOrderTheJobsRan()
{
    const QString first = QStringLiteral("1207658933");
    const QString second = QStringLiteral("1207658934");
    // The first has to go round the event loop to reach its answer (the native
    // detour asks for a token); the second reaches its answer without ever
    // yielding. Started inside the first one's teardown, the second would
    // therefore finish — and be announced — before the first one was.
    seedNoBuildsFor(first);
    seedDepotlessBuildFor(second);

    GogDownloader& downloader = GogDownloader::instance();

    QSignalSpy failed(&downloader, &GogDownloader::installFailed);
    for (const QString& id : {first, second}) {
        GogDownloader::Request request;
        request.productId = id;
        downloader.enqueue(request);
    }

    QTRY_VERIFY_WITH_TIMEOUT(failed.size() == 2, 15000);

    QCOMPARE(failed.at(0).at(0).toString(), first);
    QCOMPARE(failed.at(1).at(0).toString(), second);
}

void TstGogQueue::announcesACancellationItNeverStarted()
{
    const QString running = QStringLiteral("1207658935");
    const QString queued = QStringLiteral("1207658936");
    seedNoBuildsFor(running);
    seedNoBuildsFor(queued);

    GogDownloader& downloader = GogDownloader::instance();
    GogStoreService service;

    QSignalSpy failed(&downloader, &GogDownloader::installFailed);
    for (const QString& id : {running, queued}) {
        GogDownloader::Request request;
        request.productId = id;
        downloader.enqueue(request);
    }

    // The second one is behind the first and has not started. Dropping it from
    // the queue is all there is to do — but the store dialog painted it as
    // installing, and only these signals ever move a row off that badge.
    QVERIFY(service.isInstalling(queued));
    downloader.cancel(queued);

    QVERIFY(!service.isInstalling(queued));
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first().at(0).toString(), queued);

    // And the one that was actually running is untouched by it.
    QVERIFY(service.isInstalling(running));
    QTRY_VERIFY_WITH_TIMEOUT(failed.size() == 2, 10000);
    QCOMPARE(failed.at(1).at(0).toString(), running);
}

QTEST_MAIN(TstGogQueue)
#include "tst_gogqueue.moc"
