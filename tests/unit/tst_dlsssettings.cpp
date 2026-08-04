// DLSSSettings is the value object everything else is written in terms of, and
// it is persisted as JSON. Two things can go wrong quietly when a field is
// added: fromJson forgets it (the setting resets on restart) or operator==
// forgets it (the UI stops noticing the change). Both are checked here
// generically, so a new field is covered without touching this file.

#include <QTest>
#include <QJsonObject>
#include <QJsonDocument>

#include "core/DLSSSettings.h"

class TstDlssSettings : public QObject
{
    Q_OBJECT

private slots:
    void defaultsThatAreNotFalse();
    void jsonRoundTrip();
    void everyFieldSurvivesJson();
    void missingKeysFallBackToDefaults();
    void vkd3dConfigExtraIsOmittedWhenEmpty();
    void equalityCoversEveryField();
    void availableOptionsAreNonEmpty();

private:
    // A settings object with every field moved away from its default, built
    // through JSON so it needs no maintenance when a field is added.
    static QJsonObject mutatedJson()
    {
        QJsonObject json = DLSSSettings().toJson();
        // vkd3dConfigExtra is dropped by toJson when empty; put it back so the
        // "every field" checks below cover it too.
        json["vkd3dConfigExtra"] = QString("dxr");

        for (const QString& key : json.keys()) {
            const QJsonValue value = json.value(key);
            if (value.isBool()) {
                json[key] = !value.toBool();
            } else if (value.isDouble()) {
                json[key] = value.toInt() + 7;
            } else if (value.isString()) {
                json[key] = value.toString() + "-changed";
            }
        }
        return json;
    }
};

void TstDlssSettings::defaultsThatAreNotFalse()
{
    // Three defaults are deliberately not the zero value. Getting one of them
    // wrong is invisible in the UI but changes what every new game does.
    const DLSSSettings settings;
    QVERIFY(settings.enableNVAPI);
    QVERIFY(settings.enableSteamOverlay);
    QCOMPARE(settings.targetFrameRate, 60);

    const DLSSSettings fromEmpty = DLSSSettings::fromJson(QJsonObject());
    QVERIFY(fromEmpty.enableNVAPI);
    QVERIFY(fromEmpty.enableSteamOverlay);
    QCOMPARE(fromEmpty.targetFrameRate, 60);
}

void TstDlssSettings::jsonRoundTrip()
{
    DLSSSettings settings;
    settings.srOverride = true;
    settings.srMode = "Quality";
    settings.srPreset = "Preset E";
    settings.srScalingRatio = 66;
    settings.fgOverride = true;
    settings.fgMultiFrameCount = 3;
    settings.fgMode = "Auto";
    settings.enableProtonHDR = true;
    settings.protonVersion = "proton-cachyos-11.0-20260703-slr-x86_64";
    settings.executablePath = "/games/thing/thing.exe";
    settings.customLaunchParams = "FOO=1 %command% -novid";
    settings.vkd3dConfigExtra = "dxr";
    settings.targetFrameRate = 144;

    const DLSSSettings back = DLSSSettings::fromJson(settings.toJson());
    QVERIFY2(back == settings, qPrintable(QString::fromUtf8(
        QJsonDocument(settings.toJson()).toJson(QJsonDocument::Compact))));
}

void TstDlssSettings::everyFieldSurvivesJson()
{
    // Catches a field that toJson writes but fromJson never reads.
    const DLSSSettings mutated = DLSSSettings::fromJson(mutatedJson());
    const QJsonObject written = mutated.toJson();
    const QJsonObject expected = mutatedJson();

    for (const QString& key : expected.keys()) {
        QVERIFY2(written.contains(key),
                 qPrintable("toJson dropped '" + key + "' — fromJson probably never read it"));
        QVERIFY2(written.value(key) == expected.value(key),
                 qPrintable("'" + key + "' did not survive the JSON round trip"));
    }
}

void TstDlssSettings::missingKeysFallBackToDefaults()
{
    // An old settings.json missing a newly added key must not produce garbage.
    QJsonObject partial;
    partial["srOverride"] = true;
    partial["srMode"] = "Balanced";

    const DLSSSettings settings = DLSSSettings::fromJson(partial);
    QVERIFY(settings.srOverride);
    QCOMPARE(settings.srMode, QString("Balanced"));
    // Everything else is at its default.
    QCOMPARE(settings.targetFrameRate, 60);
    QVERIFY(settings.enableNVAPI);
    QVERIFY(!settings.fgOverride);
    QVERIFY(settings.rrMode.isEmpty());
}

void TstDlssSettings::vkd3dConfigExtraIsOmittedWhenEmpty()
{
    // Documented asymmetry: toJson skips the key entirely when it is empty, so
    // toJson/fromJson is not key-for-key symmetric even though it round-trips.
    const DLSSSettings empty;
    QVERIFY(!empty.toJson().contains("vkd3dConfigExtra"));

    DLSSSettings withExtra;
    withExtra.vkd3dConfigExtra = "dxr";
    QCOMPARE(withExtra.toJson().value("vkd3dConfigExtra").toString(), QString("dxr"));

    QVERIFY(DLSSSettings::fromJson(empty.toJson()) == empty);
}

void TstDlssSettings::equalityCoversEveryField()
{
    // Flip one field at a time and demand operator== notices. A field it
    // forgets means the UI's "settings changed" checks silently stop firing.
    const DLSSSettings base;
    const QJsonObject baseJson = mutatedJson();

    for (const QString& key : baseJson.keys()) {
        QJsonObject one = base.toJson();
        one[key] = baseJson.value(key);
        const DLSSSettings changed = DLSSSettings::fromJson(one);

        // Skip keys the mutation happened to leave identical.
        if (changed == base && one.value(key) == base.toJson().value(key)) {
            continue;
        }
        QVERIFY2(!(changed == base),
                 qPrintable("operator== ignores '" + key + "'"));
    }
}

void TstDlssSettings::availableOptionsAreNonEmpty()
{
    // These populate the combo boxes; an empty one is a dead control.
    QVERIFY(!DLSSSettings::availableSRModes().isEmpty());
    QVERIFY(!DLSSSettings::availableRRModes().isEmpty());
    QVERIFY(!DLSSSettings::availablePresets().isEmpty());
    QVERIFY(!DLSSSettings::availableFGModes().isEmpty());
    QVERIFY(!DLSSSettings::availableFGPresets().isEmpty());
}

QTEST_MAIN(TstDlssSettings)
#include "tst_dlsssettings.moc"
