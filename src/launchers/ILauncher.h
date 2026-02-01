#ifndef ILAUNCHER_H
#define ILAUNCHER_H

#include <QString>
#include <QList>
#include "core/Game.h"
#include "core/DLSSSettings.h"

class ILauncher {
public:
    virtual ~ILauncher() = default;

    // Launcher identification
    virtual QString name() const = 0;

    // Game discovery
    virtual QList<Game> discoverGames() = 0;

    // Apply settings to launcher configuration (e.g., write to localconfig.vdf)
    virtual bool applySettings(const Game& game, const DLSSSettings& settings) = 0;

    // Get launch command string for clipboard/manual use
    virtual QString getLaunchCommand(const Game& game, const DLSSSettings& settings) = 0;

    // Check if launcher is available on this system
    virtual bool isAvailable() const = 0;
};

#endif // ILAUNCHER_H
