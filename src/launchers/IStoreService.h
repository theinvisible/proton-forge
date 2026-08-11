#ifndef ISTORESERVICE_H
#define ISTORESERVICE_H

#include <QList>
#include <QObject>
#include <QString>
#include <algorithm>
#include <functional>

class QWidget;

// One owned game in a remote library — the store's view, not the disk's.
struct StoreEntry {
    QString id;
    QString title;
    QString imageUrl;
    QString storeUrl;

    // Non-empty means the store cannot install this itself and someone else
    // has to: Steam hands off to its own client with steam://install/<appid>.
    // Empty means we install it.
    QString installUrl;

    bool supportsWindows = false;
    bool supportsLinux = false;

    // False when ProtonForge knows it cannot install this one — a GOG product
    // that only has generation-1 builds, say. Better to say so on the row than
    // to fail after the user clicks.
    bool installable = false;
};

// The order libraryReady() promises. Stated here, next to the signal that carries
// it, and applied by each service before emitting rather than by the code that
// draws a list: there are two consumers — the library dialog and `--store-list` —
// and only one of them is a list widget.
//
// Case-insensitive, matching how SteamLauncher orders the games it discovers, and
// tie-broken by id so a library with two identically named products comes out the
// same way twice. Deliberately not locale- or number-aware: the installed-games
// list is not either, and one list sorting "Fallout 2" before "Fallout 10" while
// the other does the opposite would be worse than both being plain.
inline void sortEntriesByTitle(QList<StoreEntry>& entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const StoreEntry& a, const StoreEntry& b) {
        const int byTitle = QString::compare(a.title, b.title, Qt::CaseInsensitive);
        return byTitle != 0 ? byTitle < 0 : a.id < b.id;
    });
}

// What one title *is*, as opposed to what the library listing says about it.
//
// Fetched per title on selection rather than with the library: both stores answer
// this from a different endpoint than the owned-games list, one product at a time,
// and a library of nine hundred games would be nine hundred requests nobody asked
// for. Disk-cached, because none of it changes hour to hour.
//
// Every field is optional. A store that knows none of this returns valid=false and
// the panel shows what it always showed; a store that knows half of it fills half.
struct StoreEntryDetails {
    QString shortDescription;
    QStringList genres;
    // Canonical names, so the two stores read alike: "Single-player",
    // "Multi-player", "Co-op", "Cloud saves", "Controller support". Steam's
    // categories are mapped onto GOG's vocabulary rather than the other way
    // round, because GOG's is the shorter and less Steam-specific list.
    QStringList features;

    // Text languages, and the subset that is also voiced. Both stores state the
    // difference and it is the one people actually ask about.
    QStringList languages;
    QStringList voiceLanguages;

    // hasAchievements without a count means "yes, but the store did not say how
    // many" — GOG never says.
    bool hasAchievements = false;
    int achievementCount = 0;

    QString releaseDate;          // as the store words it; no reformatting
    QStringList developers;
    QStringList publishers;

    // Unlike either owned-games listing, both detail endpoints do state the
    // platforms. Shown in the panel only — the badges in the list come from
    // StoreEntry and stay as they were, or they would change under the user as
    // each title's details arrive.
    bool supportsWindows = false;
    bool supportsLinux = false;
    bool supportsMac = false;

    bool valid = false;
};

// How far along an install is. Generic on purpose: the library dialog draws a
// bar and a line of text and must not know which store is filling them in.
struct StoreInstallProgress {
    QString detail;               // "Reading 4 depots…", "142 of 1563 files"
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;        // 0 means "no total yet" — show a busy bar
    qint64 bytesPerSecond = 0;
    bool paused = false;
};

// The account side of a launcher: signing in, listing what the user owns,
// installing it.
//
// Kept separate from ILauncher because the two answer different questions.
// ILauncher is about games already on disk and is the only thing the launch
// path needs; IStoreService is about the account behind them and is only ever
// touched by the library dialog. A launcher that just reads what is on disk
// returns null from ILauncher::storeService() and none of this applies.
//
// Deliberately shaped around two stores that work differently. GOG has its own
// login and its own downloader; Steam has neither — it authenticates with an
// API key typed into Settings and installs by asking the Steam client. If a
// third store fits neither, this interface is what should change.
class IStoreService : public QObject
{
    Q_OBJECT

public:
    ~IStoreService() override = default;

    // Must equal the ILauncher::name() of the launcher this belongs to; that is
    // what ties a StoreEntry back to an installed Game.
    virtual QString launcherName() const = 0;
    virtual QString displayName() const = 0;

    // Cached and non-blocking — the dialog asks this while painting.
    virtual bool isAuthenticated() const = 0;

    // False for a store that has no sign-in of its own. The dialog then offers
    // whatever authenticationHint() describes instead of a Sign in button.
    virtual bool canSignIn() const { return true; }

    // Whether this service downloads and installs by itself.
    virtual bool canInstall() const { return false; }

    // What to tell the user when they are not authenticated and cannot sign in
    // here — e.g. "Add your Steam Web API key in Settings → Steam".
    virtual QString authenticationHint() const { return QString(); }

    // Opening a sign-in flow means constructing a dialog, and these services
    // live in protonforge_core — which must not depend on src/ui, because the
    // unit tests link only the former. So the UI installs the handler at
    // startup and the service merely calls it. main.cpp is the one place where
    // a concrete dialog meets a concrete service.
    using SignInHandler = std::function<void(QWidget* parent)>;
    void setSignInHandler(SignInHandler handler) { m_signInHandler = std::move(handler); }

    virtual void beginSignIn(QWidget* parent)
    {
        if (m_signInHandler) {
            m_signInHandler(parent);
        }
    }

    virtual void signOut() {}

    virtual void fetchLibrary() = 0;

    // Per-title metadata for the details panel. False means the panel does not
    // ask, so a store that cannot answer needs no stub that fails.
    virtual bool providesDetails() const { return false; }
    virtual void fetchDetails(const QString& id) { Q_UNUSED(id); }

    virtual void install(const QString& id) { Q_UNUSED(id); }
    virtual void uninstall(const QString& id) { Q_UNUSED(id); }
    // `discard` deletes what has already been downloaded. Without it the
    // partial install stays and a later install resumes from there.
    virtual void cancelInstall(const QString& id, bool discard = false)
    {
        Q_UNUSED(id);
        Q_UNUSED(discard);
    }
    virtual void pauseInstall(const QString& id) { Q_UNUSED(id); }
    virtual void resumeInstall(const QString& id) { Q_UNUSED(id); }

    // Look up whatever an installed game needs to be *drawn* and could not be
    // worked out locally, and record it where discovery can reach it without a
    // network call. Asynchronous; announces itself with
    // installedMetadataChanged().
    //
    // The default does nothing, and for a store whose artwork URL falls out of
    // its own id that is a complete answer rather than a stub — Steam builds
    // its banner URL from the appid and has nothing to ask anyone.
    virtual void refreshInstalledArtwork() {}

    // Whether an install of this id is running or queued right now. The dialog
    // asks while painting a row, so it must answer from memory.
    virtual bool isInstalling(const QString& id) const { Q_UNUSED(id); return false; }

signals:
    void authStateChanged(bool authenticated);
    // Sorted by sortEntriesByTitle() above — Steam's GetOwnedGames answers in
    // appid order, which reads as random on screen.
    void libraryReady(const QList<StoreEntry>& entries);
    void libraryFailed(const QString& reason);

    // Echo the id back first, the ProtonDBClient idiom: several titles can be in
    // flight while the user clicks through the list, and the panel has to know
    // whether an answer is still the one it is waiting for.
    void detailsReady(const QString& id, const StoreEntryDetails& details);
    void detailsFailed(const QString& id, const QString& reason);

    // Forwarded by whatever the service installs with, so the dialog stays
    // store-agnostic rather than connecting to a GOG-specific downloader.
    void installProgress(const QString& id, const StoreInstallProgress& progress);
    void installFinished(const QString& id);
    void installFailed(const QString& id, const QString& reason);

    // Something an *installed* game is shown with changed on disk — emitted
    // once per batch, not per game, because acting on it means rediscovering.
    void installedMetadataChanged();

private:
    SignInHandler m_signInHandler;
};

#endif // ISTORESERVICE_H
