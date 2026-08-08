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

private:
    static StoreEntry toEntry(const GogApiClient::Product& product);

    QHash<QString, QString> m_titles;   // product id -> title, for install requests

    // Products whose newest build we asked for and have not heard back about.
    // A plain set with connections made once in the constructor, rather than a
    // per-batch counter and a connection torn down when it reaches zero: that
    // shape leaks its connection whenever a lookup fails instead of answering,
    // and then fires on every unrelated build query for the rest of the run.
    QSet<QString> m_awaitingBuilds;
};

#endif // GOGSTORESERVICE_H
