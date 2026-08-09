#ifndef GOGSTORESERVICE_H
#define GOGSTORESERVICE_H

#include <QHash>
#include <QSet>

#include "gog/GogApiClient.h"
#include "launchers/IStoreService.h"

// The GOG account, behind IStoreService.
//
// A thin adapter on purpose: GogAuth, GogApiClient and GogDownloader stay
// GOG-shaped and GOG-named, and this is the only class the generic library
// dialog ever sees. Nothing above this line knows the word "GOG".
class GogStoreService : public IStoreService
{
    Q_OBJECT

public:
    GogStoreService();

    QString launcherName() const override { return QStringLiteral("GOG"); }
    QString displayName() const override { return QStringLiteral("GOG"); }

    bool isAuthenticated() const override;
    bool canSignIn() const override { return true; }
    bool canInstall() const override { return true; }

    void signOut() override;
    void fetchLibrary() override;

    void install(const QString& id) override;
    void uninstall(const QString& id) override;
    void cancelInstall(const QString& id, bool discard) override;
    void pauseInstall(const QString& id) override;
    void resumeInstall(const QString& id) override;
    bool isInstalling(const QString& id) const override;

    // Ask the content system for the newest build of everything installed, and
    // record it so GogLauncher can answer "update available" without a network
    // call. Called when the library dialog opens; results are disk-cached, so
    // repeating it is cheap.
    void refreshUpdateState();

    // Look up the banner for anything installed that has none recorded, and
    // write it into the registry so discovery can hand it to the game list
    // without a network call. Covers installs made before artwork was
    // recorded, and installs made from the CLI, which has no store entry to
    // take one from. Catalogue data, so it works signed out.
    void refreshInstalledArtwork() override;

private:
    static StoreEntry toEntry(const GogApiClient::Product& product);

    // Fill in missing banners from a library listing that just arrived. Free —
    // that response already carries one per owned game.
    void adoptArtworkFromLibrary();

    // One place to decide whether the news is worth a repaint, so the two
    // sources of artwork announce it identically.
    void recordArtwork(const QString& productId, const QString& imageUrl);
    void finishArtworkLookup(const QString& productId);

    QHash<QString, QString> m_titles;   // product id -> title, for install requests
    QHash<QString, QString> m_images;   // product id -> banner, likewise

    // Products whose newest build we asked for and have not heard back about.
    // A plain set with connections made once in the constructor, rather than a
    // per-batch counter and a connection torn down when it reaches zero: that
    // shape leaks its connection whenever a lookup fails instead of answering,
    // and then fires on every unrelated build query for the rest of the run.
    QSet<QString> m_awaitingBuilds;

    // The same shape, and for the same reason, for the artwork lookups.
    QSet<QString> m_awaitingProducts;
    bool m_artworkChanged = false;   // did any lookup actually write something
};

#endif // GOGSTORESERVICE_H
