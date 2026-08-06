#include "SteamClient.h"
#include "SteamPaths.h"
#include "utils/ProcessRunner.h"

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
    const QString output = ProcessRunner::run("flatpak", {"ps", "--columns=application"});
    if (output.isNull())
        return false;

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

Diagnostics diagnose()
{
    Diagnostics d;

    const SteamPaths::Variant variant = SteamPaths::detectedVariant();
    switch (variant) {
    case SteamPaths::Variant::Native:  d.variant = "native";  break;
    case SteamPaths::Variant::Flatpak: d.variant = "flatpak"; break;
    case SteamPaths::Variant::None:    d.variant = "none";    break;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    d.dbusConnected = bus.isConnected();
    if (d.dbusConnected) {
        d.dbusNameRegistered = launcherServiceRegistered();
    }

    if (d.dbusNameRegistered) {
        d.state = State::Ready;
        d.detail = QString("%1 is registered on the session bus")
                       .arg(QString::fromLatin1(kLauncherServiceName));
        return d;
    }

    switch (variant) {
    case SteamPaths::Variant::Native: {
        d.pidFilePath = SteamPaths::steamPidFilePath();
        QFile pidFile(d.pidFilePath);
        d.pidFileExists = pidFile.exists();
        if (!d.pidFileExists) {
            d.detail = "no pid file at " + d.pidFilePath;
            break;
        }
        if (!pidFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            d.detail = "pid file is not readable: " + d.pidFilePath;
            break;
        }
        bool ok = false;
        const qint64 pid = pidFile.readAll().trimmed().toLongLong(&ok);
        if (!ok || pid <= 0) {
            d.detail = "pid file does not contain a usable pid";
            break;
        }
        d.pid = pid;
        QFile comm(QString("/proc/%1/comm").arg(pid));
        if (!comm.open(QIODevice::ReadOnly | QIODevice::Text)) {
            d.detail = QString("pid %1 is gone — the pid file is stale").arg(pid);
            break;
        }
        d.comm = QString::fromUtf8(comm.readAll()).trimmed();
        if (d.comm != QLatin1String("steam")) {
            d.detail = QString("pid %1 is alive but is '%2', not 'steam' — recycled pid")
                           .arg(pid).arg(d.comm);
            break;
        }
        d.state = State::Starting;
        d.detail = QString("pid %1 is alive and named 'steam', but the launcher "
                           "service is not registered yet").arg(pid);
        break;
    }

    case SteamPaths::Variant::Flatpak: {
        const QString output = ProcessRunner::run("flatpak", {"ps", "--columns=application"});
        if (output.isNull()) {
            d.detail = "`flatpak ps` did not answer within 3 s, or is unavailable";
            break;
        }
        d.flatpakProbeRan = true;
        const QStringList apps = output.split('\n', Qt::SkipEmptyParts);
        for (const QString& app : apps) {
            if (app.trimmed() == QLatin1String("com.valvesoftware.Steam")) {
                d.flatpakAppListed = true;
                break;
            }
        }
        if (d.flatpakAppListed) {
            d.state = State::Starting;
            d.detail = "com.valvesoftware.Steam is listed by `flatpak ps`, but the "
                       "launcher service is not registered yet";
        } else {
            d.detail = "com.valvesoftware.Steam is not listed by `flatpak ps`";
        }
        break;
    }

    case SteamPaths::Variant::None:
        d.detail = "no Steam installation detected";
        break;
    }

    return d;
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
