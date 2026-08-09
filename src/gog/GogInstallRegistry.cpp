#include "GogInstallRegistry.h"
#include "core/SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

QStringList stringArray(const QJsonValue& value)
{
    QStringList out;
    for (const QJsonValue& item : value.toArray()) {
        const QString text = item.toString();
        if (!text.isEmpty()) {
            out << text;
        }
    }
    return out;
}

QDateTime dateOrNull(const QJsonValue& value)
{
    const QString text = value.toString();
    return text.isEmpty() ? QDateTime() : QDateTime::fromString(text, Qt::ISODate);
}

} // namespace

GogInstallRegistry& GogInstallRegistry::instance()
{
    static GogInstallRegistry registry;
    return registry;
}

QString GogInstallRegistry::filePath()
{
    return SettingsManager::configDir() + "/gog-installs.json";
}

QString GogInstallRegistry::manifestPath(const QString& productId)
{
    return SettingsManager::configDir() + "/gog-manifests/" + productId + ".json";
}

QString GogInstallRegistry::defaultInstallRoot()
{
    // Visible user data, not a dotdir — these are tens of gigabytes the user
    // will want to find, move and back up.
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return home + "/Games/ProtonForge";
}

QString GogInstallRegistry::installRoot()
{
    const QString configured = QSettings().value("gog/installRoot").toString();
    return configured.isEmpty() ? defaultInstallRoot() : configured;
}

void GogInstallRegistry::setInstallRoot(const QString& path)
{
    QSettings settings;
    if (path.isEmpty() || path == defaultInstallRoot()) {
        settings.remove("gog/installRoot");
    } else {
        settings.setValue("gog/installRoot", path);
    }
}

QString GogInstallRegistry::storeDirectory(const QString& root)
{
    return (root.isEmpty() ? installRoot() : root) + "/GOG";
}

QString GogInstallRegistry::prefixPathFor(const QString& productId, const QString& root)
{
    return (root.isEmpty() ? installRoot() : root) + "/prefixes/GOG/" + productId;
}

QList<GogInstallRegistry::Entry> GogInstallRegistry::parse(const QByteArray& json)
{
    QList<Entry> entries;

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return entries;
    }

    for (const QJsonValue& value : doc.object().value("installs").toArray()) {
        const QJsonObject object = value.toObject();

        Entry entry;
        entry.productId        = object.value("productId").toString();
        entry.title            = object.value("title").toString();
        entry.installPath      = object.value("installPath").toString();
        entry.buildId          = object.value("buildId").toString();
        entry.versionName      = object.value("versionName").toString();
        entry.platform         = object.value("platform").toString();
        entry.languages        = stringArray(object.value("languages"));
        entry.dlcIds           = stringArray(object.value("dlcIds"));
        entry.size             = static_cast<qint64>(object.value("size").toDouble());
        entry.installedAt      = dateOrNull(object.value("installedAt"));
        entry.updatedAt        = dateOrNull(object.value("updatedAt"));
        entry.complete         = object.value("complete").toBool();
        entry.executablePath   = object.value("executablePath").toString();
        entry.workingDirectory = object.value("workingDirectory").toString();
        entry.launchArgs       = stringArray(object.value("launchArgs"));
        entry.nativeLinux      = object.value("nativeLinux").toBool();
        entry.warnings         = stringArray(object.value("warnings"));
        entry.latestBuildId    = object.value("latestBuildId").toString();
        entry.latestCheckedAt  = dateOrNull(object.value("latestCheckedAt"));
        // Absent in every file written before artwork was recorded, and that
        // absence is exactly the "look it up" state — so it is not defaulted
        // to anything.
        entry.imageUrl         = object.value("imageUrl").toString();

        // Without these two there is nothing to launch and nothing to delete,
        // so such a row is dropped rather than half-honoured.
        entry.valid = !entry.productId.isEmpty() && !entry.installPath.isEmpty();
        if (entry.valid) {
            entries.append(entry);
        }
    }

    return entries;
}

QByteArray GogInstallRegistry::serialize(const QList<Entry>& entries)
{
    QJsonArray array;
    for (const Entry& entry : entries) {
        QJsonObject object;
        object["productId"]        = entry.productId;
        object["title"]            = entry.title;
        object["installPath"]      = entry.installPath;
        object["buildId"]          = entry.buildId;
        object["versionName"]      = entry.versionName;
        object["platform"]         = entry.platform;
        object["languages"]        = QJsonArray::fromStringList(entry.languages);
        object["dlcIds"]           = QJsonArray::fromStringList(entry.dlcIds);
        object["size"]             = static_cast<double>(entry.size);
        object["complete"]         = entry.complete;
        object["executablePath"]   = entry.executablePath;
        object["workingDirectory"] = entry.workingDirectory;
        object["launchArgs"]       = QJsonArray::fromStringList(entry.launchArgs);
        object["nativeLinux"]      = entry.nativeLinux;
        object["warnings"]         = QJsonArray::fromStringList(entry.warnings);
        if (entry.installedAt.isValid()) {
            object["installedAt"] = entry.installedAt.toString(Qt::ISODate);
        }
        if (entry.updatedAt.isValid()) {
            object["updatedAt"] = entry.updatedAt.toString(Qt::ISODate);
        }
        if (!entry.latestBuildId.isEmpty()) {
            object["latestBuildId"] = entry.latestBuildId;
        }
        if (entry.latestCheckedAt.isValid()) {
            object["latestCheckedAt"] = entry.latestCheckedAt.toString(Qt::ISODate);
        }
        if (!entry.imageUrl.isEmpty()) {
            object["imageUrl"] = entry.imageUrl;
        }
        array.append(object);
    }

    QJsonObject root;
    root["version"]  = 1;
    root["installs"] = array;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool GogInstallRegistry::hasUpdate(const Entry& entry)
{
    // Both halves have to be known. An unchecked entry is not "up to date" and
    // it is not "outdated" either — it is unknown, and claiming an update is
    // available on no evidence would send the user into a pointless re-download.
    if (!entry.complete || entry.buildId.isEmpty() || entry.latestBuildId.isEmpty()) {
        return false;
    }
    return entry.buildId != entry.latestBuildId;
}

void GogInstallRegistry::load()
{
    QMutexLocker locker(&m_mutex);
    m_entries.clear();
    m_loaded = true;

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;   // no installs yet is the normal state, not an error
    }
    m_entries = parse(file.readAll());
}

bool GogInstallRegistry::save()
{
    QMutexLocker locker(&m_mutex);
    QDir().mkpath(SettingsManager::configDir());

    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(serialize(m_entries));
    return file.commit();
}

QList<GogInstallRegistry::Entry> GogInstallRegistry::entries() const
{
    QMutexLocker locker(&m_mutex);
    return m_entries;
}

bool GogInstallRegistry::isEmpty() const
{
    QMutexLocker locker(&m_mutex);
    return m_entries.isEmpty();
}

QList<GogInstallRegistry::Entry> GogInstallRegistry::completeEntries() const
{
    QMutexLocker locker(&m_mutex);
    QList<Entry> out;
    for (const Entry& entry : m_entries) {
        if (entry.complete) {
            out.append(entry);
        }
    }
    return out;
}

GogInstallRegistry::Entry GogInstallRegistry::entry(const QString& productId) const
{
    QMutexLocker locker(&m_mutex);
    for (const Entry& candidate : m_entries) {
        if (candidate.productId == productId) {
            return candidate;
        }
    }
    return {};
}

bool GogInstallRegistry::contains(const QString& productId) const
{
    QMutexLocker locker(&m_mutex);
    return entry(productId).valid;
}

bool GogInstallRegistry::put(Entry entry)
{
    QMutexLocker locker(&m_mutex);
    if (entry.productId.isEmpty() || entry.installPath.isEmpty()) {
        return false;
    }
    entry.valid = true;

    const QDateTime now = QDateTime::currentDateTime();
    entry.updatedAt = now;

    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).productId == entry.productId) {
            // First install wins the installedAt; an update must not rewrite it.
            if (!entry.installedAt.isValid()) {
                entry.installedAt = m_entries.at(i).installedAt;
            }
            m_entries[i] = entry;
            return save();
        }
    }

    if (!entry.installedAt.isValid()) {
        entry.installedAt = now;
    }
    m_entries.append(entry);
    return save();
}

bool GogInstallRegistry::remove(const QString& productId)
{
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).productId == productId) {
            m_entries.removeAt(i);
            // The manifest describes files that are about to stop existing.
            QFile::remove(manifestPath(productId));
            return save();
        }
    }
    return false;
}

bool GogInstallRegistry::setImageUrl(const QString& productId, const QString& imageUrl)
{
    if (imageUrl.isEmpty()) {
        return false;
    }
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).productId != productId) {
            continue;
        }
        if (m_entries.at(i).imageUrl == imageUrl) {
            return false;   // nothing changed, so nothing to write and nothing to repaint
        }
        m_entries[i].imageUrl = imageUrl;
        return save();
    }
    return false;
}

bool GogInstallRegistry::setLatestBuild(const QString& productId, const QString& buildId)
{
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).productId != productId) {
            continue;
        }
        if (m_entries.at(i).latestBuildId == buildId) {
            return true;   // nothing changed, so nothing to write
        }
        m_entries[i].latestBuildId = buildId;
        m_entries[i].latestCheckedAt = QDateTime::currentDateTime();
        return save();
    }
    return false;
}
