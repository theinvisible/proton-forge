#include "GogLoginDialog.h"
#include "AppStyle.h"
#include "gog/GogAuth.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QUrl>
#include <QVBoxLayout>

GogLoginDialog::GogLoginDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Sign in to GOG");
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);

    auto* intro = new QLabel(
        "ProtonForge signs you in through GOG's own login page, in your browser. "
        "Your password is never seen by ProtonForge.", this);
    intro->setWordWrap(true);
    intro->setStyleSheet("color: #ccc;");
    layout->addWidget(intro);

    // --- step 1 ---
    auto* step1 = new QLabel("<b>1.</b> Open the GOG login page:", this);
    step1->setStyleSheet("color: #e0e0e0;");
    layout->addWidget(step1);

    m_openButton = new QPushButton("Open GOG login in your browser", this);
    m_openButton->setStyleSheet(AppStyle::dialogButtonStyle());
    layout->addWidget(m_openButton);

    // Only revealed when opening the browser fails. In a Flatpak that goes
    // through the OpenURI portal; on a session without one it falls back to
    // xdg-open, which may not exist inside the sandbox.
    auto* urlRow = new QHBoxLayout();
    m_urlField = new QLineEdit(GogAuth::authorizationUrl(), this);
    m_urlField->setReadOnly(true);
    m_urlField->setCursorPosition(0);
    m_copyUrlButton = new QPushButton("Copy link", this);
    m_copyUrlButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    urlRow->addWidget(m_urlField, 1);
    urlRow->addWidget(m_copyUrlButton);
    layout->addLayout(urlRow);
    m_urlField->hide();
    m_copyUrlButton->hide();

    // --- step 2 ---
    auto* step2 = new QLabel(
        "<b>2.</b> After signing in, your browser lands on a mostly blank GOG page. "
        "Copy the whole address from the address bar and paste it here — it contains "
        "<code>code=</code>.", this);
    step2->setWordWrap(true);
    step2->setStyleSheet("color: #e0e0e0;");
    layout->addWidget(step2);

    auto* pasteRow = new QHBoxLayout();
    m_pasteField = new QLineEdit(this);
    m_pasteField->setPlaceholderText("https://embed.gog.com/on_login_success?...&code=...");
    m_pasteButton = new QPushButton("Paste", this);
    m_pasteButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    pasteRow->addWidget(m_pasteField, 1);
    pasteRow->addWidget(m_pasteButton);
    layout->addLayout(pasteRow);

    // Inline, never a message box: a modal would take the pasted text off screen
    // just as the user needs to look at it.
    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: #e57373;");
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(4);
    m_progress->hide();
    layout->addWidget(m_progress);

    auto* disclaimer = new QLabel(
        "ProtonForge talks to GOG using the same interface the GOG Galaxy client uses. "
        "ProtonForge is not affiliated with or endorsed by GOG.", this);
    disclaimer->setWordWrap(true);
    disclaimer->setStyleSheet("color: #777; font-size: 11px;");
    layout->addSpacing(4);
    layout->addWidget(disclaimer);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setStyleSheet(AppStyle::secondaryButtonStyle());
    m_signInButton = new QPushButton("Sign in", this);
    m_signInButton->setStyleSheet(AppStyle::dialogButtonStyle());
    m_signInButton->setDefault(true);
    m_signInButton->setEnabled(false);
    buttons->addWidget(m_cancelButton);
    buttons->addSpacing(8);
    buttons->addWidget(m_signInButton);
    layout->addSpacing(4);
    layout->addLayout(buttons);

    connect(m_openButton, &QPushButton::clicked, this, &GogLoginDialog::openBrowser);
    connect(m_copyUrlButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_urlField->text());
    });
    connect(m_pasteButton, &QPushButton::clicked, this, &GogLoginDialog::pasteFromClipboard);
    connect(m_pasteField, &QLineEdit::textChanged, this, &GogLoginDialog::onPastedTextChanged);
    connect(m_pasteField, &QLineEdit::returnPressed, this, &GogLoginDialog::submit);
    connect(m_signInButton, &QPushButton::clicked, this, &GogLoginDialog::submit);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    GogAuth& auth = GogAuth::instance();
    connect(&auth, &GogAuth::loginSucceeded, this, [this](const QString&) {
        setBusy(false);
        accept();
    });
    connect(&auth, &GogAuth::loginFailed, this, [this](const QString& reason) {
        setBusy(false);
        showError(reason);
    });
}

void GogLoginDialog::openBrowser()
{
    if (QDesktopServices::openUrl(QUrl(GogAuth::authorizationUrl()))) {
        return;
    }
    m_urlField->show();
    m_copyUrlButton->show();
    showError("ProtonForge could not open your browser. Copy the link below and "
              "open it yourself.");
}

void GogLoginDialog::pasteFromClipboard()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.isEmpty()) {
        m_pasteField->setText(text);
    }
}

void GogLoginDialog::onPastedTextChanged()
{
    clearError();
    m_signInButton->setEnabled(!m_pasteField->text().trimmed().isEmpty());
}

void GogLoginDialog::submit()
{
    const QString pasted = m_pasteField->text();
    if (pasted.trimmed().isEmpty()) {
        showError("Paste the address from your browser first.");
        return;
    }

    // Distinguish "GOG refused" from "that is not a sign-in link" before going
    // anywhere near the network — the wording is the only thing that tells the
    // user which of the two happened.
    const QString error = GogAuth::extractAuthError(pasted);
    const QString code = GogAuth::extractAuthCode(pasted);

    if (code.isEmpty()) {
        if (error == QLatin1String("access_denied")) {
            showError("You cancelled the GOG sign-in. Click the button above to try again.");
        } else if (!error.isEmpty()) {
            showError(QString("GOG said: %1").arg(error));
        } else if (pasted.contains(QLatin1String("://"))) {
            showError("That link has no sign-in code in it. Copy the address after the "
                      "login finishes — it contains code=.");
        } else {
            showError("That doesn't look like a GOG sign-in link. Paste the full address "
                      "from your browser's address bar.");
        }
        return;
    }

    clearError();
    setBusy(true);
    GogAuth::instance().loginWithCode(code);
}

void GogLoginDialog::setBusy(bool busy)
{
    m_progress->setVisible(busy);
    m_signInButton->setEnabled(!busy && !m_pasteField->text().trimmed().isEmpty());
    m_signInButton->setText(busy ? "Signing in…" : "Sign in");
    m_pasteField->setEnabled(!busy);
    m_pasteButton->setEnabled(!busy);
    m_openButton->setEnabled(!busy);
}

void GogLoginDialog::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

void GogLoginDialog::clearError()
{
    m_errorLabel->clear();
    m_errorLabel->hide();
}
