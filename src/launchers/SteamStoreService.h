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

    // The storefront's appdetails endpoint, which needs no Web API key — so the
    // details panel works even before one is entered, and a signed-out user still
    // gets the description for a game the list happens to show.
    bool providesDetails() const override { return true; }
    void fetchDetails(const QString& id) override;

    // --- pure ---

    static QList<StoreEntry> parseOwnedGames(const QByteArray& json, int* gameCount);

    // One appdetails response, which wraps its payload in the appid it was asked
    // about and reports failure inside a 200 body — an unowned or delisted appid
    // answers {"<id>": {"success": false}}.
    static StoreEntryDetails parseAppDetails(const QByteArray& json, const QString& appId);

    // "English<strong>*</strong>, French, …<br><strong>*</strong>languages with
    // full audio support" — Steam ships this as display HTML, asterisk and
    // trailing footnote included, and it is the only place the audio languages
    // are stated. `voiced` receives the starred subset.
    static QStringList parseSupportedLanguages(const QString& html, QStringList* voiced);

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
