#include "GogAuth.h"
#include "core/SecretStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

// The GOG Galaxy client's own OAuth credentials. They are not a secret in any
// meaningful sense — every third-party GOG client (Heroic, Lutris, minigalaxy,
// lgogdownloader) ships these same constants, because GOG issues no others and
// there is no GOG client for Linux. Kept in one place so that if GOG ever
// rotates them, there is exactly one line to change.
const QString kClientId     = QStringLiteral("46899977096215655");
const QString kClientSecret = QStringLiteral("9d85c43b1482497dbbce61f6e4aa173a433796eeae2ca8c5f6129f2dc4de46d9");

// Must match byte for byte between the /auth request and the /token exchange —
// GOG checks it twice.
const QString kRedirectUri = QStringLiteral("https://embed.gog.com/on_login_success?origin=client");

const QString kAuthUrl  = QStringLiteral("https://auth.gog.com/auth");
const QString kTokenUrl = QStringLiteral("https://auth.gog.com/token");

QString firstNonEmpty(const QUrlQuery& query, const QString& key)
{
    return query.hasQueryItem(key) ? query.queryItemValue(key, QUrl::FullyDecoded) : QString();
}

// What the user pastes has usually been through a browser address bar and a
// clipboard, and sometimes a chat client or a terminal that wrapped it. Undo
// that before trying to read it as anything.
QString tidyPaste(const QString& pasted)
{
    QString text = pasted.trimmed();

    while (text.size() >= 2
           && ((text.startsWith('"') && text.endsWith('"'))
               || (text.startsWith('\'') && text.endsWith('\''))
               || (text.startsWith('<') && text.endsWith('>')))) {
        text = text.mid(1, text.size() - 2).trimmed();
    }

    // A URL cannot contain raw whitespace, so any that is left came from the
    // paste itself — a terminal that hard-wrapped it, most often.
    text.remove(QRegularExpression(QStringLiteral("\\s")));
    return text;
}

} // namespace

GogAuth& GogAuth::instance()
{
    static GogAuth instance;
    return instance;
}

GogAuth::GogAuth()
    : m_networkManager(new QNetworkAccessManager(this))
{
}

// --- pure --------------------------------------------------------------------

QString GogAuth::authorizationUrl()
{
    // Assembled by hand rather than through QUrlQuery. Qt leaves ':', '/' and
    // '?' unencoded inside a query value — they are legal there — which yields a
    // redirect_uri carrying a bare '?'. A correct parser survives it, but an
    // OAuth endpoint compares redirect_uri as a string between the /auth call
    // and the /token exchange, and handing it something that needs interpreting
    // first is not worth the convenience.
    const QString redirect = QString::fromUtf8(QUrl::toPercentEncoding(kRedirectUri));
    return QStringLiteral("%1?client_id=%2&redirect_uri=%3&response_type=code&layout=client2")
        .arg(kAuthUrl, kClientId, redirect);
}

QString GogAuth::extractAuthCode(const QString& pasted)
{
    const QString text = tidyPaste(pasted);
    if (text.isEmpty()) {
        return QString();
    }

    const QUrl url(text);
    if (url.isValid() && !url.scheme().isEmpty()) {
        const QString fromQuery = firstNonEmpty(QUrlQuery(url.query()), QStringLiteral("code"));
        if (!fromQuery.isEmpty()) {
            return fromQuery;
        }
        // Some flows put the response in the fragment instead.
        const QString fromFragment =
            firstNonEmpty(QUrlQuery(url.fragment()), QStringLiteral("code"));
        if (!fromFragment.isEmpty()) {
            return fromFragment;
        }
        // A URL that carries an error and no code is a refusal, not a code.
        if (!extractAuthError(pasted).isEmpty()) {
            return QString();
        }
    }

    // Not a URL we could parse, but it may still contain the parameter.
    const int marker = text.indexOf(QStringLiteral("code="));
    if (marker >= 0) {
        QString rest = text.mid(marker + 5);
        const int end = rest.indexOf('&');
        if (end >= 0) {
            rest = rest.left(end);
        }
        // Exactly once: a code may legitimately contain a percent sign, and
        // decoding twice would eat it.
        return QUrl::fromPercentEncoding(rest.toUtf8());
    }

    // People sometimes copy just the code rather than the whole address, so
    // accept that — but strictly. Whitespace has already been stripped by now,
    // which means a sentence like "it just says login failed" arrives here as
    // one long word and would otherwise pass as a code. Sending prose to GOG
    // earns a confusing server error; refusing it earns "paste the full
    // address", which the user can act on.
    static const QRegularExpression bareCode(QStringLiteral("^[A-Za-z0-9_.\\-]{20,}$"));
    static const QRegularExpression hasDigit(QStringLiteral("[0-9]"));
    if (bareCode.match(text).hasMatch() && hasDigit.match(text).hasMatch()) {
        return text;
    }

    return QString();
}

QString GogAuth::extractAuthError(const QString& pasted)
{
    const QString text = tidyPaste(pasted);
    if (text.isEmpty()) {
        return QString();
    }

    const QUrl url(text);
    QUrlQuery query(url.isValid() && !url.scheme().isEmpty() ? url.query() : text);

    const QString error = firstNonEmpty(query, QStringLiteral("error"));
    if (error.isEmpty()) {
        return QString();
    }
    const QString description = firstNonEmpty(query, QStringLiteral("error_description"));
    return description.isEmpty() ? error : description;
}

GogAuth::Tokens GogAuth::parseTokenResponse(const QByteArray& json, const QDateTime& now)
{
    Tokens tokens;

    const QJsonObject root = QJsonDocument::fromJson(json).object();
    tokens.accessToken  = root.value(QStringLiteral("access_token")).toString();
    tokens.refreshToken = root.value(QStringLiteral("refresh_token")).toString();
    tokens.userId       = root.value(QStringLiteral("user_id")).toVariant().toString();
    tokens.sessionId    = root.value(QStringLiteral("session_id")).toVariant().toString();

    const QJsonValue expiresIn = root.value(QStringLiteral("expires_in"));
    const int seconds = expiresIn.isUndefined() ? 3600 : expiresIn.toVariant().toInt();
    tokens.expiresAt = now.addSecs(seconds > 0 ? seconds : 3600);

    // Both, not either: without a refresh token there is nothing worth keeping,
    // because the access token is gone in an hour and cannot be renewed.
    tokens.valid = !tokens.accessToken.isEmpty() && !tokens.refreshToken.isEmpty();
    return tokens;
}

bool GogAuth::needsRefresh(const Tokens& tokens, const QDateTime& now, int skewSeconds)
{
    if (!tokens.valid || !tokens.expiresAt.isValid()) {
        return true;
    }
    return now.addSecs(skewSeconds) >= tokens.expiresAt;
}

// --- session -----------------------------------------------------------------

void GogAuth::restoreSession()
{
    const QString refreshToken =
        SecretStore::instance().value(SecretStore::Key::GogRefreshToken);
    if (refreshToken.isEmpty()) {
        emit sessionRestored(false);
        return;
    }

    // A stored refresh token is all we have; the access token is always gone by
    // now. Mark it valid-but-expired so the first requestToken() renews it.
    m_tokens = Tokens();
    m_tokens.refreshToken = refreshToken;
    m_tokens.valid = true;
    m_tokens.expiresAt = QDateTime::currentDateTimeUtc();

    emit sessionRestored(true);
}

void GogAuth::loginWithCode(const QString& code)
{
    if (code.isEmpty()) {
        emit loginFailed(QStringLiteral("No sign-in code was provided."));
        return;
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), kClientId);
    form.addQueryItem(QStringLiteral("client_secret"), kClientSecret);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("redirect_uri"), kRedirectUri);

    QUrl url(kTokenUrl);
    url.setQuery(form);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "ProtonForge");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const QByteArray body = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            // The one people actually hit: codes are single-use and short-lived,
            // so a second attempt with the same pasted URL always lands here.
            if (status == 400) {
                emit loginFailed(QStringLiteral(
                    "That sign-in code was already used or has expired. "
                    "Click 'Open GOG login' again to get a fresh one."));
            } else {
                emit loginFailed(QStringLiteral("Could not reach GOG: %1")
                                     .arg(reply->errorString()));
            }
            return;
        }

        const Tokens tokens = parseTokenResponse(body, QDateTime::currentDateTimeUtc());
        if (!tokens.valid) {
            emit loginFailed(QStringLiteral("GOG's reply did not contain a usable token."));
            return;
        }

        storeTokens(tokens);
        emit loginSucceeded(tokens.userId);
    });
}

void GogAuth::logout()
{
    m_tokens = Tokens();
    SecretStore::instance().clear(SecretStore::Key::GogRefreshToken);
    settleWaiters(false, QStringLiteral("Signed out."));
    emit loggedOut();
}

void GogAuth::invalidateAccessToken()
{
    // Not logged out — just no longer trusting that the access token is good.
    m_tokens.accessToken.clear();
    m_tokens.expiresAt = QDateTime::currentDateTimeUtc();
}

quint64 GogAuth::requestToken()
{
    const quint64 requestId = m_nextRequestId++;

    if (!m_tokens.valid) {
        QTimer::singleShot(0, this, [this, requestId]() {
            emit tokenFailed(requestId, QStringLiteral("Not signed in to GOG."));
        });
        return requestId;
    }

    if (!needsRefresh(m_tokens, QDateTime::currentDateTimeUtc()) && !m_tokens.accessToken.isEmpty()) {
        const QString token = m_tokens.accessToken;
        QTimer::singleShot(0, this, [this, requestId, token]() {
            emit tokenReady(requestId, token);
        });
        return requestId;
    }

    // Queue onto whatever refresh is already running rather than starting a
    // second one. See the note on requestToken() in the header: two refreshes
    // racing loses the session outright.
    m_waiting.append(requestId);
    if (!m_refreshInFlight) {
        startRefresh();
    }
    return requestId;
}

void GogAuth::startRefresh()
{
    m_refreshInFlight = true;

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), kClientId);
    form.addQueryItem(QStringLiteral("client_secret"), kClientSecret);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), m_tokens.refreshToken);

    QUrl url(kTokenUrl);
    url.setQuery(form);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "ProtonForge");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_refreshInFlight = false;

        const QByteArray body = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            if (status == 400) {
                // invalid_grant: the stored refresh token is dead. Nothing here
                // can recover it, so stop pretending to be signed in.
                m_tokens = Tokens();
                SecretStore::instance().clear(SecretStore::Key::GogRefreshToken);
                settleWaiters(false, QStringLiteral("Your GOG sign-in has expired."));
                emit reauthenticationRequired();
                return;
            }
            settleWaiters(false, QStringLiteral("Could not reach GOG: %1")
                                     .arg(reply->errorString()));
            return;
        }

        const Tokens tokens = parseTokenResponse(body, QDateTime::currentDateTimeUtc());
        if (!tokens.valid) {
            settleWaiters(false, QStringLiteral("GOG's reply did not contain a usable token."));
            return;
        }

        storeTokens(tokens);
        settleWaiters(true, QString());
    });
}

void GogAuth::settleWaiters(bool ok, const QString& reason)
{
    const QList<quint64> waiting = m_waiting;
    m_waiting.clear();

    for (const quint64 requestId : waiting) {
        if (ok) {
            emit tokenReady(requestId, m_tokens.accessToken);
        } else {
            emit tokenFailed(requestId, reason);
        }
    }
}

void GogAuth::storeTokens(const Tokens& tokens)
{
    m_tokens = tokens;
    // Only the refresh token is worth persisting; the access token is stale
    // within the hour and would just widen the blast radius of a leaked file.
    SecretStore::instance().setValue(SecretStore::Key::GogRefreshToken, tokens.refreshToken);
}
