#ifndef KSCREENDOCTOR_H
#define KSCREENDOCTOR_H

#include <QString>

// Single entry point for `kscreen-doctor -o`, KDE's display-state dump.
//
// Two independent consumers want that text — KdeDisplayProbe (native mode, VRR,
// bit depth, scale) and HDRChecker (the HDR line) — and each used to spawn its
// own copy, so DisplayDetector::detect() ran the same command twice in a row and
// stripped the same ANSI codes twice. Both now take the output as a parameter and
// DisplayDetector fetches it once.
class KScreenDoctor
{
public:
    // True when the tool is on PATH. A stat scan, not a subprocess.
    static bool available();

    // Runs `kscreen-doctor -o` once and returns its output with ANSI colour
    // codes removed. Null when the tool is missing, times out, or fails — never
    // an empty string standing in for "asked and got nothing", so callers can
    // stay silent instead of reporting a made-up state.
    static QString run();

    // Strips ANSI SGR sequences. Exposed so tests can feed captured raw output
    // through the same path the live code takes.
    static QString stripAnsi(QString text);
};

#endif // KSCREENDOCTOR_H
