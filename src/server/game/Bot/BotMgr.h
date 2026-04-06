/*
 * This file is part of the FirelandsCore Project. See AUTHORS file for
 * Copyright information.
 *
 * Bot session manager – control layer.
 *
 * Tracks all active bot players that were spawned via ".bot spawn".
 * All methods are called exclusively from the main game thread inside
 * WorldSession::Update() / World::UpdateSessions(), so no mutex is needed.
 */

#ifndef BOTMGR_H
#define BOTMGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>
#include <unordered_map>

class Player;

class FC_GAME_API BotMgr
{
public:
    static BotMgr* instance();

    // Called after a bot player is added to the world (HandlePlayerLogin).
    void RegisterBot(Player* player);

    // Called before a bot player is removed from the world (LogoutPlayer).
    void UnregisterBot(ObjectGuid guid);

    // Returns the bot player with the given name, or nullptr if not found.
    Player* GetBotByName(std::string const& name);

    // Read-only view of all active bots.
    std::unordered_map<ObjectGuid, Player*> const& GetAllBots() const { return m_bots; }

private:
    BotMgr() = default;

    std::unordered_map<ObjectGuid, Player*> m_bots;
};

#define sBotMgr BotMgr::instance()

#endif // BOTMGR_H
