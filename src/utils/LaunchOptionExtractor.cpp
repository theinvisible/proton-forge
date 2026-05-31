#include "LaunchOptionExtractor.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace {
// Env-var name prefixes that strongly indicate a real launch option, used to
// keep prose assignments (e.g. "GPU=NVIDIA" in a sentence) out of the results.
const QStringList kEnvPrefixes = {
    "PROTON_", "DXVK_", "VKD3D_", "VKD3D", "WINE", "WINEDLLOVERRIDES", "MANGOHUD",
    "RADV_", "MESA_", "NV_", "__GL_", "__NV_", "VK_", "DRI_", "NVPRESENT_",
    "ENABLE_HDR", "LD_PRELOAD", "DXVK_NVAPI_", "DXVK_FRAME_RATE", "STEAM_",
    "GAMEMODE", "OBS_", "SDL_", "PULSE_", "WAYLAND", "DISPLAY", "DXIL_", "RADEON_"
};

bool looksLikeLaunchEnvKey(const QString& key, bool noteHasCommand)
{
    for (const QString& prefix : kEnvPrefixes) {
        if (key.startsWith(prefix)) {
            return true;
        }
    }
    // If the surrounding note is clearly a launch string (has %command%), be more
    // permissive: any all-caps KEY= token is plausibly an env var.
    return noteHasCommand;
}

QString collapseWhitespace(const QString& s)
{
    static const QRegularExpression ws("\\s+");
    return s.simplified().replace(ws, " ");
}

QString sourceLabel(const ProtonDBClient::Report& r)
{
    QStringList parts;
    if (!r.protonVersion.isEmpty()) {
        parts << "Proton " + r.protonVersion;
    }
    if (!r.gpuDriver.isEmpty()) {
        parts << "driver " + r.gpuDriver;
    }
    return parts.join(" · ");
}

struct Agg {
    int occurrences = 0;
    bool hasCommand = false;
    QString source;
};
} // namespace

QList<LaunchOptionExtractor::Suggestion>
LaunchOptionExtractor::extract(const QList<ProtonDBClient::Report>& reports, int maxResults)
{
    // Matches an environment-variable assignment: UPPER_SNAKE=value (value runs
    // until whitespace). Anchored so it doesn't match mid-identifier.
    static const QRegularExpression envAssign(
        "(?:^|\\s)([A-Z_][A-Z0-9_]{2,})=([^\\s]+)");
    // Matches known wrapper commands as standalone words.
    static const QRegularExpression wrapper(
        "\\b(gamemoderun|mangohud|gamescope)\\b", QRegularExpression::CaseInsensitiveOption);

    QHash<QString, Agg> agg;

    auto bump = [&](const QString& snippetIn, bool hasCommand, const ProtonDBClient::Report& r) {
        const QString snippet = snippetIn.trimmed();
        if (snippet.isEmpty() || snippet.length() > 400) {
            return;
        }
        Agg& a = agg[snippet];
        a.occurrences += 1;
        a.hasCommand = a.hasCommand || hasCommand;
        if (a.source.isEmpty()) {
            a.source = sourceLabel(r);
        }
    };

    for (const ProtonDBClient::Report& report : reports) {
        const QString notes = report.notes;
        const bool noteHasCommand = notes.contains("%command%");

        // 1) Whole launch-option lines containing %command% (highest value).
        const QStringList lines = notes.split('\n');
        for (const QString& line : lines) {
            if (line.contains("%command%")) {
                bump(collapseWhitespace(line), true, report);
            }
        }

        // 2) Environment-variable assignments anywhere in the note.
        auto it = envAssign.globalMatch(notes);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            const QString key = m.captured(1);
            const QString value = m.captured(2);
            if (looksLikeLaunchEnvKey(key, noteHasCommand)) {
                bump(key + "=" + value, false, report);
            }
        }

        // 3) Wrapper commands.
        auto wit = wrapper.globalMatch(notes);
        while (wit.hasNext()) {
            QRegularExpressionMatch m = wit.next();
            bump(m.captured(1).toLower(), false, report);
        }
    }

    QList<Suggestion> suggestions;
    suggestions.reserve(agg.size());
    for (auto it = agg.constBegin(); it != agg.constEnd(); ++it) {
        Suggestion s;
        s.snippet = it.key();
        s.occurrences = it.value().occurrences;
        s.hasCommand = it.value().hasCommand;
        s.sampleSource = it.value().source;
        suggestions << s;
    }

    // Rank: full %command% lines first, then by frequency, then alphabetically.
    std::sort(suggestions.begin(), suggestions.end(),
              [](const Suggestion& a, const Suggestion& b) {
        if (a.hasCommand != b.hasCommand) {
            return a.hasCommand;
        }
        if (a.occurrences != b.occurrences) {
            return a.occurrences > b.occurrences;
        }
        return a.snippet < b.snippet;
    });

    if (suggestions.size() > maxResults) {
        suggestions = suggestions.mid(0, maxResults);
    }
    return suggestions;
}
