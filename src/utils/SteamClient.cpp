#include "SteamClient.h"
#include "SteamPaths.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace {

// Owned by `steam-runtime-launcher-service --alongside-steam`, which the Steam
// client starts once it is up — the closest thing to a "client is ready" signal
// that exists. Flatpak Steam can own it too: its Flathub manifest grants
// `com.steampowered.*=own` in the session bus policy, so xdg-dbus-proxy holds
// the name on the real session bus on the app's behalf.
const char* kLauncherServiceName = "com.steampowered.PressureVessel.LaunchAlongsideSteam";

bool launcherServiceRegistered()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    QDBusConnectionInterface* iface = bus.interface();
    if (!iface) {
        return false;
    }
    return iface->isServiceRegistered(QString::fromLatin1(kLauncherServiceName));
}

// Native Steam writes its PID to ~/.steam/steam.pid. That file survives a
// shutdown, so the PID needs checking for liveness *and* identity — otherwise a
// stale entry or a recycled PID reads as a running client.
bool nativeClientAlive()
{
    const QString pidPath = SteamPaths::steamPidFilePath();
    if (pidPath.isEmpty()) {
        return false;
    }

    QFile pidFile(pidPath);
    if (!pidFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    bool ok = false;
    const qint64 pid = pidFile.readAll().trimmed().toLongLong(&ok);
    if (!ok || pid <= 0) {
        return false;
    }

    QFile comm(QString("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;   // process is gone; the pid file was stale
    }
    return comm.readAll().trimmed() == "steam";
}

// Flatpak shares only the network and IPC namespaces, not PIDs, so the PID
// written inside the sandbox means nothing on the host. Ask flatpak instead.
bool flatpakClientAlive()
{
    QProcess proc;
    proc.start("flatpak", {"ps", "--columns=application"});
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        return false;
    }
    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList apps = output.split('\n', Qt::SkipEmptyParts);
    for (const QString& app : apps) {
        if (app.trimmed() == "com.valvesoftware.Steam") {
            return true;
        }
    }
    return false;
}

bool clientAlive()
{
    switch (SteamPaths::detectedVariant()) {
    case SteamPaths::Variant::Flatpak: return flatpakClientAlive();
    case SteamPaths::Variant::Native:  return nativeClientAlive();
    case SteamPaths::Variant::None:    break;
    }
    return false;
}

} // namespace

namespace SteamClient {

State state()
{
    // A registered D-Bus name cannot go stale — it disappears with its owner —
    // so this alone is enough to call the client ready.
    if (launcherServiceRegistered()) {
        return State::Ready;
    }
    return clientAlive() ? State::Starting : State::NotRunning;
}

bool isRunning()
{
    return state() != State::NotRunning;
}

bool isReady()
{
    return state() == State::Ready;
}

bool start(QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    switch (SteamPaths::detectedVariant()) {
    case SteamPaths::Variant::Flatpak:
        if (QProcess::startDetached("flatpak", {"run", "com.valvesoftware.Steam", "-silent"})) {
            return true;
        }
        return fail("Could not start Flatpak Steam (flatpak run com.valvesoftware.Steam).");

    case SteamPaths::Variant::Native: {
        if (QProcess::startDetached("steam", {"-silent"})) {
            return true;
        }
        // `steam` is not necessarily on PATH; native installs ship steam.sh in
        // the Steam root.
        const QString root = SteamPaths::steamRoot();
        const QString script = root.isEmpty() ? QString() : root + "/steam.sh";
        if (!script.isEmpty() && QFileInfo(script).isExecutable()
            && QProcess::startDetached(script, {"-silent"})) {
            return true;
        }
        return fail("Could not start Steam: 'steam' is not on PATH and steam.sh could not be run.");
    }

    case SteamPaths::Variant::None:
        break;
    }

    return fail("No Steam installation detected.");
}

} // namespace SteamClient
