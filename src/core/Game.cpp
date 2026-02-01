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

bool Game::operator==(const Game& other) const
{
    return m_id == other.m_id && m_launcher == other.m_launcher;
}
