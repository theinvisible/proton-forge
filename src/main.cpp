#include <QApplication>
#include "ui/MainWindow.h"
#include "core/Game.h"
#include "utils/ProtonManager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Application metadata
    app.setApplicationName("NvidiaAppLinux");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("NvidiaAppLinux");
    app.setOrganizationDomain("nvidia-app-linux");

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

    MainWindow window;
    window.show();

    return app.exec();
}
