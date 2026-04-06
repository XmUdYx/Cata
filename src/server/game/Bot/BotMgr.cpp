/*
 * This file is part of the FirelandsCore Project. See AUTHORS file for
 * Copyright information.
 *
 * Bot session manager – control layer.
 *
 * All methods run on the main game thread only (inside World::UpdateSessions).
 * No synchronisation required.
 */

#include "BotMgr.h"
#include "Log.h"
#include "Player.h"

BotMgr* BotMgr::instance()
{
    static BotMgr instance;
    return &instance;
}

void BotMgr::RegisterBot(Player* player)
{
    if (!player)
    {
        LOG_ERROR("bot", "BotMgr::RegisterBot called with nullptr.");
        return;
    }

    ObjectGuid guid = player->GetGUID();

    auto [it, inserted] = m_bots.emplace(guid, player);
    if (!inserted)
    {
        LOG_WARN("bot", "BotMgr::RegisterBot: bot %s (%s) is already registered – overwriting.",
            player->GetName().c_str(), guid.ToString().c_str());
        it->second = player;
    }
    else
    {
        LOG_INFO("bot", "BotMgr: Registered bot %s (%s). Active bots: %zu.",
            player->GetName().c_str(), guid.ToString().c_str(), m_bots.size());
    }
}

void BotMgr::UnregisterBot(ObjectGuid guid)
{
    auto it = m_bots.find(guid);
    if (it == m_bots.end())
    {
        LOG_WARN("bot", "BotMgr::UnregisterBot: guid %s not found in registry.",
            guid.ToString().c_str());
        return;
    }

    std::string name = it->second ? it->second->GetName() : "<unknown>";
    m_bots.erase(it);
    LOG_INFO("bot", "BotMgr: Unregistered bot %s (%s). Active bots: %zu.",
        name.c_str(), guid.ToString().c_str(), m_bots.size());
}

Player* BotMgr::GetBotByName(std::string const& name)
{
    for (auto const& [guid, player] : m_bots)
    {
        if (player && player->GetName() == name)
            return player;
    }
    return nullptr;
}
