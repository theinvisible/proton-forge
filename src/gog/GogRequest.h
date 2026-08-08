#ifndef GOGREQUEST_H
#define GOGREQUEST_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QUrl>
#include <functional>

// Every authenticated GOG call goes through here, so the token handling and the
// 401 retry exist once rather than in each client.
//
// The retry matters because there are two independent clocks: the access token
// expires on GOG's schedule, and GogAuth refreshes on ours. Skew between them
// means a request can be refused by a token that looked fine a moment ago. One
// retry against a freshly minted token covers it; a second failure is a real
// error and is reported as one.
namespace GogRequest {

// The reply is finished and is deleted once the handler returns — read what you
// need from it, do not store it.
//
// **The reply is null when the request never got as far as an answer**: not
// signed in, the refresh failed, or a second 401 against a freshly minted
// token. Handlers must check.
using Handler = std::function<void(QNetworkReply*)>;

// Pure, so the header shape can be asserted without a socket.
QNetworkRequest make(const QUrl& url, const QString& bearerToken = QString());

// Authenticated GET. Obtains a token first, refreshing if needed, and retries
// once on 401. `context` scopes the callbacks: if it dies, nothing fires.
void get(QNetworkAccessManager* manager, const QUrl& url, QObject* context, Handler onFinished);

// GET without a token, for the parts of the content system that need none.
void getPublic(QNetworkAccessManager* manager, const QUrl& url, QObject* context,
               Handler onFinished);

} // namespace GogRequest

#endif // GOGREQUEST_H
