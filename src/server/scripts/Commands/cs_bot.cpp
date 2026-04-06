/*
 * This file is part of the FirelandsCore Project. See AUTHORS file for
 * Copyright information.
 *
 * PoC bot session bootstrap – GM command: .bot spawn <charname>
 *
 * Minimal proof-of-concept only. No AI, no combat, no follow, no quests.
 * Success condition: character is in world and update loop runs without crash.
 */

#include "ScriptMgr.h"
#include "RBAC.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Common.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"

class bot_commandscript : public CommandScript
{
public:
    bot_commandscript() : CommandScript("bot_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> botCommandTable =
        {
            { "spawn", rbac::RBAC_PERM_COMMAND_BOT_SPAWN, true, &HandleBotSpawnCommand, "" },
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "bot", rbac::RBAC_PERM_COMMAND_BOT_SPAWN, true, nullptr, "", botCommandTable },
        };

        return commandTable;
    }

    // .bot spawn <charname>
    //
    // Creates a socket-less bot session for an existing character and loads
    // that character into the world using the standard login path.
    // The character must exist in the database and its account must not be
    // already logged in (AddSession_ will kick any conflicting session).
    static bool HandleBotSpawnCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->SendSysMessage("Usage: .bot spawn <charname>");
            return true;
        }

        std::string charName(args);

        // Trim whitespace
        charName.erase(charName.find_last_not_of(" \t\r\n") + 1);
        charName.erase(0, charName.find_first_not_of(" \t\r\n"));

        if (charName.empty())
        {
            handler->SendSysMessage("Usage: .bot spawn <charname>");
            return true;
        }

        // Normalise to title-case as the DB stores character names.
        // Cast to unsigned char before std::toupper/tolower to avoid UB on
        // negative char values (non-ASCII input).
        charName[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(charName[0])));
        for (std::size_t i = 1; i < charName.size(); ++i)
            charName[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(charName[i])));

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

        // Check that the character is not already in the world as a real player
        if (ObjectAccessor::FindConnectedPlayerByName(charName))
        {
            handler->PSendSysMessage("Character '%s' is already online as a real player.", charName.c_str());
            return true;
        }

        LOG_INFO("bot", "BotSession: GM '%s' requested spawn of bot character '%s' (guid %s, account %u).",
            handler->GetNameLink().c_str(), charName.c_str(), guid.ToString().c_str(), accountId);

        // Create a socket-less WorldSession for the bot.
        // Passing nullptr as the socket is intentional; m_isBotSession prevents
        // the null-socket crash in WorldSession::Update() and keeps the session
        // alive in World::UpdateSessions().
        WorldSession* botSession = new WorldSession(
            accountId,
            "bot_" + charName,  // account name field – used only for display
            nullptr,            // no real network socket
            SEC_PLAYER,
            EXPANSION_CATACLYSM,
            0,                  // no mute
            DEFAULT_LOCALE,
            0,                  // no recruiter
            false);

        botSession->SetBotSession();

        // Queue the character load.  The callback fires inside Update() once
        // the session is registered and UpdateSessions() ticks it.
        botSession->SpawnBotPlayerAsync(guid);

        // Register the session.  AddSession_() will kick any real session that
        // is currently using the same account (same behaviour as a normal login).
        sWorld->AddSession(botSession);

        handler->PSendSysMessage("Bot session created for character '%s'. Watch the log for 'PoC bootstrap complete'.", charName.c_str());
        LOG_INFO("bot", "BotSession: Session queued for character '%s' (account %u).", charName.c_str(), accountId);

        return true;
    }
};

void AddSC_bot_commandscript()
{
    new bot_commandscript();
}
