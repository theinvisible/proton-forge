#ifndef GOGLOGINDIALOG_H
#define GOGLOGINDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>

// Signing in to GOG, without embedding a browser.
//
// GOG validates redirect_uri against what is registered for the Galaxy client
// id, and validates it again when the code is exchanged — so a loopback
// listener is not merely inconvenient, it is impossible. Every third-party GOG
// client therefore either ships a webview or asks the user to paste the address
// back. QtWebEngine is not in the KDE Flatpak runtime and would have to be built
// as a module, for the sole benefit of saving one copy-paste. So: paste.
//
// The password is only ever typed into GOG's own page, in the user's own
// browser. ProtonForge never sees it.
class GogLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GogLoginDialog(QWidget* parent = nullptr);

private slots:
    void openBrowser();
    void pasteFromClipboard();
    void submit();
    void onPastedTextChanged();

private:
    void setBusy(bool busy);
    void showError(const QString& message);
    void clearError();

    QPushButton*  m_openButton;
    QLineEdit*    m_urlField;      // shown only if the browser could not be opened
    QPushButton*  m_copyUrlButton;
    QLineEdit*    m_pasteField;
    QPushButton*  m_pasteButton;
    QLabel*       m_errorLabel;
    QProgressBar* m_progress;
    QPushButton*  m_signInButton;
    QPushButton*  m_cancelButton;
};

#endif // GOGLOGINDIALOG_H
