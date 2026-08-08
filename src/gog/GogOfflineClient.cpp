#include "GogOfflineClient.h"
#include "gog/GogAuth.h"
#include "gog/GogRequest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>

namespace {

const QString kEmbed = QStringLiteral("https://embed.gog.com");

QString normalizeOs(const QString& raw)
{
    const QString os = raw.trimmed().toLower();
    if (os == QLatin1String("windows")) return QStringLiteral("windows");
    if (os == QLatin1String("linux")) return QStringLiteral("linux");
    if (os == QLatin1String("mac") || os == QLatin1String("osx")) return QStringLiteral("mac");
    return os;
}

} // namespace

GogOfflineClient& GogOfflineClient::instance()
{
    static GogOfflineClient client;
    return client;
}

GogOfflineClient::GogOfflineClient()
    : m_networkManager(new QNetworkAccessManager(this))
{
    qRegisterMetaType<GogOfflineClient::Installer>("GogOfflineClient::Installer");

    // A stalled connection has to become an error the caller can act on rather
    // than a download that sits at 41 % forever. Generous, because this is one
    // long transfer and a slow link is not a failure.
    m_networkManager->setTransferTimeout(120 * 1000);
}

// --- parsing -----------------------------------------------------------------

qint64 GogOfflineClient::parseSize(const QString& text)
{
    // "3.4 GB", "512 MB", "980 KB". Never a guess: an unrecognised shape
    // returns 0 so a caller can tell "we do not know" from "it is small".
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*([0-9]+(?:[.,][0-9]+)?)\\s*(B|KB|MB|GB|TB)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        return 0;
    }

    bool ok = false;
    const double value = QString(match.captured(1)).replace(',', '.').toDouble(&ok);
    if (!ok || value <= 0) {
        return 0;
    }

    const QString unit = match.captured(2).toUpper();
    double multiplier = 1;
    if (unit == QLatin1String("KB")) multiplier = 1024.0;
    else if (unit == QLatin1String("MB")) multiplier = 1024.0 * 1024;
    else if (unit == QLatin1String("GB")) multiplier = 1024.0 * 1024 * 1024;
    else if (unit == QLatin1String("TB")) multiplier = 1024.0 * 1024 * 1024 * 1024;

    return static_cast<qint64>(value * multiplier);
}

QList<GogOfflineClient::Installer> GogOfflineClient::parseGameDetails(const QByteArray& json)
{
    QList<Installer> installers;

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return installers;
    }

    // `downloads` is [[language, {os: [installer, ...]}], ...] — a list of
    // two-element arrays, not an object. Reading it as a map yields nothing.
    const QJsonArray byLanguage = doc.object().value(QStringLiteral("downloads")).toArray();
    for (const QJsonValue& languageEntry : byLanguage) {
        const QJsonArray pair = languageEntry.toArray();
        if (pair.size() < 2) {
            continue;
        }

        const QString language = pair.at(0).toString();
        const QJsonObject byOs = pair.at(1).toObject();

        for (auto it = byOs.constBegin(); it != byOs.constEnd(); ++it) {
            for (const QJsonValue& value : it.value().toArray()) {
                const QJsonObject object = value.toObject();

                Installer installer;
                installer.id        = object.value(QStringLiteral("id")).toVariant().toString();
                installer.name      = object.value(QStringLiteral("name")).toString();
                installer.os        = normalizeOs(it.key());
                installer.language  = language;
                installer.version   = object.value(QStringLiteral("version")).toString();
                installer.manualUrl = object.value(QStringLiteral("manualUrl")).toString();
                installer.size      = parseSize(object.value(QStringLiteral("size")).toString());

                if (!installer.manualUrl.isEmpty()) {
                    installers.append(installer);
                }
            }
        }
    }

    return installers;
}

GogOfflineClient::Installer GogOfflineClient::selectInstaller(const QList<Installer>& installers,
                                                             const QString& os,
                                                             const QString& language)
{
    const QString wantedOs = normalizeOs(os);

    QList<Installer> forOs;
    for (const Installer& installer : installers) {
        if (installer.os == wantedOs) {
            forOs.append(installer);
        }
    }
    if (forOs.isEmpty()) {
        return {};
    }

    for (const Installer& installer : forOs) {
        if (installer.language.compare(language, Qt::CaseInsensitive) == 0) {
            return installer;
        }
    }
    // English rather than "whatever came first": a game installed in a language
    // the user cannot read is worse than one in the lingua franca.
    for (const Installer& installer : forOs) {
        if (installer.language.compare(QLatin1String("English"), Qt::CaseInsensitive) == 0) {
            return installer;
        }
    }
    return forOs.first();
}

QString GogOfflineClient::downloadUrl(const QString& manualUrl)
{
    if (manualUrl.isEmpty()) {
        return QString();
    }
    if (manualUrl.startsWith(QLatin1String("http"))) {
        return manualUrl;
    }
    return kEmbed + (manualUrl.startsWith('/') ? manualUrl : "/" + manualUrl);
}

// --- network -----------------------------------------------------------------

void GogOfflineClient::fetchInstallers(const QString& productId)
{
    const QUrl url(QStringLiteral("%1/account/gameDetails/%2.json").arg(kEmbed, productId));

    GogRequest::get(m_networkManager, url, this, [this, productId](QNetworkReply* reply) {
        if (!reply || reply->error() != QNetworkReply::NoError) {
            emit installersFailed(productId, reply ? reply->errorString()
                                                   : QStringLiteral("not signed in to GOG"));
            return;
        }

        const QList<Installer> installers = parseGameDetails(reply->readAll());
        if (installers.isEmpty()) {
            emit installersFailed(productId,
                                  QStringLiteral("GOG lists no offline installers for this game"));
            return;
        }
        emit installersReady(productId, installers);
    });
}

void GogOfflineClient::download(const QString& productId, const Installer& installer,
                                const QString& destPath)
{
    if (m_reply) {
        emit downloadFailed(productId, QStringLiteral("another installer is already downloading"));
        return;
    }

    const QString url = downloadUrl(installer.manualUrl);
    if (url.isEmpty()) {
        emit downloadFailed(productId, QStringLiteral("this installer has no download link"));
        return;
    }

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    m_productId = productId;
    m_destPath = destPath;
    // Whatever a previous attempt already fetched. GOG's CDN honours Range, and
    // a 20 GB installer is not something to start over after a dropped Wi-Fi.
    m_resumeFrom = QFileInfo(destPath).size();

    // The token is fetched first rather than inside the request: this URL is an
    // authenticated redirect, and GogRequest::get cannot be used here because it
    // hands back a finished reply — the whole point is to stream the body as it
    // arrives instead of holding twenty gigabytes in memory.
    GogAuth& auth = GogAuth::instance();
    const quint64 requestId = auth.requestToken();

    auto* ready = new QMetaObject::Connection;
    auto* failed = new QMetaObject::Connection;
    const auto disarm = [ready, failed]() {
        QObject::disconnect(*ready);
        QObject::disconnect(*failed);
        delete ready;
        delete failed;
    };

    *ready = connect(&auth, &GogAuth::tokenReady, this,
                     [this, requestId, url, disarm](quint64 id, const QString& token) {
        if (id != requestId) {
            return;
        }
        disarm();
        startTransfer(url, token);
    });
    *failed = connect(&auth, &GogAuth::tokenFailed, this,
                      [this, requestId, disarm](quint64 id, const QString& reason) {
        if (id != requestId) {
            return;
        }
        disarm();
        emit downloadFailed(m_productId, reason);
    });
}

void GogOfflineClient::startTransfer(const QString& url, const QString& token)
{
    m_sink = new QFile(m_destPath, this);
    if (!m_sink->open(m_resumeFrom > 0 ? (QIODevice::WriteOnly | QIODevice::Append)
                                       : (QIODevice::WriteOnly | QIODevice::Truncate))) {
        const QString error = m_sink->errorString();
        delete m_sink;
        m_sink = nullptr;
        emit downloadFailed(m_productId,
                            QStringLiteral("cannot write %1: %2").arg(m_destPath, error));
        return;
    }

    QNetworkRequest request = GogRequest::make(QUrl(url), token);
    if (m_resumeFrom > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(m_resumeFrom) + "-");
    }

    m_reply = m_networkManager->get(request);
    m_rangeHonoured = m_resumeFrom == 0;

    // A server may ignore Range and send the whole file with 200 instead of 206.
    // Appending that to what is already on disk yields a file of the right shape
    // and twice the length — which unpacks to nonsense rather than failing.
    connect(m_reply, &QNetworkReply::metaDataChanged, this, [this]() {
        if (!m_reply || m_resumeFrom == 0 || m_rangeHonoured) {
            return;
        }
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 206) {
            m_rangeHonoured = true;
            return;
        }
        // Start over rather than corrupt the file.
        m_resumeFrom = 0;
        m_rangeHonoured = true;
        if (m_sink) {
            m_sink->seek(0);
            m_sink->resize(0);
        }
    });

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_sink && m_reply) {
            m_sink->write(m_reply->readAll());
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        emit downloadProgress(m_productId, m_resumeFrom + received,
                              total > 0 ? m_resumeFrom + total : 0);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;

        if (m_sink) {
            m_sink->write(reply->readAll());
            m_sink->close();
            delete m_sink;
            m_sink = nullptr;
        }

        const QNetworkReply::NetworkError error = reply->error();
        const QString message = reply->errorString();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            // The partial file stays where it is: the next attempt resumes.
            emit downloadFailed(m_productId, message);
            return;
        }
        emit downloadFinished(m_productId, m_destPath);
    });
}

void GogOfflineClient::cancel()
{
    abortDownload();
    if (m_sink) {
        m_sink->close();
        delete m_sink;
        m_sink = nullptr;
    }
}

void GogOfflineClient::abortDownload()
{
    if (!m_reply) {
        return;
    }
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->abort();
    reply->deleteLater();
}
