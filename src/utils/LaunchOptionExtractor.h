#ifndef LAUNCHOPTIONEXTRACTOR_H
#define LAUNCHOPTIONEXTRACTOR_H

#include <QList>
#include <QString>
#include "network/ProtonDBClient.h"

// Mines ProtonDB reports for launch-option recommendations.
//
// Sources, in priority order:
//   * the report's explicit `launchOptions` field (what the user actually set),
//   * whole launch-option lines containing "%command%" in the notes,
//   * environment-variable assignments (PROTON_*, DXVK_*, VKD3D_*, etc.),
//   * known wrapper commands (gamemoderun, mangohud, gamescope) in the notes.
// Results are aggregated across reports, deduplicated, and ranked.
class LaunchOptionExtractor {
public:
    struct Suggestion {
        QString snippet;        // the extracted launch-option text
        int occurrences = 0;    // number of reports it was found in
        bool direct = false;    // from the explicit launchOptions field
        bool hasCommand = false;// contains "%command%" (a full launch line)
        QString sampleSource;   // e.g. "Proton 9.0-3 · driver 550.120"
        // Representative source reports (capped), so the UI can show the full
        // comment/context a snippet came from.
        QList<ProtonDBClient::Report> sources;
    };

    static QList<Suggestion> extract(const QList<ProtonDBClient::Report>& reports,
                                     int maxResults = 8);
};

#endif // LAUNCHOPTIONEXTRACTOR_H
