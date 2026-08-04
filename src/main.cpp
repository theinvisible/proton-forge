#include <QApplication>
#include <QIcon>
#include <QLockFile>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include "ui/MainWindow.h"
#include "ui/OpaqueTooltip.h"
#include "core/Cli.h"
#include "core/Game.h"
#include "utils/ProtonManager.h"
#include "Version.h"

int main(int argc, char *argv[])
{
    // Headless commands run before any widget exists, on a QCoreApplication and
    // without a display. Only arguments the CLI knows about divert here; a bare
    // invocation and Qt's own flags fall through to the GUI unchanged.
    if (Cli::isCliInvocation(argc, argv)) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QCoreApplication app(argc, argv);
        Cli::configureMetadata(app);
        return Cli::run(app);
    }

    QApplication app(argc, argv);

    // Application metadata
    app.setApplicationName("ProtonForge");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("ProtonForge");
    app.setOrganizationDomain("protonforge");

    // Set application icon
    app.setWindowIcon(QIcon(":/icon.svg"));

    // Single instance check
    QString lockFilePath = QDir::temp().absoluteFilePath("protonforge.lock");
    QLockFile lockFile(lockFilePath);
    lockFile.setStaleLockTime(0);

    if (!lockFile.tryLock(100)) {
        QMessageBox::warning(
            nullptr,
            "Application Already Running",
            "ProtonForge is already running.\n\nOnly one instance of the application can run at a time.",
            QMessageBox::Ok
        );
        return 1;
    }

    // Register metatypes
    qRegisterMetaType<Game>("Game");
    qRegisterMetaType<ProtonManager::ProtonRelease>("ProtonManager::ProtonRelease");

    // Apply dark style
    app.setStyle("Fusion");
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
    darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
    app.setPalette(darkPalette);

    // Load centralized stylesheet from Qt resource
    QFile styleFile(":/style.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(styleFile.readAll());
        styleFile.close();
    }

    // Install custom opaque tooltip to bypass compositor transparency
    OpaqueTooltip::instance().install(&app);

    MainWindow window;
    window.show();

    return app.exec();
}
