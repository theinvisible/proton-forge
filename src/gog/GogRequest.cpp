#include "GogRequest.h"
#include "GogAuth.h"

namespace GogRequest {

namespace {

// One authenticated GET, including its single retry.
//
// It is parented to the caller's context object, so a dialog that closes mid
// flight takes the exchange with it, and it deletes itself once the exchange is
// over either way. No Q_OBJECT: it declares no signals or slots of its own and
// only ever appears as a connection context.
class AuthenticatedGet : public QObject
{
public:
    AuthenticatedGet(QNetworkAccessManager* manager, const QUrl& url, QObject* context,
                     Handler handler)
        : QObject(context)
        , m_manager(manager)
        , m_url(url)
        , m_handler(std::move(handler))
    {
        GogAuth& auth = GogAuth::instance();

        // Connect before asking: requestToken() may answer immediately (it
        // defers through the event loop, but only just).
        connect(&auth, &GogAuth::tokenReady, this, [this](quint64 id, const QString& token) {
            if (id == m_requestId) {
                send(token);
            }
        });
        connect(&auth, &GogAuth::tokenFailed, this, [this](quint64 id, const QString&) {
            if (id == m_requestId) {
                finish(nullptr);
            }
        });

        askForToken();
    }

private:
    void askForToken() { m_requestId = GogAuth::instance().requestToken(); }

    void send(const QString& token)
    {
        QNetworkReply* reply = m_manager->get(make(m_url, token));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();

            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 && m_retriesLeft > 0) {
                // The token expired earlier than its own expiry claimed. Throw
                // it away and go round once with a fresh one.
                --m_retriesLeft;
                GogAuth::instance().invalidateAccessToken();
                askForToken();
                return;
            }

            // A second 401 is not a clock problem — let the caller see it.
            finish(reply);
        });
    }

    void finish(QNetworkReply* reply)
    {
        m_handler(reply);
        deleteLater();
    }

    QNetworkAccessManager* m_manager;
    QUrl m_url;
    Handler m_handler;
    quint64 m_requestId = 0;
    int m_retriesLeft = 1;
};

} // namespace

QNetworkRequest make(const QUrl& url, const QString& bearerToken)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "ProtonForge");
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!bearerToken.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + bearerToken).toUtf8());
    }
    return request;
}

void get(QNetworkAccessManager* manager, const QUrl& url, QObject* context, Handler onFinished)
{
    // Owned by `context`; deletes itself when the exchange ends.
    new AuthenticatedGet(manager, url, context, std::move(onFinished));
}

void getPublic(QNetworkAccessManager* manager, const QUrl& url, QObject* context,
               Handler onFinished)
{
    QNetworkReply* reply = manager->get(make(url, QString()));
    QObject::connect(reply, &QNetworkReply::finished, context,
                     [reply, onFinished = std::move(onFinished)]() {
        reply->deleteLater();
        onFinished(reply);
    });
}

} // namespace GogRequest
