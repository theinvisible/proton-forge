#include "GogPlayTasks.h"
#include "gog/GogContentClient.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace GogPlayTasks {

namespace {

QStringList stringArray(const QJsonValue& value)
{
    QStringList out;
    for (const QJsonValue& item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            out << text;
        }
    }
    return out;
}

// GOG writes `arguments` as a string in most info files and as an array in
// some. Both mean the same thing, so both are accepted here rather than at
// every call site.
QStringList argumentsOf(const QJsonValue& value)
{
    if (value.isArray()) {
        return stringArray(value);
    }
    return splitArguments(value.toString());
}

} // namespace

Info parseInfoFile(const QByteArray& json)
{
    Info info;

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return info;
    }

    const QJsonObject root = doc.object();
    info.gameId     = root.value("gameId").toString();
    info.rootGameId = root.value("rootGameId").toString();
    info.name       = root.value("name").toString();
    info.buildId    = root.value("buildId").toString();
    info.formatVersion = root.value("version").isDouble()
                             ? QString::number(root.value("version").toInt())
                             : root.value("version").toString();
    info.languages  = stringArray(root.value("languages"));
    info.osBitness  = stringArray(root.value("osBitness"));

    for (const QJsonValue& value : root.value("playTasks").toArray()) {
        const QJsonObject object = value.toObject();

        PlayTask task;
        task.name       = object.value("name").toString();
        task.type       = object.value("type").toString();
        task.category   = object.value("category").toString();
        task.path       = object.value("path").toString();
        task.workingDir = object.value("workingDir").toString();
        task.arguments  = argumentsOf(object.value("arguments"));
        task.languages  = stringArray(object.value("languages"));
        task.osBitness  = stringArray(object.value("osBitness"));
        task.isPrimary  = object.value("isPrimary").toBool();
        task.isHidden   = object.value("isHidden").toBool();
        info.playTasks.append(task);
    }

    // A gameId is what ties this file back to the product being installed. An
    // info file without one describes nothing we can use.
    info.valid = !info.gameId.isEmpty();
    return info;
}

QString infoFileName(const QString& productId)
{
    return QStringLiteral("goggame-%1.info").arg(productId);
}

QList<PlayTask> gameTasks(const Info& info)
{
    QList<PlayTask> tasks;
    for (const PlayTask& task : info.playTasks) {
        // A URLTask is a manual or a forum link — startable, but not a game.
        if (task.isHidden || task.path.isEmpty()) {
            continue;
        }
        if (task.type.compare("FileTask", Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (task.category.compare("game", Qt::CaseInsensitive) != 0) {
            continue;
        }
        tasks.append(task);
    }
    return tasks;
}

PlayTask primaryTask(const Info& info)
{
    const QList<PlayTask> games = gameTasks(info);

    for (const PlayTask& task : games) {
        if (task.isPrimary) {
            return task;
        }
    }
    if (!games.isEmpty()) {
        return games.first();
    }

    // Nothing categorised as a game. Some older info files leave the category
    // off entirely, so a plain FileTask with a path is still better than
    // refusing to launch at all.
    for (const PlayTask& task : info.playTasks) {
        if (!task.isHidden && !task.path.isEmpty()
            && task.type.compare("URLTask", Qt::CaseInsensitive) != 0) {
            return task;
        }
    }
    return {};
}

QStringList splitArguments(const QString& raw)
{
    QStringList out;
    QString current;
    bool inWord = false;
    QChar quote;

    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);

        if (c == '\\' && i + 1 < raw.size()
            && (raw.at(i + 1) == '"' || raw.at(i + 1) == '\'')) {
            current.append(raw.at(i + 1));
            inWord = true;
            ++i;
            continue;
        }

        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            } else {
                current.append(c);
            }
            continue;
        }

        if (c == '"' || c == '\'') {
            // An empty quoted string is still an argument, so entering a quote
            // counts as having started a word.
            quote = c;
            inWord = true;
            continue;
        }

        if (c.isSpace()) {
            if (inWord) {
                out << current;
                current.clear();
                inWord = false;
            }
            continue;
        }

        current.append(c);
        inWord = true;
    }

    if (inWord) {
        out << current;
    }
    return out;
}

QString resolveExecutable(const QString& installPath, const QString& taskPath)
{
    if (installPath.isEmpty()) {
        return QString();
    }
    // Same rules as a depot item: this file came off the network too, and
    // "path": "../../../.bashrc" is a one-line exploit otherwise.
    const QString safe = GogContentClient::sanitizeDepotPath(taskPath);
    if (safe.isEmpty()) {
        return QString();
    }
    return installPath + "/" + safe;
}

QString resolveExecutableOnDisk(const QString& installPath, const QString& taskPath)
{
    const QString exact = resolveExecutable(installPath, taskPath);
    if (exact.isEmpty() || QFileInfo::exists(exact)) {
        return exact;
    }

    // Walk it component by component, matching case-insensitively. Only the
    // parts that do not exist as written get the search, so an install whose
    // case already agrees costs one stat call.
    const QString relative = exact.mid(installPath.size() + 1);
    QString resolved = installPath;
    const QStringList parts = relative.split('/', Qt::SkipEmptyParts);

    for (const QString& part : parts) {
        const QString candidate = resolved + "/" + part;
        if (QFileInfo::exists(candidate)) {
            resolved = candidate;
            continue;
        }

        QString match;
        const QStringList siblings = QDir(resolved).entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QString& sibling : siblings) {
            if (sibling.compare(part, Qt::CaseInsensitive) == 0) {
                match = sibling;
                break;
            }
        }
        if (match.isEmpty()) {
            return QString();
        }
        resolved += "/" + match;
    }

    return resolved;
}

bool looksNativeLinux(const QString& taskPath)
{
    if (taskPath.isEmpty()) {
        return false;
    }
    const QString normalized = QString(taskPath).replace('\\', '/');
    const QString base = normalized.section('/', -1);

    if (base.endsWith(".exe", Qt::CaseInsensitive)
        || base.endsWith(".bat", Qt::CaseInsensitive)
        || base.endsWith(".cmd", Qt::CaseInsensitive)) {
        return false;
    }
    // A .sh wrapper or an extensionless ELF — both are what GOG's Linux builds
    // actually ship as.
    return base.endsWith(".sh", Qt::CaseInsensitive) || !base.contains('.');
}

} // namespace GogPlayTasks
