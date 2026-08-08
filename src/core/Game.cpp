#include "Game.h"

Game::Game(const QString& id, const QString& name, const QString& launcher)
    : m_id(id)
    , m_name(name)
    , m_launcher(launcher)
{
}

QString Game::settingsKey() const
{
    return m_launcher + ":" + m_id;
}

// Empty is a deliberate answer, not a degenerate one.
//
// These used to be built by concatenation at the call site, so a game with no
// library path produced "/compatdata/<id>" — an absolute path at the filesystem
// root. mkpath() then failed, its return value was discarded, and Proton was
// handed a STEAM_COMPAT_DATA_PATH it could not write to. The failure surfaced
// much later and looked like a Proton bug.
//
// Returning nothing lets the launch refuse with a message that names the actual
// problem. Anything that needs a prefix has to treat empty as an error.
QString Game::compatDataPath() const
{
    if (!m_compatDataPath.isEmpty()) {
        return m_compatDataPath;
    }
    if (m_libraryPath.isEmpty()) {
        return QString();
    }
    // Steam's layout: the prefix lives in the same library as the game.
    return m_libraryPath + "/compatdata/" + m_id;
}

QString Game::shaderCachePath() const
{
    if (!m_shaderCachePath.isEmpty()) {
        return m_shaderCachePath;
    }
    if (m_libraryPath.isEmpty()) {
        return QString();
    }
    return m_libraryPath + "/shadercache/" + m_id + "/fozpipelinesv6";
}

bool Game::operator==(const Game& other) const
{
    return m_id == other.m_id && m_launcher == other.m_launcher;
}
