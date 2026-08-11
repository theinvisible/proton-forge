#ifndef GOGAUTH_H
#define GOGAUTH_H

#include <QDateTime>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

// GOG's OAuth2, as the Galaxy client speaks it.
//
// There is no localhost redirect to be had: GOG validates redirect_uri against
// what is registered for the client id, and re-validates it when the code is
// exchanged. So the sign-in flow is "open GOG's page in the real browser, paste
// the address back" — see GogLoginDialog. Everything that parses what the user
// pasted is a pure static here, because that is the part with edge cases.
//
// Only the refresh token is persisted (in SecretStore). The access token lives
// an hour and never leaves memory.
class GogAuth : public QObject
{
    Q_OBJECT

public:
    struct Tokens {
        QString accessToken;
        QString refreshToken;
        QString userId;
        QString sessionId;
        QDateTime expiresAt;
        bool valid = false;
    };

    static GogAuth& instance();

    // --- pure, and where all the awkward cases live ---

    static QString authorizationUrl();

    // Pull the authorization code out of whatever the user pasted: the whole
    // redirect URL, a URL with the code in the fragment, or a bare code. Empty
    // when there is none — including when GOG reported an error instead.
    static QString extractAuthCode(const QString& pasted);

    // The human-readable reason GOG refused, if it refused. Empty otherwise.
    static QString extractAuthError(const QString& pasted);

    static Tokens parseTokenResponse(const QByteArray& json, const QDateTime& now);

    // Refreshed early on purpose. Waiting for the exact expiry means trusting
    // two clocks to agree, and they do not.
    static bool needsRefresh(const Tokens& tokens, const QDateTime& now, int skewSeconds = 300);

    // --- session ---

    void restoreSession();
    void loginWithCode(const QString& code);
    void logout();

    bool isLoggedIn() const { return m_tokens.valid; }
    QString userId() const { return m_tokens.userId; }

    // Hand back a usable access token, refreshing first if it is close to
    // expiring. Concurrent callers coalesce onto one refresh: GOG invalidates a
    // refresh token the moment it is used, so two refreshes in flight means one
    // of them loses the session permanently and the user is signed out for no
    // visible reason.
    quint64 requestToken();

    // Called by GogRequest when a request came back 401 — the access token died
    // earlier than its expiry claimed, so stop trusting the clock.
    void invalidateAccessToken();

signals:
    void tokenReady(quint64 requestId, const QString& accessToken);
    void tokenFailed(quint64 requestId, const QString& reason);

    void loginSucceeded(const QString& userId);
    void loginFailed(const QString& reason);
    void loggedOut();
    void sessionRestored(bool loggedIn);

    // The refresh token itself was rejected. Nothing to do but sign in again.
    void reauthenticationRequired();

private:
    GogAuth();
    ~GogAuth() = default;
    GogAuth(const GogAuth&) = delete;
    GogAuth& operator=(const GogAuth&) = delete;

    void startRefresh();
    void settleWaiters(bool ok, const QString& reason);
    void storeTokens(const Tokens& tokens);

    QNetworkAccessManager* m_networkManager;
    Tokens m_tokens;
    bool m_refreshInFlight = false;
    quint64 m_nextRequestId = 1;
    QList<quint64> m_waiting;
};

#endif // GOGAUTH_H
