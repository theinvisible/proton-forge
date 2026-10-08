#ifndef COMPATTOOLRESOLVER_H
#define COMPATTOOLRESOLVER_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVersionNumber>

#include "core/FeatureGate.h"

// Finds Proton builds on disk and answers "which one?" — by Steam's internal
// tool name, by "newest of a kind", or by ProtonForge's default preference.
//
// Pure: every directory is a parameter, so it runs against fixture trees and
// never consults SteamPaths or a singleton. Versions are compared numerically
// throughout; sorting directory names is what made GE-Proton9-20 outrank
// GE-Proton11-7 and the oldest Proton-CachyOS win the default.
namespace CompatToolResolver {

enum class Kind {
    Other,              // a custom tool we don't recognise by name
    CachyOS,            // proton-cachyos-11.0-20260703-slr…
    GE,                 // GE-Proton11-7
    Valve,              // "Proton 11.0" — a numbered Valve release
    ValveExperimental,  // "Proton - Experimental"
    ValveHotfix,        // "Proton Hotfix"
};

struct ToolInfo {
    QString path;           // directory containing the `proton` script
    QString dirName;        // last path component
    QString internalName;   // Steam's name (compatibilitytool.vdf), else dirName
    Kind kind = Kind::Other;
    // Full precision where the name or version file allows it:
    // CachyOS 11.0.20260703, GE 11.7.0, Valve 11.0.2. Null when unknown.
    QVersionNumber version;

    bool isValid() const { return !path.isEmpty(); }
};

// The fork FeatureGate should assume. Experimental and Hotfix track a moving
// target and stay Unknown, which the gate treats leniently.
FeatureGate::Fork forkOf(Kind kind);

// "proton-cachyos-11.0-20260703-slr-x86_64_v3" (or a release file name) -> 11.0.20260703
QVersionNumber parseCachyOS(const QString& name);
// "GE-Proton11-7" -> 11.7.0
QVersionNumber parseGE(const QString& name);
// Contents of a Valve Proton's `version` file, e.g.
// "1788504981 proton-11.0-2c-x86_64" -> 11.0.2. Null if unrecognised.
QVersionNumber parseValveVersionFile(const QString& text);

Kind classify(const QString& dirName);

// Inspect one tool directory (must contain `proton`); internalName defaults
// to the directory name. Invalid ToolInfo if it is not a Proton.
ToolInfo inspect(const QString& path, const QString& internalName = QString());

// Every Proton under these compatibilitytools.d-style directories. Internal
// names come from each tool's compatibilitytool.vdf (one manifest may
// register several tools; install_path is relative to the manifest).
QList<ToolInfo> scanCompatDirs(const QStringList& dirs);

// Valve's Protons in these Steam library roots (<lib>/common/Proton*).
QList<ToolInfo> scanLibraries(const QStringList& libraryPaths);

// Where Steam additionally registers system-wide tools (distro packages):
// /usr/share/steam/compatibilitytools.d, /usr/local/share/…, and
// $STEAM_EXTRA_COMPAT_TOOLS_PATHS. Only for resolving a mapped name — the
// "auto" default deliberately sticks to the user's own directories.
QStringList systemCompatDirs();

// Newest tool of `kind`; invalid if there is none.
ToolInfo newest(const QList<ToolInfo>& tools, Kind kind);

// Steam's CompatToolMapping name -> tool. Tried in order: exact internal
// name, directory name, proton_experimental / proton_hotfix, proton_N
// (incl. legacy proton_63 = 6.3), the aliases proton-stable / proton-next
// (newest numbered Valve release). Invalid if nothing matches.
ToolInfo resolveMappedName(const QString& name, const QList<ToolInfo>& tools);

// ProtonForge's preference when nothing is selected: newest CachyOS, newest
// GE, Experimental, newest numbered Valve, Hotfix.
ToolInfo defaultChoice(const QList<ToolInfo>& tools);

// "Latest Steam Proton": Experimental, else the newest numbered release.
ToolInfo latestSteam(const QList<ToolInfo>& tools);

// Sorted newest first, for presenting a list.
QList<ToolInfo> sortedNewestFirst(QList<ToolInfo> tools);

}  // namespace CompatToolResolver

#endif // COMPATTOOLRESOLVER_H
