#ifndef DLSSSETTINGS_H
#define DLSSSETTINGS_H

#include <QString>
#include <QJsonObject>
#include <QStringList>

class DLSSSettings {
public:
    DLSSSettings() = default;

    // General
    bool enableNVAPI = true;
    bool enableNGXUpdater = false;

    // Super Resolution (SR)
    bool srOverride = false;
    QString srMode;
    QString srPreset;
    int srScalingRatio = 0;  // 33-100, 0 = default

    // Ray Reconstruction (RR)
    bool rrOverride = false;
    QString rrMode;
    QString rrPreset;
    int rrScalingRatio = 0;

    // Frame Generation (FG)
    bool fgOverride = false;
    int fgMultiFrameCount = 0;  // 0-3

    // DLSS Upgrade
    bool dlssUpgrade = false;
    QString dlssVersion;

    // Indicators
    bool showIndicator = false;

    // Smooth Motion / Frame Rate
    bool enableSmoothMotion = false;
    bool enableFrameRateLimit = false;
    int targetFrameRate = 60;  // FPS limit

    // Executable Selection (user preference)
    QString executablePath;

    // Proton Version Selection
    QString protonVersion;  // Empty/"auto" = latest CachyOS, "latest-ge" = latest GE, or specific version folder name

    // Serialization
    QJsonObject toJson() const;
    static DLSSSettings fromJson(const QJsonObject& json);

    // Available options
    static QStringList availableSRModes();
    static QStringList availableRRModes();
    static QStringList availablePresets();

    bool operator==(const DLSSSettings& other) const;
};

#endif // DLSSSETTINGS_H
