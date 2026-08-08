// GOG's sign-in ends with the user copying an address out of their browser, so
// extractAuthCode() gets whatever the clipboard, the terminal and the chat
// client did to it on the way. Every row below is a shape that turns up in
// practice; getting one wrong reads to the user as "ProtonForge rejected my
// login" with no way to tell why.
//
// The rest is the token bookkeeping: what counts as a usable reply, and when to
// renew. Both are pure, so none of this touches the network.

#include <QTest>
#include <QDateTime>

#include "gog/GogAuth.h"
#include "gog/GogRequest.h"

class TstGogAuth : public QObject
{
    Q_OBJECT

private slots:
    void authorizationUrlIsWellFormed();

    void extractsTheCode_data();
    void extractsTheCode();

    void reportsWhatGogRefusedWith_data();
    void reportsWhatGogRefusedWith();

    void parsesATokenResponse();
    void rejectsATokenResponseWithoutARefreshToken();
    void survivesGarbageTokenResponses();
    void defaultsAMissingExpiry();

    void needsRefreshWellBeforeExpiry();
    void requestHeadersCarryTheBearer();

private:
    static QDateTime fixedNow() { return QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC); }
};

void TstGogAuth::authorizationUrlIsWellFormed()
{
    const QString url = GogAuth::authorizationUrl();

    QVERIFY(url.startsWith("https://auth.gog.com/auth?"));
    QVERIFY(url.contains("client_id=46899977096215655"));
    QVERIFY(url.contains("response_type=code"));
    QVERIFY(url.contains("layout=client2"));

    // Percent-encoded exactly once. Encoding it twice is a silent failure: GOG
    // simply refuses the redirect and the user sees an error page.
    QVERIFY2(url.contains("redirect_uri=https%3A%2F%2Fembed.gog.com%2Fon_login_success%3Forigin%3Dclient"),
             qPrintable(url));
    QVERIFY(!url.contains("%253A"));
}

void TstGogAuth::extractsTheCode_data()
{
    QTest::addColumn<QString>("pasted");
    QTest::addColumn<QString>("expected");

    const QString base = "https://embed.gog.com/on_login_success?origin=client";

    QTest::newRow("the whole redirect url")
        << base + "&code=abcdef123456" << "abcdef123456";
    QTest::newRow("code before other parameters")
        << "https://embed.gog.com/on_login_success?code=abcdef123456&origin=client"
        << "abcdef123456";
    QTest::newRow("in the fragment")
        << "https://embed.gog.com/on_login_success#code=abcdef123456&state=x"
        << "abcdef123456";
    // A real GOG code is a long alphanumeric string; the fallback is
    // deliberately strict so prose cannot be mistaken for one.
    const QString realisticCode = "sN9G4hZ2kQvB7xLpR3mTfW8cYd1JeA6u";
    QTest::newRow("a bare code, as people sometimes copy")
        << realisticCode << realisticCode;
    QTest::newRow("wrapped in quotes by a chat client")
        << "\"" + base + "&code=abcdef123456\"" << "abcdef123456";
    QTest::newRow("wrapped in angle brackets by a mail client")
        << "<" + base + "&code=abcdef123456>" << "abcdef123456";
    QTest::newRow("hard-wrapped by a terminal")
        << base + "&code=abcdef\n123456" << "abcdef123456";
    QTest::newRow("surrounded by whitespace")
        << "   " + base + "&code=abcdef123456   " << "abcdef123456";
    QTest::newRow("percent-encoded, decoded exactly once")
        << base + "&code=abc%2Bdef123456" << "abc+def123456";

    QTest::newRow("empty") << "" << "";
    QTest::newRow("a url with no code at all") << base << "";
    QTest::newRow("the user cancelled")
        << base + "&error=access_denied" << "";
    // Whitespace is stripped before this check, so a sentence arrives as one
    // long word. It has no digits and must not pass as a code.
    QTest::newRow("prose rather than a link")
        << "it just says login failed" << "";
    QTest::newRow("long prose, still no digits")
        << "the browser said something went badly wrong" << "";
    QTest::newRow("too short to be a code") << "abc123" << "";
}

void TstGogAuth::extractsTheCode()
{
    QFETCH(QString, pasted);
    QFETCH(QString, expected);
    QCOMPARE(GogAuth::extractAuthCode(pasted), expected);
}

void TstGogAuth::reportsWhatGogRefusedWith_data()
{
    QTest::addColumn<QString>("pasted");
    QTest::addColumn<QString>("expected");

    const QString base = "https://embed.gog.com/on_login_success?origin=client";

    QTest::newRow("bare error code")
        << base + "&error=access_denied" << "access_denied";
    QTest::newRow("description wins when present")
        << base + "&error=access_denied&error_description=You%20cancelled%20the%20sign-in"
        << "You cancelled the sign-in";
    QTest::newRow("nothing went wrong")
        << base + "&code=abcdef123456" << "";
    QTest::newRow("empty") << "" << "";
}

void TstGogAuth::reportsWhatGogRefusedWith()
{
    QFETCH(QString, pasted);
    QFETCH(QString, expected);
    QCOMPARE(GogAuth::extractAuthError(pasted), expected);
}

void TstGogAuth::parsesATokenResponse()
{
    const QByteArray json = R"({
        "access_token": "at-123",
        "refresh_token": "rt-456",
        "expires_in": 3600,
        "user_id": "48058169507196979",
        "session_id": "sess-789",
        "token_type": "bearer"
    })";

    const GogAuth::Tokens tokens = GogAuth::parseTokenResponse(json, fixedNow());

    QVERIFY(tokens.valid);
    QCOMPARE(tokens.accessToken, QStringLiteral("at-123"));
    QCOMPARE(tokens.refreshToken, QStringLiteral("rt-456"));
    // GOG sends the user id as a number in some replies and a string in others.
    QCOMPARE(tokens.userId, QStringLiteral("48058169507196979"));
    QCOMPARE(tokens.expiresAt, fixedNow().addSecs(3600));
}

void TstGogAuth::rejectsATokenResponseWithoutARefreshToken()
{
    // An access token alone is worthless: it dies within the hour and there is
    // nothing to renew it with. Accepting it would show the user as signed in
    // until it silently stopped working.
    const QByteArray json = R"({"access_token": "at-123", "expires_in": 3600})";
    QVERIFY(!GogAuth::parseTokenResponse(json, fixedNow()).valid);
}

void TstGogAuth::survivesGarbageTokenResponses()
{
    for (const QByteArray& bad : {QByteArray(""), QByteArray("not json"), QByteArray("[]"),
                                  QByteArray("{}"), QByteArray("<html>404</html>")}) {
        const GogAuth::Tokens tokens = GogAuth::parseTokenResponse(bad, fixedNow());
        QVERIFY2(!tokens.valid, "accepted garbage: " + bad);
    }
}

void TstGogAuth::defaultsAMissingExpiry()
{
    const QByteArray json = R"({"access_token": "at", "refresh_token": "rt"})";
    const GogAuth::Tokens tokens = GogAuth::parseTokenResponse(json, fixedNow());

    QVERIFY(tokens.valid);
    QCOMPARE(tokens.expiresAt, fixedNow().addSecs(3600));
}

void TstGogAuth::needsRefreshWellBeforeExpiry()
{
    GogAuth::Tokens tokens;
    tokens.valid = true;
    tokens.accessToken = "at";
    tokens.refreshToken = "rt";
    tokens.expiresAt = fixedNow().addSecs(3600);

    QVERIFY(!GogAuth::needsRefresh(tokens, fixedNow()));

    // The skew window: renew early rather than discovering the expiry from a
    // 401 halfway through a download.
    QVERIFY(!GogAuth::needsRefresh(tokens, fixedNow().addSecs(3600 - 301)));
    QVERIFY(GogAuth::needsRefresh(tokens, fixedNow().addSecs(3600 - 300)));
    QVERIFY(GogAuth::needsRefresh(tokens, fixedNow().addSecs(3600)));

    // Nothing to refresh from, or no idea when it dies: renew.
    QVERIFY(GogAuth::needsRefresh(GogAuth::Tokens(), fixedNow()));
    tokens.expiresAt = QDateTime();
    QVERIFY(GogAuth::needsRefresh(tokens, fixedNow()));
}

void TstGogAuth::requestHeadersCarryTheBearer()
{
    const QNetworkRequest authed =
        GogRequest::make(QUrl("https://api.gog.com/products/1"), "at-123");
    QCOMPARE(authed.rawHeader("Authorization"), QByteArray("Bearer at-123"));
    QCOMPARE(authed.rawHeader("User-Agent"), QByteArray("ProtonForge"));
    QCOMPARE(authed.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
             int(QNetworkRequest::NoLessSafeRedirectPolicy));

    // No token, no header — rather than "Bearer " with nothing after it.
    const QNetworkRequest anonymous = GogRequest::make(QUrl("https://cdn.gog.com/x"));
    QVERIFY(!anonymous.hasRawHeader("Authorization"));
}

QTEST_MAIN(TstGogAuth)
#include "tst_gogauth.moc"
