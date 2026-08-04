#ifndef CLI_H
#define CLI_H

#include <QString>

class QCoreApplication;

// Headless command line, primarily so the app is testable without a screen.
//
// The GUI is the product; this is a side door into the same code. It exists
// because everything interesting in ProtonForge happens below the widgets —
// Steam discovery (SteamPaths, VDFParser, SteamLauncher), the launch-options
// translation (EnvBuilder) and the launch chain (GameRunner) — and none of it
// could be asserted from a test without a way in.
//
// Dispatch happens in main() *before* QApplication is constructed, and only
// when isCliInvocation() recognises one of the options below. Qt's own flags
// (-platform, -style, ...) and a bare invocation are untouched, so the GUI
// path behaves exactly as it did before this file existed.
namespace Cli {

// Exit codes. Anything a test asserts on lives here rather than as a literal.
enum ExitCode {
    Ok          = 0,
    Error       = 1,   // the command ran but failed
    UsageError  = 2,   // bad or missing arguments
    NoSteam     = 3,   // no Steam installation detected
    UnknownGame = 4    // no game with that app id
};

// True when argv holds one of our long options. Cheap string comparison only —
// no Qt objects exist yet at this point.
bool isCliInvocation(int argc, char* argv[]);

// setApplicationName/Version/Organization, matching what main() sets for the
// GUI so QStandardPaths and QSettings resolve to the same places.
void configureMetadata(QCoreApplication& app);

// Parses and runs. Returns the process exit code.
int run(QCoreApplication& app);

} // namespace Cli

#endif // CLI_H
