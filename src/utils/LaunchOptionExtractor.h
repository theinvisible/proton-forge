#ifndef LAUNCHOPTIONEXTRACTOR_H
#define LAUNCHOPTIONEXTRACTOR_H

#include <QList>
#include <QString>
#include "network/ProtonDBClient.h"

// Mines free-text ProtonDB report notes for launch-option recommendations.
// Source-agnostic: it works on any list of reports with a `notes` field,
// regardless of where the reports came from.
//
// Detects:
//   * whole launch-option lines containing "%command%" (highest confidence),
//   * environment-variable assignments (PROTON_*, DXVK_*, VKD3D_*, etc.),
//   * known wrapper commands (gamemoderun, mangohud, gamescope).
// Results are aggregated across reports, deduplicated, and ranked by how often
// they appear (and whether they include %command%).
class LaunchOptionExtractor {
public:
    struct Suggestion {
        QString snippet;        // the extracted launch-option text
        int occurrences = 0;    // number of reports it was found in
        bool hasCommand = false;// contains "%command%" (a full launch line)
        QString sampleSource;   // e.g. "Proton 9.0-3 · driver 550.120"
    };

    static QList<Suggestion> extract(const QList<ProtonDBClient::Report>& reports,
                                     int maxResults = 8);
};

#endif // LAUNCHOPTIONEXTRACTOR_H
