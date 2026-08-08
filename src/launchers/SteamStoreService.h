#ifndef STEAMSTORESERVICE_H
#define STEAMSTORESERVICE_H

#include <QNetworkAccessManager>
#include <QString>

#include "IStoreService.h"

// Steam's owned library, via the Web API.
//
// This exists to show something ProtonForge cannot see today: the games you own
// but have *not* installed. SteamLauncher::discoverGames() reads
// appmanifest_*.acf files, and those only exist once a game is installed.
//
// It is also the second implementation of IStoreService, deliberately built
// alongside the first. Steam is the awkward case — no sign-in of its own (an API
// key typed into Settings), no downloader of its own (it hands off to the Steam
// client with steam://install/<appid>) — so if the interface only fits GOG, this
// is where that shows.
class SteamStoreService : public IStoreService
{
    Q_OBJECT

public:
    SteamStoreService();

    QString launcherName() const override { return QStringLiteral("Steam"); }
    QString displayName() const override { return QStringLiteral("Steam"); }

    bool isAuthenticated() const override;
    bool canSignIn() const override { return false; }
    bool canInstall() const override { return false; }
    QString authenticationHint() const override;

    void fetchLibrary() override;

    // --- pure ---

    static QList<StoreEntry> parseOwnedGames(const QByteArray& json, int* gameCount);

    // The SteamID64 of whichever account Steam last used, read from
    // config/loginusers.vdf. Asking the user to find their own 64-bit id is a
    // poor first impression when the answer is already on disk.
    static QString steamIdFromLoginUsers(const QByteArray& vdf);

    static QString headerImageUrl(const QString& appId);

    // The stored id, or the one detected from the local Steam install.
    static QString resolveSteamId();

private:
    QNetworkAccessManager* m_networkManager;
};

#endif // STEAMSTORESERVICE_H
