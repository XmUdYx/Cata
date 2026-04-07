/*
 * This file is part of the FirelandsCore Project. See AUTHORS file for
 * Copyright information.
 *
 * Bot session control layer – GM commands: .bot spawn / despawn / move / teleport
 *
 * Phase 1 (bootstrap): .bot spawn
 * Phase 2 (control):   .bot despawn, .bot move
 * Phase 3 (transport): .bot teleport
 *
 * No AI, no combat, no follow, no quests.
 */

#include "BotMgr.h"
#include "Chat.h"
#include "CharacterCache.h"
#include "Common.h"
#include "DBCStores.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static std::string NormaliseName(char const* args)
{
    if (!args || !*args)
        return {};

    std::string name(args);

    // Trim leading/trailing whitespace
    name.erase(name.find_last_not_of(" \t\r\n") + 1);
    name.erase(0, name.find_first_not_of(" \t\r\n"));

    if (name.empty())
        return {};

    // Title-case: first char upper, rest lower.
    // Cast to unsigned char to avoid UB on negative char values (non-ASCII).
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    for (std::size_t i = 1; i < name.size(); ++i)
        name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));

    return name;
}

// ---------------------------------------------------------------------------

class bot_commandscript : public CommandScript
{
public:
    bot_commandscript() : CommandScript("bot_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> botCommandTable =
        {
            { "spawn",    rbac::RBAC_PERM_COMMAND_BOT_SPAWN,    true, &HandleBotSpawnCommand,    "" },
            { "despawn",  rbac::RBAC_PERM_COMMAND_BOT_DESPAWN,  true, &HandleBotDespawnCommand,  "" },
            { "move",     rbac::RBAC_PERM_COMMAND_BOT_MOVE,     true, &HandleBotMoveCommand,     "" },
            { "teleport", rbac::RBAC_PERM_COMMAND_BOT_TELEPORT, true, &HandleBotTeleportCommand, "" },
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "bot", rbac::RBAC_PERM_COMMAND_BOT_SPAWN, true, nullptr, "", botCommandTable },
        };

        return commandTable;
    }

    // -----------------------------------------------------------------------
    // .bot spawn <charname>
    //
    // Creates a socket-less bot session for an existing character and loads
    // that character into the world using the standard login path.
    // -----------------------------------------------------------------------
    static bool HandleBotSpawnCommand(ChatHandler* handler, char const* args)
    {
        std::string charName = NormaliseName(args);
        if (charName.empty())
        {
            handler->SendSysMessage("Usage: .bot spawn <charname>");
            return true;
        }

        ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(charName);
        if (guid.IsEmpty())
        {
            handler->PSendSysMessage("Character '%s' not found in character cache.", charName.c_str());
            return true;
        }

        uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
        if (accountId == 0)
        {
            handler->PSendSysMessage("Could not retrieve account ID for character '%s'.", charName.c_str());
            return true;
        }

        // Reject if the character is already in the world (as a real player or
        // an already-registered bot).
        if (ObjectAccessor::FindConnectedPlayerByName(charName))
        {
            handler->PSendSysMessage("Character '%s' is already online as a real player.", charName.c_str());
            return true;
        }

        if (sBotMgr->GetBotByName(charName))
        {
            handler->PSendSysMessage("Bot '%s' is already spawned.", charName.c_str());
            return true;
        }

        LOG_INFO("bot", "BotSession: GM '%s' requested spawn of bot character '%s' (guid %s, account %u).",
            handler->GetNameLink().c_str(), charName.c_str(), guid.ToString().c_str(), accountId);

        // Create a socket-less WorldSession for the bot.
        WorldSession* botSession = new WorldSession(
            accountId,
            "bot_" + charName,  // account name – display only
            nullptr,            // no real network socket
            SEC_PLAYER,
            EXPANSION_CATACLYSM,
            0,                  // no mute
            DEFAULT_LOCALE,
            0,                  // no recruiter
            false);

        botSession->SetBotSession();
        botSession->SpawnBotPlayerAsync(guid);
        sWorld->AddSession(botSession);

        handler->PSendSysMessage("Bot session created for '%s'. Watch the log for 'PoC bootstrap complete'.", charName.c_str());
        return true;
    }

    // -----------------------------------------------------------------------
    // .bot despawn <charname>
    //
    // Cleanly removes a bot from the world and terminates its session so that
    // World::UpdateSessions() will erase it on the next tick.
    // -----------------------------------------------------------------------
    static bool HandleBotDespawnCommand(ChatHandler* handler, char const* args)
    {
        std::string charName = NormaliseName(args);
        if (charName.empty())
        {
            handler->SendSysMessage("Usage: .bot despawn <charname>");
            return true;
        }

        Player* bot = sBotMgr->GetBotByName(charName);
        if (!bot)
        {
            handler->PSendSysMessage("No active bot named '%s'.", charName.c_str());
            return true;
        }

        WorldSession* botSession = bot->GetSession();
        if (!botSession || !botSession->IsBotSession())
        {
            handler->PSendSysMessage("Session for '%s' is not a bot session – aborting.", charName.c_str());
            return true;
        }

        LOG_INFO("bot", "BotMgr: GM '%s' is despawning bot '%s'.",
            handler->GetNameLink().c_str(), charName.c_str());

        // KillBotSession() calls LogoutPlayer(false) which:
        //   1. Removes the player from the map (deletes Player object).
        //   2. Triggers UnregisterBot() via our hook before SetPlayer(nullptr).
        // It then clears m_isBotSession so UpdateSessions() erases the session.
        botSession->KillBotSession();

        handler->PSendSysMessage("Bot '%s' despawned.", charName.c_str());
        return true;
    }

    // -----------------------------------------------------------------------
    // .bot move <charname> <x> <y> <z> [<o>]
    //
    // Repositions a bot on its current map via Player::BotRelocate (server-side
    // only, no pathfinding, no client packets).
    // -----------------------------------------------------------------------
    static bool HandleBotMoveCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->SendSysMessage("Usage: .bot move <charname> <x> <y> <z> [<o>]");
            return true;
        }

        // Split the argument string into tokens so the name and coordinates
        // are parsed independently – avoids sscanf format-string pitfalls.
        Tokenizer tokens(std::string(args), ' ');
        if (tokens.size() < 4)
        {
            handler->SendSysMessage("Usage: .bot move <charname> <x> <y> <z> [<o>]");
            return true;
        }

        std::string charName = NormaliseName(tokens[0]);
        if (charName.empty())
        {
            handler->SendSysMessage("Usage: .bot move <charname> <x> <y> <z> [<o>]");
            return true;
        }

        float x, y, z, o = 0.0f;
        try
        {
            x = std::stof(tokens[1]);
            y = std::stof(tokens[2]);
            z = std::stof(tokens[3]);
            if (tokens.size() >= 5)
                o = std::stof(tokens[4]);
        }
        catch (std::exception const&)
        {
            handler->SendSysMessage("Invalid coordinates. Usage: .bot move <charname> <x> <y> <z> [<o>]");
            return true;
        }

        Player* bot = sBotMgr->GetBotByName(charName);
        if (!bot)
        {
            handler->PSendSysMessage("No active bot named '%s'.", charName.c_str());
            return true;
        }

        // .bot move is same-map only. Verify the invoker is on the same map as
        // the bot before relocating, so cross-map coordinates are rejected early.
        Player* gmPlayer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!gmPlayer)
        {
            handler->SendSysMessage(".bot move requires an in-game GM (no console support).");
            return true;
        }

        if (gmPlayer->GetMapId() != bot->GetMapId())
        {
            handler->PSendSysMessage(
                "Cannot move bot '%s': bot is on map %u but you are on map %u. "
                ".bot move is same-map only.",
                charName.c_str(), bot->GetMapId(), gmPlayer->GetMapId());
            LOG_WARN("bot", "BotMgr: GM '%s' attempted cross-map move of bot '%s' "
                "(GM map %u -> bot map %u) – rejected.",
                handler->GetNameLink().c_str(), charName.c_str(),
                gmPlayer->GetMapId(), bot->GetMapId());
            return true;
        }

        LOG_INFO("bot", "BotMgr: GM '%s' moving bot '%s' to (%.2f, %.2f, %.2f, o=%.2f).",
            handler->GetNameLink().c_str(), charName.c_str(), x, y, z, o);

        bot->BotRelocate(Position(x, y, z, o));

        handler->PSendSysMessage("Bot '%s' moved to (%.2f, %.2f, %.2f).", charName.c_str(), x, y, z);
        return true;
    }

    // -----------------------------------------------------------------------
    // .bot teleport <charname> <mapId> <x> <y> <z> [<o>]
    //
    // Teleports a bot to the given coordinates, which may be on a different
    // map.  Same-map destinations are handled via BotRelocate; cross-map
    // destinations go through the standard TeleportTo / HandleMoveWorldportAck
    // path synchronously.
    // -----------------------------------------------------------------------
    static bool HandleBotTeleportCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->SendSysMessage("Usage: .bot teleport <charname> <mapId> <x> <y> <z> [<o>]");
            return true;
        }

        Tokenizer tokens(std::string(args), ' ');
        if (tokens.size() < 5)
        {
            handler->SendSysMessage("Usage: .bot teleport <charname> <mapId> <x> <y> <z> [<o>]");
            return true;
        }

        std::string charName = NormaliseName(tokens[0]);
        if (charName.empty())
        {
            handler->SendSysMessage("Usage: .bot teleport <charname> <mapId> <x> <y> <z> [<o>]");
            return true;
        }

        uint32 mapId;
        float x, y, z, o = 0.0f;
        try
        {
            mapId = static_cast<uint32>(std::stoul(tokens[1]));
            x = std::stof(tokens[2]);
            y = std::stof(tokens[3]);
            z = std::stof(tokens[4]);
            if (tokens.size() >= 6)
                o = std::stof(tokens[5]);
        }
        catch (std::exception const&)
        {
            handler->SendSysMessage("Invalid arguments. Usage: .bot teleport <charname> <mapId> <x> <y> <z> [<o>]");
            return true;
        }

        MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
        if (!mapEntry)
        {
            handler->PSendSysMessage("Unknown map %u.", mapId);
            return true;
        }

        if (mapEntry->IsBattlegroundOrArena())
        {
            handler->PSendSysMessage("Battleground/arena maps are not supported for bot teleport.");
            return true;
        }

        if (mapEntry->IsDungeon())
        {
            handler->PSendSysMessage("Instance maps are not supported for bot teleport.");
            return true;
        }

        if (!MapManager::IsValidMapCoord(mapId, x, y, z, o))
        {
            handler->PSendSysMessage("Invalid coordinates for map %u.", mapId);
            return true;
        }

        Player* bot = sBotMgr->GetBotByName(charName);
        if (!bot)
        {
            handler->PSendSysMessage("No active bot named '%s'.", charName.c_str());
            return true;
        }

        LOG_INFO("bot", "BotMgr: GM '%s' teleporting bot '%s' to map %u (%.2f, %.2f, %.2f, o=%.2f).",
            handler->GetNameLink().c_str(), charName.c_str(), mapId, x, y, z, o);

        if (bot->BotTeleport(mapId, x, y, z, o))
            handler->PSendSysMessage("Bot '%s' teleported to map %u (%.2f, %.2f, %.2f).", charName.c_str(), mapId, x, y, z);
        else
            handler->PSendSysMessage("Teleport failed for bot '%s'. See server log for details.", charName.c_str());

        return true;
    }
};

void AddSC_bot_commandscript()
{
    new bot_commandscript();
}

