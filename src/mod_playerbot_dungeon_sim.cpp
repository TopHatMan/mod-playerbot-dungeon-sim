/*
 * mod-playerbot-dungeon-sim
 * AzerothCore module skeleton.
 * Forms faction/guild-aware bot dungeon runs, creates live groups for online bots,
 * teleports them to dungeon starts, records timed runs, and resolves with simulation fallback.
 */

#include "ScriptMgr.h"
#include "WorldScript.h"
#include "PlayerScript.h"
#include "World.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "Random.h"
#include "Log.h"
#include "DisableMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"
#include "MapMgr.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Chat.h"
#include "Item.h"
#include "ItemTemplate.h"

#include <algorithm>
#include <vector>
#include <unordered_set>
#include <cctype>
#include <string>

namespace PlayerbotDungeonSim
{
    enum RunState : uint8
    {
        RUN_FORMING = 0,
        RUN_ACTIVE = 1,
        RUN_COMPLETED = 2,
        RUN_FAILED = 3,
        RUN_REAL_ATTEMPT = 4
    };

    enum RunResult : uint8
    {
        RESULT_PENDING = 0,
        RESULT_FULL_CLEAR = 1,
        RESULT_PARTIAL = 2,
        RESULT_WIPE = 3,
        RESULT_ABANDONED = 4
    };

    enum Role : uint8
    {
        ROLE_UNKNOWN = 0,
        ROLE_TANK = 1,
        ROLE_HEALER = 2,
        ROLE_DPS = 3
    };

    struct DungeonTemplate
    {
        uint32 Id = 0;
        std::string Name;
        uint32 MapId = 0;
        uint8 Difficulty = 0;
        uint8 MinLevel = 1;
        uint8 MaxLevel = 60;
        uint8 TargetLevel = 1;
        uint8 GroupSize = 5;
        bool IsRaid = false;
        uint32 EntranceMap = 0;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float O = 0.0f;
    };

    struct BotCandidate
    {
        uint32 GuidLow = 0;
        std::string Name;
        uint8 Level = 1;
        uint8 Race = 0;
        uint8 Class = 0;
        uint8 Team = TEAM_ALLIANCE;
        uint32 GuildId = 0;
        Role PreferredRole = ROLE_DPS;
    };

    static bool Enable;
    static bool Debug;
    static uint32 TickSeconds;
    static uint8 MinBotLevel;
    static uint32 MaxGroupsPerTick;
    static bool PreferGuildGroups;
    static bool AllowPugs;
    static bool RequireFullGroup;
    static uint32 MinDurationMinutes;
    static uint32 MaxDurationMinutes;
    static bool RealRunFirst;
    static bool SimFallback;
    static bool TeleportOnlineMembers;
    static bool CreateRealGroups;
    static bool TeleportOnlyIfOnline;
    static bool ManualTravelForPlayerJoinedRuns;
    static bool AwardLoot;
    static bool GiveLootToOnlinePlayers;
    static bool EquipUsableLootOnline;
    static bool DeliverPendingLootOnLogin;
    static uint32 MaxPendingDeliveriesPerLogin;
    static bool EquipUpgradesOnDelivery;
    static bool OnlyEquipBetterItems;
    static bool InviteRealPlayers;
    static bool InviteOnlyIfGroupShort;
    static bool RequirePlayerPartyless;
    static bool RequirePlayerSameFaction;
    static bool RequirePlayerLevelRange;
    static uint32 InviteTimeoutSeconds;
    static uint32 MaxRealPlayerInvitesPerRun;
    static bool BotOnlyEligibilityFilter;
    static std::string BotAccountPrefix;
    static uint32 BotAccountMin;
    static uint32 BotAccountMax;
    static bool RaidMode;
    static uint32 RaidMinServerLevelCap;
    static uint32 RaidChanceAtCap;
    static bool RaidWaiveBotAttunement;
    static bool RaidUseProgressMemory;
    static bool RaidLockouts;
    static uint32 RaidLockoutDays;
    static bool NormalDungeonLockouts;
    static bool RespectProgressionPhase;
    static uint32 ProgressionPhase;
    static uint32 LootPerBossMin;
    static uint32 LootPerBossMax;
    static bool ConsoleStatus;
    static uint32 ConsoleStatusSeconds;
    static bool GuildChatSimulation;
    static uint32 GuildChatChance;
    static bool GuildChatToOnlineMembers;
    static bool GeneralChatSimulation;
    static uint32 GeneralChatChance;
    static bool LocalZoneChatSimulation;
    static uint32 LocalZoneChatChance;
    static uint32 SimulatedBotDeclineChance;
    static bool OrganicLfgActing;
    static uint32 OrganicLfgChance;
    static bool RestrictOrganicCitiesToClassic;
    static bool TradeChatSimulation;
    static uint32 TradeChatChance;
    static uint32 TradeChatEveryTicks;
    static uint32 _tradeTickCounter = 0;

    static uint32 _timerMs = 0;
    static uint32 _statusTimerMs = 0;

    static uint32 GetRunGuildId(std::vector<BotCandidate> const& group);

    static void DebugLog(std::string const& msg)
    {
        if (Debug)
            LOG_INFO("module", "[PlayerbotDungeonSim] {}", msg);
    }

    static void StatusLog(std::string const& msg)
    {
        LOG_INFO("module", "[PlayerbotDungeonSim] {}", msg);
    }

    static std::string TeamName(uint8 team)
    {
        return team == TEAM_HORDE ? "Horde" : "Alliance";
    }

    static std::string GetDungeonTemplateName(uint32 dungeonTemplateId)
    {
        if (QueryResult result = WorldDatabase.Query("SELECT name FROM playerbot_dungeon_template WHERE id = {} LIMIT 1", dungeonTemplateId))
            return result->Fetch()[0].Get<std::string>();
        return "DungeonTemplate#" + std::to_string(dungeonTemplateId);
    }

    static uint8 GetEffectiveServerLevelCap()
    {
        uint32 cap = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        if (cap == 0 || cap > 255)
            cap = 80;
        return uint8(std::min<uint32>(cap, 255));
    }

    static bool IsRaidAllowedByServerCap(DungeonTemplate const& dungeon)
    {
        if (!dungeon.IsRaid)
            return true;

        if (!RaidMode)
            return false;

        uint8 cap = GetEffectiveServerLevelCap();
        if (cap < RaidMinServerLevelCap)
            return false;

        // Example: if the realm is capped at 40, Molten Core min_level 60 is ignored.
        return cap >= dungeon.MinLevel;
    }

    static uint8 GetTeamFromRace(uint8 race)
    {
        switch (race)
        {
            case RACE_HUMAN:
            case RACE_DWARF:
            case RACE_NIGHTELF:
            case RACE_GNOME:
            case RACE_DRAENEI:
                return TEAM_ALLIANCE;
            default:
                return TEAM_HORDE;
        }
    }

    static Role GuessRole(uint8 cls)
    {
        switch (cls)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_DRUID:
                return urand(0, 100) < 30 ? ROLE_TANK : ROLE_DPS;
            case CLASS_PRIEST:
            case CLASS_SHAMAN:
                return urand(0, 100) < 55 ? ROLE_HEALER : ROLE_DPS;
            default:
                return ROLE_DPS;
        }
    }

    static bool IsDungeonAllowed(uint32 mapId)
    {
        // AzerothCore disables table support. If the map is disabled, skip it.
        return !DisableMgr::IsDisabledFor(DISABLE_TYPE_MAP, mapId, nullptr);
    }

    static std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static bool AccountMatchesBotFilter(uint32 accountId)
    {
        if (!BotOnlyEligibilityFilter)
            return true;

        bool hasPrefix = !BotAccountPrefix.empty();
        bool hasRange = BotAccountMax >= BotAccountMin && BotAccountMax > 0;

        if (!hasPrefix && !hasRange)
            return false;

        if (hasRange && accountId < BotAccountMin || hasRange && accountId > BotAccountMax)
            return false;

        if (!hasPrefix)
            return true;

        QueryResult result = LoginDatabase.Query("SELECT username FROM account WHERE id = {} LIMIT 1", accountId);
        if (!result)
            return false;

        std::string username = ToLowerCopy(result->Fetch()[0].Get<std::string>());
        std::string prefix = ToLowerCopy(BotAccountPrefix);
        return username.rfind(prefix, 0) == 0;
    }

    static std::vector<DungeonTemplate> LoadDungeonTemplates(uint8 team, uint8 level, bool raidOnly)
    {
        std::vector<DungeonTemplate> out;

        std::string phaseClause;
        if (RespectProgressionPhase)
            phaseClause = Acore::StringFormat(" AND min_phase <= {} AND max_phase >= {} ", ProgressionPhase, ProgressionPhase);

        QueryResult result = WorldDatabase.Query(
            "SELECT id, name, map_id, difficulty, min_level, max_level, target_level, group_size, is_raid, "
            "entrance_map, entrance_x, entrance_y, entrance_z, entrance_o "
            "FROM playerbot_dungeon_template "
            "WHERE min_level <= {} AND max_level >= {} AND is_raid = {} {} "
            "ORDER BY ABS(target_level - {}) ASC LIMIT 8",
            level, level, raidOnly ? 1 : 0, phaseClause, level);

        if (!result)
            return out;

        do
        {
            Field* f = result->Fetch();
            DungeonTemplate d;
            d.Id = f[0].Get<uint32>();
            d.Name = f[1].Get<std::string>();
            d.MapId = f[2].Get<uint32>();
            d.Difficulty = f[3].Get<uint8>();
            d.MinLevel = f[4].Get<uint8>();
            d.MaxLevel = f[5].Get<uint8>();
            d.TargetLevel = f[6].Get<uint8>();
            d.GroupSize = f[7].Get<uint8>();
            d.IsRaid = f[8].Get<uint8>() != 0;
            d.EntranceMap = f[9].Get<uint32>();
            d.X = f[10].Get<float>();
            d.Y = f[11].Get<float>();
            d.Z = f[12].Get<float>();
            d.O = f[13].Get<float>();

            if (IsDungeonAllowed(d.MapId) && IsRaidAllowedByServerCap(d))
                out.push_back(d);
        } while (result->NextRow());

        return out;
    }

    static std::vector<BotCandidate> LoadEligibleBots(uint8 team, uint8 minLevel, uint8 maxLevel, uint32 limit)
    {
        std::vector<BotCandidate> out;

        // Bot safety filter is applied after row load so we can check auth.account.username via LoginDatabase.
        QueryResult result = CharacterDatabase.Query(
            "SELECT c.guid, c.name, c.level, c.race, c.class, COALESCE(g.guildid,0), c.account "
            "FROM characters c "
            "LEFT JOIN guild_member g ON g.guid = c.guid "
            "LEFT JOIN playerbot_dungeon_run_member rm ON rm.guid = c.guid "
            "LEFT JOIN playerbot_dungeon_run r ON r.id = rm.run_id AND r.state IN (0,1,4) "
            "WHERE c.level BETWEEN {} AND {} AND r.id IS NULL "
            "ORDER BY RAND() LIMIT {}",
            minLevel, maxLevel, limit * 8);

        if (!result)
            return out;

        do
        {
            Field* f = result->Fetch();
            BotCandidate b;
            b.GuidLow = f[0].Get<uint32>();
            b.Name = f[1].Get<std::string>();
            b.Level = f[2].Get<uint8>();
            b.Race = f[3].Get<uint8>();
            b.Class = f[4].Get<uint8>();
            b.GuildId = f[5].Get<uint32>();
            uint32 accountId = f[6].Get<uint32>();
            b.Team = GetTeamFromRace(b.Race);
            b.PreferredRole = GuessRole(b.Class);

            if (b.Team == team && b.Level >= MinBotLevel && AccountMatchesBotFilter(accountId))
                out.push_back(b);
        } while (result->NextRow());

        return out;
    }

    static int32 CalculateQuality(std::vector<BotCandidate> const& group, DungeonTemplate const& dungeon)
    {
        int32 score = 50;
        uint32 tanks = 0, healers = 0;
        uint32 guildId = group.empty() ? 0 : group.front().GuildId;
        bool sameGuild = guildId != 0;
        int32 avgLevel = 0;

        for (BotCandidate const& b : group)
        {
            avgLevel += b.Level;
            if (b.PreferredRole == ROLE_TANK) ++tanks;
            if (b.PreferredRole == ROLE_HEALER) ++healers;
            if (!guildId || b.GuildId != guildId)
                sameGuild = false;
        }

        avgLevel /= std::max<size_t>(1, group.size());
        score += (avgLevel - dungeon.TargetLevel) * 4;
        if (tanks >= 1) score += 15; else score -= 20;
        if (healers >= 1) score += 20; else score -= 25;
        if (sameGuild) score += 12;
        if (group.size() < dungeon.GroupSize) score -= 15;

        return std::clamp(score, 1, 100);
    }

    static bool ShouldSimulatedBotDecline(BotCandidate const& bot, DungeonTemplate const& dungeon)
    {
        if (SimulatedBotDeclineChance == 0)
            return false;

        if (urand(1, 100) > SimulatedBotDeclineChance)
            return false;

        static char const* reasons[] =
        {
            "nah, finishing quests right now",
            "can't, bags are full",
            "maybe later",
            "repairing first",
            "not enough time for that run",
            "saving my gold tonight"
        };

        std::string reason = reasons[urand(0, 5)];
        StatusLog("[SocialSim] " + bot.Name + " declined " + dungeon.Name + ": " + reason);
        // Guild-specific decline chatter is handled after guild chat helpers are available; keep this branch-safe here.

        return true;
    }

    static bool BuildGroup(std::vector<BotCandidate>& candidates, DungeonTemplate const& dungeon, std::vector<BotCandidate>& group)
    {
        group.clear();
        if (candidates.size() < dungeon.GroupSize && RequireFullGroup)
            return false;

        std::sort(candidates.begin(), candidates.end(), [](BotCandidate const& a, BotCandidate const& b)
        {
            if (a.GuildId != b.GuildId)
                return a.GuildId > b.GuildId;
            return a.Level > b.Level;
        });

        if (PreferGuildGroups)
        {
            for (BotCandidate const& seed : candidates)
            {
                if (!seed.GuildId)
                    continue;

                std::vector<BotCandidate> sameGuild;
                for (BotCandidate const& b : candidates)
                    if (b.GuildId == seed.GuildId && !ShouldSimulatedBotDecline(b, dungeon))
                        sameGuild.push_back(b);

                if (sameGuild.size() >= dungeon.GroupSize)
                {
                    group.assign(sameGuild.begin(), sameGuild.begin() + dungeon.GroupSize);
                    return true;
                }
            }
        }

        if (!AllowPugs)
            return false;

        bool hasTank = false, hasHealer = false;
        for (BotCandidate const& b : candidates)
        {
            if (group.size() >= dungeon.GroupSize)
                break;
            if (!hasTank && b.PreferredRole == ROLE_TANK && !ShouldSimulatedBotDecline(b, dungeon))
            {
                group.push_back(b); hasTank = true;
            }
        }
        for (BotCandidate const& b : candidates)
        {
            if (group.size() >= dungeon.GroupSize)
                break;
            if (!hasHealer && b.PreferredRole == ROLE_HEALER && !ShouldSimulatedBotDecline(b, dungeon))
            {
                group.push_back(b); hasHealer = true;
            }
        }
        for (BotCandidate const& b : candidates)
        {
            if (group.size() >= dungeon.GroupSize)
                break;
            bool exists = std::any_of(group.begin(), group.end(), [&](BotCandidate const& g) { return g.GuidLow == b.GuidLow; });
            if (!exists && !ShouldSimulatedBotDecline(b, dungeon))
                group.push_back(b);
        }

        return group.size() == dungeon.GroupSize || (!RequireFullGroup && !group.empty());
    }


    static Player* FindOnlinePlayer(uint32 guidLow)
    {
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(guidLow);
        return ObjectAccessor::FindConnectedPlayer(guid);
    }

    static void SendGuildSimulationMessage(uint32 guildId, std::string const& speakerName, std::string const& message)
    {
        if (!GuildChatSimulation || guildId == 0)
            return;

        StatusLog("[GuildSim guild=" + std::to_string(guildId) + "] " + speakerName + ": " + message);

        if (!GuildChatToOnlineMembers)
            return;

        QueryResult members = CharacterDatabase.Query("SELECT guid FROM guild_member WHERE guildid = {} LIMIT 200", guildId);
        if (!members)
            return;

        do
        {
            uint32 guidLow = members->Fetch()[0].Get<uint32>();
            if (Player* player = FindOnlinePlayer(guidLow))
                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage("|cff40ff40[GuildSim]|r {}: {}", speakerName, message);
        } while (members->NextRow());
    }

    static void MaybeSendGuildLfm(std::vector<BotCandidate> const& group, DungeonTemplate const& dungeon)
    {
        if (!GuildChatSimulation || group.empty())
            return;

        uint32 guildId = GetRunGuildId(group);
        if (guildId == 0)
            return;

        if (urand(1, 100) > GuildChatChance)
            return;

        BotCandidate const& speaker = group.front();
        std::string msg;
        if (group.size() < dungeon.GroupSize)
            msg = "LFM " + dungeon.Name + " - need " + std::to_string(uint32(dungeon.GroupSize - group.size())) + " more.";
        else
            msg = "Guild group heading into " + dungeon.Name + ". Wish us luck.";

        SendGuildSimulationMessage(guildId, speaker.Name, msg);
    }

    static std::string CapitalCityName(uint8 team)
    {
        return team == TEAM_HORDE ? "Orgrimmar" : "Stormwind";
    }

    static void SendAmbientChatMessage(std::string const& channel, std::string const& speakerName, std::string const& message)
    {
        // This is intentionally console/system style instead of injecting raw packets into a real channel.
        // It gives visibility without risking channel state issues on different AzerothCore branches.
        StatusLog("[" + channel + "] " + speakerName + ": " + message);
    }

    static void MaybeSendGeneralLfg(std::vector<BotCandidate> const& group, DungeonTemplate const& dungeon)
    {
        if (!GeneralChatSimulation || group.empty())
            return;

        if (urand(1, 100) <= GeneralChatChance)
        {
            BotCandidate const& speaker = group[urand(0, uint32(group.size() - 1))];
            std::string msg = "LFM " + dungeon.Name + " - " + TeamName(speaker.Team) + " group forming, pst.";
            SendAmbientChatMessage("GeneralSim:" + CapitalCityName(speaker.Team), speaker.Name, msg);
        }

        if (LocalZoneChatSimulation && urand(1, 100) <= LocalZoneChatChance)
        {
            BotCandidate const& speaker = group[urand(0, uint32(group.size() - 1))];
            std::string msg = "Anyone near " + dungeon.Name + "? Group heading in soon.";
            SendAmbientChatMessage("LocalSim:" + dungeon.Name, speaker.Name, msg);
        }
    }

    static std::string ClassicCapitalCityName(uint8 team)
    {
        if (!RestrictOrganicCitiesToClassic)
            return CapitalCityName(team);

        if (team == TEAM_HORDE)
        {
            static char const* cities[] = { "Orgrimmar", "Thunder Bluff", "Undercity" };
            return cities[urand(0, 2)];
        }

        static char const* cities[] = { "Stormwind", "Ironforge", "Darnassus" };
        return cities[urand(0, 2)];
    }

    static std::string RoleText(Role role)
    {
        switch (role)
        {
            case ROLE_TANK: return "tank";
            case ROLE_HEALER: return "heal";
            case ROLE_DPS: return "dps";
            default: return "dps";
        }
    }

    static std::string ClassSpecText(uint8 cls, Role role)
    {
        switch (cls)
        {
            case CLASS_WARRIOR: return role == ROLE_TANK ? "prot warrior" : "arms warrior";
            case CLASS_PALADIN: return role == ROLE_HEALER ? "holy paladin" : (role == ROLE_TANK ? "prot paladin" : "ret paladin");
            case CLASS_HUNTER: return "marks hunter";
            case CLASS_ROGUE: return "combat rogue";
            case CLASS_PRIEST: return role == ROLE_HEALER ? (urand(0, 1) ? "holy priest" : "disc priest") : "shadow priest";
            case CLASS_SHAMAN: return role == ROLE_HEALER ? "resto shaman" : "enh shaman";
            case CLASS_MAGE: return urand(0, 1) ? "frost mage" : "fire mage";
            case CLASS_WARLOCK: return "aff lock";
            case CLASS_DRUID: return role == ROLE_HEALER ? "resto druid" : (role == ROLE_TANK ? "feral tank" : "feral dps");
            default: return RoleText(role);
        }
    }

    static void SendOrganicLfgActing(std::vector<BotCandidate> const& group, DungeonTemplate const& dungeon)
    {
        if (!OrganicLfgActing || group.empty())
            return;

        if (urand(1, 100) > OrganicLfgChance)
            return;

        std::string city = ClassicCapitalCityName(group.front().Team);
        BotCandidate const& leader = group.front();

        static char const* openers[] =
        {
            "LFM {} need tank/heals/dps",
            "forming {} pst role",
            "LFG/LFM {} whisper me",
            "{} group starting, need a few more"
        };

        std::string opener = Acore::StringFormat(openers[urand(0, 3)], dungeon.Name);
        SendAmbientChatMessage("GeneralSim:" + city, leader.Name, opener);

        uint32 lines = std::min<uint32>(uint32(group.size()), urand(2, std::min<uint32>(5, uint32(group.size()))));
        for (uint32 i = 0; i < lines; ++i)
        {
            BotCandidate const& b = group[i];
            std::string spec = ClassSpecText(b.Class, b.PreferredRole);
            std::string msg;

            switch (b.PreferredRole)
            {
                case ROLE_TANK:
                    msg = urand(0, 1) ? "I can tank" : "tank here " + spec;
                    break;
                case ROLE_HEALER:
                    msg = urand(0, 1) ? "I can heal" : spec + " heals";
                    break;
                default:
                    msg = urand(0, 1) ? "dps here" : spec + " dps";
                    break;
            }

            SendAmbientChatMessage("GeneralSim:" + city, b.Name, msg);
        }

        if (LocalZoneChatSimulation && urand(1, 100) <= LocalZoneChatChance)
        {
            BotCandidate const& b = group[urand(0, uint32(group.size() - 1))];
            SendAmbientChatMessage("LocalSim:" + dungeon.Name, b.Name, "summons? at stone? heading to " + dungeon.Name);
        }
    }

    static void MaybeSendTradeBoeSpam()
    {
        if (!TradeChatSimulation || !GeneralChatSimulation)
            return;

        if (TradeChatEveryTicks > 1)
        {
            ++_tradeTickCounter;
            if ((_tradeTickCounter % TradeChatEveryTicks) != 0)
                return;
        }

        if (urand(1, 100) > TradeChatChance)
            return;

        uint8 team = urand(0, 1) ? TEAM_ALLIANCE : TEAM_HORDE;
        std::string city = ClassicCapitalCityName(team);

        QueryResult result = CharacterDatabase.Query(
            "SELECT c.name FROM characters c WHERE c.level >= {} ORDER BY RAND() LIMIT 1",
            MinBotLevel);

        std::string seller = result ? result->Fetch()[0].Get<std::string>() : "Auctioneer";

        static char const* fakeBoes[] =
        {
            "green mail boots",
            "blue 2h axe",
            "cloth int shoulders",
            "agi leather belt",
            "boe wand",
            "twink sword",
            "of the bear chest",
            "of the eagle staff"
        };

        uint32 priceGold = urand(2, 25);
        std::string msg = Acore::StringFormat("WTS {} {}g can COD", fakeBoes[urand(0, 7)], priceGold);
        SendAmbientChatMessage("TradeSim:" + city, seller, msg);
    }

    static bool IsPlayerSafeToMove(Player* player)
    {
        if (!player)
            return false;

        if (!player->IsInWorld())
            return false;

        if (player->IsBeingTeleported())
            return false;

        if (player->GetSession() && player->GetSession()->GetSecurity() > SEC_PLAYER)
            return false;

        // Do not yank an actively controlled real player unless a later invite system explicitly accepts.
        // For now, online non-GM characters are only moved if they are already known members of this bot run.
        return true;
    }

    static uint32 TeleportOnlineGroupMembers(std::vector<BotCandidate> const& members, DungeonTemplate const& dungeon)
    {
        if (!TeleportOnlineMembers)
            return 0;

        uint32 moved = 0;
        for (BotCandidate const& b : members)
        {
            Player* player = FindOnlinePlayer(b.GuidLow);
            if (!IsPlayerSafeToMove(player))
                continue;

            if (player->IsInCombat())
                player->CombatStop(true);

            player->TeleportTo(dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O);
            ++moved;
        }

        return moved;
    }

    static bool CreateLiveGroupIfPossible(std::vector<BotCandidate> const& members)
    {
        if (!CreateRealGroups || members.empty())
            return false;

        std::vector<Player*> online;
        online.reserve(members.size());

        for (BotCandidate const& b : members)
        {
            Player* player = FindOnlinePlayer(b.GuidLow);
            if (IsPlayerSafeToMove(player) && !player->GetGroup())
                online.push_back(player);
        }

        if (online.size() < 2)
            return false;

        Player* leader = online.front();
        Group* liveGroup = new Group();
        if (!liveGroup->Create(leader))
        {
            delete liveGroup;
            return false;
        }

        sGroupMgr->AddGroup(liveGroup);

        for (Player* member : online)
        {
            if (member == leader)
                continue;

            if (!member->GetGroup())
                liveGroup->AddMember(member);
        }

        return true;
    }

    static bool IsRealPlayerEligibleForInvite(Player* player, DungeonTemplate const& dungeon, uint8 team)
    {
        if (!player || !player->GetSession())
            return false;

        // Do not invite GMs, bots already in a sim run, or characters that are busy.
        if (player->GetSession()->GetSecurity() > SEC_PLAYER)
            return false;

        if (RequirePlayerPartyless && player->GetGroup())
            return false;

        if (player->IsBeingTeleported() || player->IsInCombat())
            return false;

        if (RequirePlayerSameFaction && GetTeamFromRace(player->getRace()) != team)
            return false;

        if (RequirePlayerLevelRange && (player->GetLevel() < dungeon.MinLevel || player->GetLevel() > dungeon.MaxLevel))
            return false;

        QueryResult alreadyRunning = CharacterDatabase.Query(
            "SELECT 1 FROM playerbot_dungeon_run_member rm "
            "JOIN playerbot_dungeon_run r ON r.id = rm.run_id "
            "WHERE rm.guid = {} AND r.state IN (0,1,4) LIMIT 1",
            player->GetGUID().GetCounter());
        if (alreadyRunning)
            return false;

        return true;
    }

    static std::vector<Player*> FindRealPlayerInviteTargets(DungeonTemplate const& dungeon, uint8 team, uint32 maxTargets)
    {
        std::vector<Player*> out;
        if (!InviteRealPlayers || maxTargets == 0)
            return out;

        // NOTE: This starter module intentionally avoids depending on one specific
        // AzerothCore online-player container API, because older/newer branches
        // expose it differently. Real-player invites are supported by the DB/chat
        // handler path, but online target discovery should be wired to your branch's
        // player iteration helper in the next local pass.
        (void)dungeon;
        (void)team;
        return out;
    }

    static void CreateRealPlayerInvites(uint64 runId, DungeonTemplate const& dungeon, std::vector<BotCandidate> const& group)
    {
        if (!InviteRealPlayers || group.empty())
            return;

        if (InviteOnlyIfGroupShort && group.size() >= dungeon.GroupSize)
            return;

        uint32 now = GameTime::GetGameTime().count();
        uint32 inviteCount = std::max<uint32>(1, MaxRealPlayerInvitesPerRun);
        std::vector<Player*> targets = FindRealPlayerInviteTargets(dungeon, group.front().Team, inviteCount);
        if (targets.empty())
            return;

        std::string leaderName = group.front().Name;
        CharacterDatabase.EscapeString(leaderName);

        for (Player* target : targets)
        {
            uint32 guidLow = target->GetGUID().GetCounter();
            CharacterDatabase.Execute(
                "INSERT INTO playerbot_dungeon_player_invite "
                "(run_id, player_guid, dungeon_template_id, state, invited_at, expires_at, inviter_name) "
                "VALUES ({},{},{},0,{},{},'{}') "
                "ON DUPLICATE KEY UPDATE state = 0, invited_at = VALUES(invited_at), expires_at = VALUES(expires_at), inviter_name = VALUES(inviter_name)",
                runId, guidLow, dungeon.Id, now, now + InviteTimeoutSeconds, leaderName);

            ChatHandler(target->GetSession()).PSendSysMessage(
                "{} asks: Want to join a {} run? Type 'yes' or 'no' in say chat within {} seconds. If you accept, no auto-teleport will happen; travel together or summon bots with bot commands.",
                group.front().Name, dungeon.Name, InviteTimeoutSeconds);
        }
    }

    static std::string LowerCopy(std::string msg)
    {
        std::transform(msg.begin(), msg.end(), msg.begin(), [](unsigned char c) { return std::tolower(c); });
        return msg;
    }

    static bool HandleInviteChat(Player* player, std::string const& msg)
    {
        if (!InviteRealPlayers || !player)
            return false;

        std::string lower = LowerCopy(msg);
        if (lower != "yes" && lower != "y" && lower != "no" && lower != "n")
            return false;

        uint32 now = GameTime::GetGameTime().count();
        uint32 guidLow = player->GetGUID().GetCounter();
        QueryResult invite = CharacterDatabase.Query(
            "SELECT i.run_id, i.dungeon_template_id, r.team, r.entrance_map, r.entrance_x, r.entrance_y, r.entrance_z, r.entrance_o "
            "FROM playerbot_dungeon_player_invite i "
            "JOIN playerbot_dungeon_run r ON r.id = i.run_id "
            "WHERE i.player_guid = {} AND i.state = 0 AND i.expires_at >= {} AND r.state IN (1,4) "
            "ORDER BY i.id DESC LIMIT 1",
            guidLow, now);

        if (!invite)
            return false;

        Field* f = invite->Fetch();
        uint64 runId = f[0].Get<uint64>();
        uint32 dungeonTemplateId = f[1].Get<uint32>();
        uint8 team = f[2].Get<uint8>();
        uint32 entranceMap = f[3].Get<uint32>();
        float x = f[4].Get<float>();
        float y = f[5].Get<float>();
        float z = f[6].Get<float>();
        float o = f[7].Get<float>();

        if (lower == "no" || lower == "n")
        {
            CharacterDatabase.Execute("UPDATE playerbot_dungeon_player_invite SET state = 2, responded_at = {} WHERE run_id = {} AND player_guid = {}",
                now, runId, guidLow);
            ChatHandler(player->GetSession()).PSendSysMessage("You declined the dungeon group invite.");
            return true;
        }

        QueryResult dungeonResult = WorldDatabase.Query(
            "SELECT min_level, max_level, name FROM playerbot_dungeon_template WHERE id = {} LIMIT 1", dungeonTemplateId);
        if (!dungeonResult)
            return true;

        Field* df = dungeonResult->Fetch();
        DungeonTemplate dungeon;
        dungeon.Id = dungeonTemplateId;
        dungeon.MinLevel = df[0].Get<uint8>();
        dungeon.MaxLevel = df[1].Get<uint8>();
        dungeon.Name = df[2].Get<std::string>();
        dungeon.EntranceMap = entranceMap;
        dungeon.X = x;
        dungeon.Y = y;
        dungeon.Z = z;
        dungeon.O = o;

        if (!IsRealPlayerEligibleForInvite(player, dungeon, team))
        {
            CharacterDatabase.Execute("UPDATE playerbot_dungeon_player_invite SET state = 3, responded_at = {}, note = 'player_no_longer_eligible' WHERE run_id = {} AND player_guid = {}",
                now, runId, guidLow);
            ChatHandler(player->GetSession()).PSendSysMessage("You are no longer eligible for that dungeon invite.");
            return true;
        }

        CharacterDatabase.Execute(
            "INSERT IGNORE INTO playerbot_dungeon_run_member "
            "(run_id, guid, role, guild_id, level_at_start, joined_as_real_player) VALUES ({},{},3,0,{},1)",
            runId, guidLow, player->GetLevel());

        CharacterDatabase.Execute("UPDATE playerbot_dungeon_player_invite SET state = 1, responded_at = {} WHERE run_id = {} AND player_guid = {}",
            now, runId, guidLow);

        // Real players should not be yanked by DungeonSim.
        // Mixed player+bot runs use manual travel/summon behavior so the player and bots can journey together.
        ChatHandler(player->GetSession()).PSendSysMessage("You accepted the dungeon group invite for {}. No teleport was used; travel together or summon the bots with your bot commands.", dungeon.Name);
        DebugLog("Real player " + player->GetName() + " accepted dungeon sim run #" + std::to_string(runId));
        return true;
    }

    static void ExpireOldInvites()
    {
        if (!InviteRealPlayers)
            return;

        uint32 now = GameTime::GetGameTime().count();
        CharacterDatabase.Execute("UPDATE playerbot_dungeon_player_invite SET state = 4, note = 'expired' WHERE state = 0 AND expires_at < {}", now);
    }

    static uint32 GetRunGuildId(std::vector<BotCandidate> const& group)
    {
        if (group.empty())
            return 0;

        uint32 guildId = group.front().GuildId;
        if (guildId == 0)
            return 0;

        for (BotCandidate const& b : group)
            if (b.GuildId != guildId)
                return 0;

        return guildId;
    }

    static bool IsRunLocked(DungeonTemplate const& dungeon, uint8 team, uint32 guildId)
    {
        if (dungeon.IsRaid)
        {
            if (!RaidLockouts)
                return false;

            uint32 now = GameTime::GetGameTime().count();
            QueryResult result = CharacterDatabase.Query(
                "SELECT reset_at FROM playerbot_dungeon_lockout "
                "WHERE dungeon_template_id = {} AND team = {} AND guild_id = {} AND reset_at > {} LIMIT 1",
                dungeon.Id, team, guildId, now);
            return bool(result);
        }

        // Vanilla normal 5-mans do not have saved lockouts. Keep this off by default.
        return NormalDungeonLockouts;
    }

    static void SaveRunLockout(DungeonTemplate const& dungeon, uint8 team, uint32 guildId)
    {
        if (!dungeon.IsRaid || !RaidLockouts)
            return;

        uint32 now = GameTime::GetGameTime().count();
        uint32 resetAt = now + RaidLockoutDays * DAY;
        CharacterDatabase.Execute(
            "INSERT INTO playerbot_dungeon_lockout "
            "(dungeon_template_id, team, guild_id, locked_at, reset_at) VALUES ({},{},{},{},{}) "
            "ON DUPLICATE KEY UPDATE locked_at = VALUES(locked_at), reset_at = VALUES(reset_at)",
            dungeon.Id, team, guildId, now, resetAt);
    }

    static void CreateRun(DungeonTemplate const& dungeon, std::vector<BotCandidate> const& group)
    {
        uint32 now = GameTime::GetGameTime().count();
        uint32 duration = urand(MinDurationMinutes * MINUTE, MaxDurationMinutes * MINUTE);
        int32 quality = CalculateQuality(group, dungeon);
        uint32 guildId = GetRunGuildId(group);

        if (IsRunLocked(dungeon, group.front().Team, guildId))
        {
            DebugLog("Skipped " + dungeon.Name + " because this raid team is locked until reset.");
            return;
        }

        bool madeLiveGroup = RealRunFirst && CreateLiveGroupIfPossible(group);

        // Bot-only runs may auto-teleport. Runs that may invite a real player should not yank anyone,
        // because those are meant to feel like a real party that travels/summons together.
        bool mayInviteRealPlayer = InviteRealPlayers && MaxRealPlayerInvitesPerRun > 0 && (!InviteOnlyIfGroupShort || group.size() < dungeon.GroupSize);
        bool allowAutoTeleport = TeleportOnlineMembers && !(ManualTravelForPlayerJoinedRuns && mayInviteRealPlayer);
        uint32 movedOnline = (RealRunFirst && allowAutoTeleport) ? TeleportOnlineGroupMembers(group, dungeon) : 0;
        uint8 initialState = (RealRunFirst && movedOnline > 0) ? RUN_REAL_ATTEMPT : RUN_ACTIVE;

        if (ManualTravelForPlayerJoinedRuns && mayInviteRealPlayer)
            DebugLog("Manual travel mode for " + dungeon.Name + ": real-player invite possible, no auto-teleport used.");

        if (TeleportOnlyIfOnline && RealRunFirst && allowAutoTeleport && movedOnline == 0)
        {
            DebugLog("Skipped run for " + dungeon.Name + " because no online members could be teleported.");
            return;
        }

        CharacterDatabase.Execute(
            "INSERT INTO playerbot_dungeon_run "
            "(dungeon_template_id, team, guild_id, state, started_at, ends_at, quality_score, planned_duration_sec, "
            "real_group_created, online_members_moved, entrance_map, entrance_x, entrance_y, entrance_z, entrance_o) "
            "VALUES ({},{},{},{},{},{},{},{},{},{},{},{},{},{},{})",
            dungeon.Id, group.front().Team, guildId, initialState, now, now + duration, quality, duration,
            madeLiveGroup ? 1 : 0, movedOnline, dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O);

        uint64 runId = 0;
        if (QueryResult idResult = CharacterDatabase.Query("SELECT LAST_INSERT_ID()"))
            runId = (*idResult)[0].Get<uint64>();
        if (!runId)
            return;

        SaveRunLockout(dungeon, group.front().Team, guildId);

        MaybeSendGuildLfm(group, dungeon);
        MaybeSendGeneralLfg(group, dungeon);
        SendOrganicLfgActing(group, dungeon);
        CreateRealPlayerInvites(runId, dungeon, group);

        for (BotCandidate const& b : group)
        {
            CharacterDatabase.Execute(
                "INSERT INTO playerbot_dungeon_run_member "
                "(run_id, guid, role, guild_id, level_at_start, joined_as_real_player) "
                "VALUES ({},{},{},{},{},0)",
                runId, b.GuidLow, uint8(b.PreferredRole), b.GuildId, b.Level);
        }

        QueryResult bosses = WorldDatabase.Query(
            "SELECT boss_order, boss_name FROM playerbot_dungeon_boss_template WHERE dungeon_template_id = {} ORDER BY boss_order",
            dungeon.Id);
        if (bosses)
        {
            do
            {
                Field* f = bosses->Fetch();
                std::string bossName = f[1].Get<std::string>();
                CharacterDatabase.EscapeString(bossName);
                CharacterDatabase.Execute(
                    "INSERT INTO playerbot_dungeon_run_boss (run_id, boss_order, boss_name, killed) VALUES ({},{},'{}',0)",
                    runId, f[0].Get<uint8>(), bossName);
            } while (bosses->NextRow());
        }

        StatusLog("START run #" + std::to_string(runId) + " " + TeamName(group.front().Team) + " " + dungeon.Name +
            " members=" + std::to_string(group.size()) + " guild=" + std::to_string(guildId) +
            " quality=" + std::to_string(quality) + " duration=" + std::to_string(duration / MINUTE) + "m" +
            " liveGroup=" + std::to_string(madeLiveGroup ? 1 : 0) + " moved=" + std::to_string(movedOnline));

        // Next pass:
        // - Issue playerbot strategy/orders where your Playerbots branch exposes API hooks.
        // - Watch live boss kills if possible; otherwise timed simulation fallback resolves the run.
    }



    static bool IsClassAllowed(ItemTemplate const* proto, uint8 cls)
    {
        if (!proto)
            return false;

        if (proto->AllowableClass == -1)
            return true;

        uint32 classMask = 1u << (cls - 1);
        return (uint32(proto->AllowableClass) & classMask) != 0;
    }

    static bool IsRaceAllowed(ItemTemplate const* proto, uint8 race)
    {
        if (!proto)
            return false;

        if (proto->AllowableRace == -1)
            return true;

        uint32 raceMask = 1u << (race - 1);
        return (uint32(proto->AllowableRace) & raceMask) != 0;
    }

    static bool CanUseItem(BotCandidate const& bot, ItemTemplate const* proto)
    {
        if (!proto)
            return false;

        if (proto->RequiredLevel && bot.Level < proto->RequiredLevel)
            return false;

        if (!IsClassAllowed(proto, bot.Class) || !IsRaceAllowed(proto, bot.Race))
            return false;

        // Skip quest keys, conjured junk, money-only objects, recipes for now.
        if (proto->Class == ITEM_CLASS_QUEST || proto->Class == ITEM_CLASS_KEY || proto->Class == ITEM_CLASS_RECIPE)
            return false;

        return true;
    }

    static uint32 ScoreLootForBot(BotCandidate const& bot, ItemTemplate const* proto)
    {
        if (!CanUseItem(bot, proto))
            return 0;

        uint32 score = 10 + proto->ItemLevel + (proto->Quality * 15);

        // Simple armor preference. This is intentionally conservative: usable first, perfect upgrade logic later.
        if (proto->Class == ITEM_CLASS_ARMOR)
        {
            switch (bot.Class)
            {
                case CLASS_MAGE:
                case CLASS_PRIEST:
                case CLASS_WARLOCK:
                    if (proto->SubClass == ITEM_SUBCLASS_ARMOR_CLOTH) score += 40;
                    break;
                case CLASS_ROGUE:
                case CLASS_DRUID:
                    if (proto->SubClass == ITEM_SUBCLASS_ARMOR_LEATHER) score += 40;
                    break;
                case CLASS_HUNTER:
                case CLASS_SHAMAN:
                    if (bot.Level >= 40 && proto->SubClass == ITEM_SUBCLASS_ARMOR_MAIL) score += 45;
                    else if (proto->SubClass == ITEM_SUBCLASS_ARMOR_LEATHER) score += 25;
                    break;
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                    if (bot.Level >= 40 && proto->SubClass == ITEM_SUBCLASS_ARMOR_PLATE) score += 50;
                    else if (proto->SubClass == ITEM_SUBCLASS_ARMOR_MAIL) score += 30;
                    break;
                default:
                    break;
            }
        }

        // Role bias. Later we can inspect stat arrays deeply; this is a safe first pass.
        if (bot.PreferredRole == ROLE_TANK && proto->Class == ITEM_CLASS_ARMOR)
            score += 10;
        if (bot.PreferredRole == ROLE_HEALER && proto->InventoryType != INVTYPE_WEAPON && proto->InventoryType != INVTYPE_2HWEAPON)
            score += 8;

        return score;
    }

    static std::vector<BotCandidate> LoadRunMembers(uint64 runId)
    {
        std::vector<BotCandidate> members;
        QueryResult result = CharacterDatabase.Query(
            "SELECT c.guid, c.name, c.level, c.race, c.class, COALESCE(g.guildid,0), rm.role "
            "FROM playerbot_dungeon_run_member rm "
            "JOIN characters c ON c.guid = rm.guid "
            "LEFT JOIN guild_member g ON g.guid = c.guid "
            "WHERE rm.run_id = {}",
            runId);

        if (!result)
            return members;

        do
        {
            Field* f = result->Fetch();
            BotCandidate b;
            b.GuidLow = f[0].Get<uint32>();
            b.Name = f[1].Get<std::string>();
            b.Level = f[2].Get<uint8>();
            b.Race = f[3].Get<uint8>();
            b.Class = f[4].Get<uint8>();
            b.GuildId = f[5].Get<uint32>();
            b.Team = GetTeamFromRace(b.Race);
            b.PreferredRole = Role(f[6].Get<uint8>());
            members.push_back(b);
        } while (result->NextRow());

        return members;
    }

    struct LootCandidate
    {
        uint32 ItemEntry = 0;
        float Chance = 0.0f;
    };

    static std::vector<LootCandidate> LoadBossLoot(uint32 creatureEntry)
    {
        std::vector<LootCandidate> loot;
        if (!creatureEntry)
            return loot;

        QueryResult result = WorldDatabase.Query(
            "SELECT Item, Chance FROM creature_loot_template WHERE Entry = {} AND Item > 0 ORDER BY Chance DESC LIMIT 80",
            creatureEntry);

        if (!result)
            return loot;

        do
        {
            Field* f = result->Fetch();
            LootCandidate l;
            l.ItemEntry = f[0].Get<uint32>();
            l.Chance = f[1].Get<float>();
            loot.push_back(l);
        } while (result->NextRow());

        return loot;
    }

    static bool TryStoreOnline(Player* player, uint32 itemEntry, bool& equipped)
    {
        equipped = false;
        if (!player)
            return false;

        ItemPosCountVec dest;
        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, 1);
        if (msg != EQUIP_ERR_OK)
            return false;

        Item* item = player->StoreNewItem(dest, itemEntry, true);
        if (!item)
            return false;

        player->SendNewItem(item, 1, true, false);

        if (EquipUsableLootOnline)
        {
            uint16 equipDest = 0;
            InventoryResult equipMsg = player->CanEquipItem(NULL_SLOT, equipDest, item, false);
            if (equipMsg == EQUIP_ERR_OK)
            {
                player->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
                player->EquipItem(equipDest, item, true);
                equipped = true;
            }
        }

        return true;
    }

    static uint32 GetEquippedItemScore(Player* player, uint16 equipDest)
    {
        if (!player)
            return 0;

        uint8 slot = uint8(equipDest & 255);
        if (slot >= EQUIPMENT_SLOT_END)
            return 0;

        Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!equipped)
            return 0;

        ItemTemplate const* proto = equipped->GetTemplate();
        if (!proto)
            return 0;

        return proto->ItemLevel + (proto->Quality * 15);
    }

    static uint32 GetNewItemScore(ItemTemplate const* proto)
    {
        if (!proto)
            return 0;

        return proto->ItemLevel + (proto->Quality * 15);
    }

    static bool IsBetterThanEquipped(Player* player, ItemTemplate const* proto, uint16 equipDest)
    {
        if (!OnlyEquipBetterItems)
            return true;

        uint32 newScore = GetNewItemScore(proto);
        uint32 oldScore = GetEquippedItemScore(player, equipDest);

        // If the slot is empty, any equippable loot is an upgrade.
        if (oldScore == 0)
            return true;

        // Require a small margin so bots do not constantly replace sidegrades.
        return newScore >= oldScore + 5;
    }

    static bool TryStoreOnlineSmart(Player* player, uint32 itemEntry, bool allowEquip, bool& equipped, uint32& upgradeScore, uint8& equipSlot)
    {
        equipped = false;
        upgradeScore = 0;
        equipSlot = 255;

        if (!player)
            return false;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
        if (!proto)
            return false;

        ItemPosCountVec dest;
        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, 1);
        if (msg != EQUIP_ERR_OK)
            return false;

        Item* item = player->StoreNewItem(dest, itemEntry, true);
        if (!item)
            return false;

        player->SendNewItem(item, 1, true, false);

        if (allowEquip)
        {
            uint16 equipDest = 0;
            InventoryResult equipMsg = player->CanEquipItem(NULL_SLOT, equipDest, item, false);
            if (equipMsg == EQUIP_ERR_OK && IsBetterThanEquipped(player, proto, equipDest))
            {
                upgradeScore = GetNewItemScore(proto) - GetEquippedItemScore(player, equipDest);
                equipSlot = uint8(equipDest & 255);
                player->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
                player->EquipItem(equipDest, item, true);
                equipped = true;
            }
        }

        return true;
    }

    static void RecordLootAward(uint64 runId, uint32 guid, uint32 itemEntry, std::string bossName, bool storedOnline, bool equipped, uint32 upgradeScore, uint8 equipSlot)
    {
        CharacterDatabase.EscapeString(bossName);
        uint8 deliveryState = storedOnline ? 2 : 0; // 0 pending, 1 failed, 2 delivered
        CharacterDatabase.Execute(
            "INSERT INTO playerbot_dungeon_loot_award "
            "(run_id, guid, item_entry, source_boss, equipped, stored_online, delivery_state, delivered_at, upgrade_score, equip_slot, awarded_at) "
            "VALUES ({},{},{},'{}',{},{},{},{},{},{},{})",
            runId, guid, itemEntry, bossName, equipped ? 1 : 0, storedOnline ? 1 : 0,
            deliveryState, storedOnline ? GameTime::GetGameTime().count() : 0, upgradeScore, equipSlot,
            GameTime::GetGameTime().count());
    }

    static void AwardLootForRun(uint64 runId, uint32 dungeonTemplateId)
    {
        if (!AwardLoot)
            return;

        std::vector<BotCandidate> members = LoadRunMembers(runId);
        if (members.empty())
            return;

        QueryResult killedBosses = CharacterDatabase.Query(
            "SELECT boss_order, boss_name FROM playerbot_dungeon_run_boss WHERE run_id = {} AND killed = 1 ORDER BY boss_order",
            runId);

        if (!killedBosses)
            return;

        do
        {
            Field* bf = killedBosses->Fetch();
            uint8 bossOrder = bf[0].Get<uint8>();
            std::string bossName = bf[1].Get<std::string>();
            QueryResult bossEntryResult = WorldDatabase.Query(
                "SELECT creature_entry FROM playerbot_dungeon_boss_template WHERE dungeon_template_id = {} AND boss_order = {} LIMIT 1",
                dungeonTemplateId, bossOrder);
            if (!bossEntryResult)
                continue;

            uint32 creatureEntry = bossEntryResult->Fetch()[0].Get<uint32>();
            std::vector<LootCandidate> loot = LoadBossLoot(creatureEntry);
            if (loot.empty())
                continue;

            uint32 drops = urand(LootPerBossMin, LootPerBossMax);
            for (uint32 i = 0; i < drops; ++i)
            {
                LootCandidate picked = loot[urand(0, loot.size() - 1)];
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(picked.ItemEntry);
                if (!proto)
                    continue;

                BotCandidate const* winner = nullptr;
                uint32 bestScore = 0;
                for (BotCandidate const& member : members)
                {
                    uint32 score = ScoreLootForBot(member, proto);
                    if (!score)
                        continue;

                    // Need roll flavor. Better-fit bots are favored but not guaranteed.
                    score += urand(1, 100);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        winner = &member;
                    }
                }

                if (!winner)
                    continue;

                bool storedOnline = false;
                bool equipped = false;
                uint32 upgradeScore = 0;
                uint8 equipSlot = 255;
                if (GiveLootToOnlinePlayers)
                {
                    Player* player = FindOnlinePlayer(winner->GuidLow);
                    if (player)
                        storedOnline = TryStoreOnlineSmart(player, picked.ItemEntry, EquipUsableLootOnline, equipped, upgradeScore, equipSlot);
                }

                RecordLootAward(runId, winner->GuidLow, picked.ItemEntry, bossName, storedOnline, equipped, upgradeScore, equipSlot);
                DebugLog("Awarded item " + std::to_string(picked.ItemEntry) + " from " + bossName + " to " + winner->Name +
                    (storedOnline ? " delivered" : " pending") + (equipped ? " and equipped." : "."));
            }
        } while (killedBosses->NextRow());
    }

    static void DeliverPendingLoot(Player* player)
    {
        if (!DeliverPendingLootOnLogin || !player)
            return;

        uint32 guidLow = player->GetGUID().GetCounter();
        QueryResult pending = CharacterDatabase.Query(
            "SELECT id, item_entry FROM playerbot_dungeon_loot_award "
            "WHERE guid = {} AND delivery_state = 0 ORDER BY id ASC LIMIT {}",
            guidLow, MaxPendingDeliveriesPerLogin);

        if (!pending)
            return;

        uint32 delivered = 0;
        do
        {
            Field* f = pending->Fetch();
            uint64 awardId = f[0].Get<uint64>();
            uint32 itemEntry = f[1].Get<uint32>();

            bool equipped = false;
            uint32 upgradeScore = 0;
            uint8 equipSlot = 255;
            bool stored = TryStoreOnlineSmart(player, itemEntry, EquipUpgradesOnDelivery, equipped, upgradeScore, equipSlot);

            CharacterDatabase.Execute(
                "UPDATE playerbot_dungeon_loot_award "
                "SET delivery_state = {}, stored_online = {}, equipped = {}, delivered_at = {}, upgrade_score = {}, equip_slot = {}, note = '{}' "
                "WHERE id = {}",
                stored ? 2 : 1, stored ? 1 : 0, equipped ? 1 : 0, stored ? GameTime::GetGameTime().count() : 0,
                upgradeScore, equipSlot, stored ? "delivered_on_login" : "delivery_failed_bags_full_or_invalid", awardId);

            if (stored)
                ++delivered;
        } while (pending->NextRow());

        if (delivered > 0)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Dungeon sim delivered {} pending loot item(s).", delivered);
            DebugLog("Delivered " + std::to_string(delivered) + " pending loot item(s) to " + player->GetName());
        }
    }

    static uint8 GetRaidPreviousBest(uint32 dungeonTemplateId, uint8 team, uint32 guildId)
    {
        if (!RaidUseProgressMemory)
            return 0;

        QueryResult result = CharacterDatabase.Query(
            "SELECT best_bosses_killed FROM playerbot_dungeon_raid_progress "
            "WHERE dungeon_template_id = {} AND team = {} AND guild_id = {} LIMIT 1",
            dungeonTemplateId, team, guildId);

        return result ? result->Fetch()[0].Get<uint8>() : 0;
    }

    static void UpdateRaidProgress(uint32 dungeonTemplateId, uint8 team, uint32 guildId, uint8 killed)
    {
        if (!RaidUseProgressMemory || killed == 0)
            return;

        uint32 now = GameTime::GetGameTime().count();
        CharacterDatabase.Execute(
            "INSERT INTO playerbot_dungeon_raid_progress "
            "(dungeon_template_id, team, guild_id, best_bosses_killed, last_bosses_killed, attempts, kills_total, last_run_at, updated_at) "
            "VALUES ({},{},{},{},{},{},{},{},{}) "
            "ON DUPLICATE KEY UPDATE "
            "best_bosses_killed = GREATEST(best_bosses_killed, VALUES(best_bosses_killed)), "
            "last_bosses_killed = VALUES(last_bosses_killed), attempts = attempts + 1, "
            "kills_total = kills_total + VALUES(kills_total), last_run_at = VALUES(last_run_at), updated_at = VALUES(updated_at)",
            dungeonTemplateId, team, guildId, killed, killed, 1, killed, now, now);
    }

    static uint8 CalculateRaidBossDepth(uint32 dungeonTemplateId, uint8 team, uint32 guildId, int32 quality, uint8 bossCount)
    {
        if (bossCount == 0)
            return 0;

        uint8 previousBest = GetRaidPreviousBest(dungeonTemplateId, team, guildId);
        uint8 floor = std::min<uint8>(previousBest, bossCount);
        uint32 roll = urand(1, 100);

        int8 movement = 0;
        if (quality >= 80)
            movement = roll <= 20 ? 0 : (roll <= 75 ? 1 : 2);
        else if (quality >= 55)
            movement = roll <= 25 ? -1 : (roll <= 80 ? 0 : 1);
        else
            movement = roll <= 45 ? -2 : (roll <= 80 ? -1 : 0);

        int32 killed = int32(floor) + movement;
        if (floor == 0)
        {
            if (quality >= 70)
                killed = urand(1, std::min<uint8>(3, bossCount));
            else if (quality >= 45)
                killed = roll <= 65 ? urand(1, std::min<uint8>(2, bossCount)) : 0;
            else
                killed = roll <= 30 ? 1 : 0;
        }

        return uint8(std::clamp<int32>(killed, 0, bossCount));
    }

    static bool IsRaidTemplate(uint32 dungeonTemplateId)
    {
        QueryResult result = WorldDatabase.Query("SELECT is_raid FROM playerbot_dungeon_template WHERE id = {} LIMIT 1", dungeonTemplateId);
        return result && result->Fetch()[0].Get<uint8>() != 0;
    }

    static void ResolveFinishedRuns()
    {
        uint32 now = GameTime::GetGameTime().count();
        QueryResult runs = CharacterDatabase.Query(
            "SELECT id, dungeon_template_id, quality_score, team, guild_id FROM playerbot_dungeon_run WHERE state IN (1,4) AND ends_at <= {} LIMIT 20",
            now);

        if (!runs)
            return;

        do
        {
            Field* f = runs->Fetch();
            uint64 runId = f[0].Get<uint64>();
            uint32 dungeonId = f[1].Get<uint32>();
            int32 quality = f[2].Get<int32>();
            uint8 team = f[3].Get<uint8>();
            uint32 guildId = f[4].Get<uint32>();
            bool isRaid = IsRaidTemplate(dungeonId);

            QueryResult bossCountResult = CharacterDatabase.Query("SELECT COUNT(*) FROM playerbot_dungeon_run_boss WHERE run_id = {}", runId);
            uint8 bossCount = bossCountResult ? bossCountResult->Fetch()[0].Get<uint8>() : 0;

            uint8 result = RESULT_PARTIAL;
            uint8 killed = 0;
            uint32 roll = urand(1, 100);

            if (!SimFallback)
            {
                CharacterDatabase.Execute("UPDATE playerbot_dungeon_run SET state = {}, result = {}, bosses_killed = 0 WHERE id = {}",
                    uint8(RUN_FAILED), uint8(RESULT_ABANDONED), runId);
                continue;
            }

            if (bossCount == 0)
            {
                result = RESULT_ABANDONED;
                killed = 0;
            }
            else if (isRaid)
            {
                killed = CalculateRaidBossDepth(dungeonId, team, guildId, quality, bossCount);
                result = killed == bossCount ? RESULT_FULL_CLEAR : (killed > 0 ? RESULT_PARTIAL : RESULT_WIPE);
            }
            else if (quality >= 75)
            {
                result = roll <= 70 ? RESULT_FULL_CLEAR : RESULT_PARTIAL;
                killed = result == RESULT_FULL_CLEAR ? bossCount : urand(1, bossCount);
            }
            else if (quality >= 45)
            {
                result = roll <= 45 ? RESULT_FULL_CLEAR : (roll <= 85 ? RESULT_PARTIAL : RESULT_WIPE);
                killed = result == RESULT_FULL_CLEAR ? bossCount : (result == RESULT_PARTIAL ? urand(1, bossCount) : 0);
            }
            else
            {
                result = roll <= 15 ? RESULT_FULL_CLEAR : (roll <= 60 ? RESULT_PARTIAL : RESULT_WIPE);
                killed = result == RESULT_FULL_CLEAR ? bossCount : (result == RESULT_PARTIAL ? urand(1, bossCount) : 0);
            }

            CharacterDatabase.Execute("UPDATE playerbot_dungeon_run SET state = {}, result = {}, bosses_killed = {} WHERE id = {}",
                uint8((result == RESULT_WIPE || result == RESULT_ABANDONED) ? RUN_FAILED : RUN_COMPLETED), uint8(result), killed, runId);
            CharacterDatabase.Execute("UPDATE playerbot_dungeon_run_boss SET killed = 1 WHERE run_id = {} AND boss_order <= {}", runId, killed);
            if (isRaid)
                UpdateRaidProgress(dungeonId, team, guildId, killed);

            std::string dungeonName = GetDungeonTemplateName(dungeonId);
            StatusLog("END run #" + std::to_string(runId) + " " + TeamName(team) + " " + dungeonName +
                " result=" + std::to_string(result) + " bosses=" + std::to_string(killed) + "/" + std::to_string(bossCount) +
                " guild=" + std::to_string(guildId));

            if (GuildChatSimulation && guildId != 0 && urand(1, 100) <= GuildChatChance)
            {
                std::string msg = killed == bossCount && bossCount > 0
                    ? "Full clear in " + dungeonName + ". Loot was decent."
                    : (killed > 0 ? "We made progress in " + dungeonName + " - killed " + std::to_string(killed) + "/" + std::to_string(bossCount) + "."
                                  : "Rough run in " + dungeonName + ". We are calling it for now.");
                SendGuildSimulationMessage(guildId, "Dungeon Group", msg);
            }

            if (killed > 0)
                AwardLootForRun(runId, dungeonId);
        } while (runs->NextRow());
    }

    static void PrintConsoleStatus()
    {
        if (!ConsoleStatus)
            return;

        QueryResult active = CharacterDatabase.Query(
            "SELECT id, dungeon_template_id, team, guild_id, state, started_at, ends_at, quality_score, online_members_moved "
            "FROM playerbot_dungeon_run WHERE state IN (1,4) ORDER BY ends_at ASC LIMIT 25");

        if (!active)
        {
            StatusLog("STATUS no active dungeon/raid runs.");
            return;
        }

        uint32 now = GameTime::GetGameTime().count();
        StatusLog("STATUS active dungeon/raid runs:");
        do
        {
            Field* f = active->Fetch();
            uint64 runId = f[0].Get<uint64>();
            uint32 dungeonId = f[1].Get<uint32>();
            uint8 team = f[2].Get<uint8>();
            uint32 guildId = f[3].Get<uint32>();
            uint8 state = f[4].Get<uint8>();
            uint32 endsAt = f[6].Get<uint32>();
            int32 quality = f[7].Get<int32>();
            uint32 moved = f[8].Get<uint32>();
            uint32 remaining = endsAt > now ? endsAt - now : 0;

            QueryResult members = CharacterDatabase.Query("SELECT COUNT(*) FROM playerbot_dungeon_run_member WHERE run_id = {}", runId);
            uint32 memberCount = members ? members->Fetch()[0].Get<uint32>() : 0;

            StatusLog(" - #" + std::to_string(runId) + " " + TeamName(team) + " " + GetDungeonTemplateName(dungeonId) +
                " guild=" + std::to_string(guildId) + " members=" + std::to_string(memberCount) +
                " state=" + std::to_string(state) + " q=" + std::to_string(quality) +
                " moved=" + std::to_string(moved) + " eta=" + std::to_string(remaining / 60) + "m");
        } while (active->NextRow());
    }

    static void TryStartRuns()
    {
        for (uint32 made = 0; made < MaxGroupsPerTick; ++made)
        {
            uint8 team = urand(0, 1) == 0 ? TEAM_ALLIANCE : TEAM_HORDE;

            uint8 cap = GetEffectiveServerLevelCap();
            if (cap < MinBotLevel)
                return;

            uint8 level = urand(MinBotLevel, cap);
            bool raid = RaidMode && cap >= RaidMinServerLevelCap && level >= RaidMinServerLevelCap && urand(1, 100) <= RaidChanceAtCap;
            if (raid)
                level = cap;

            std::vector<DungeonTemplate> dungeons = LoadDungeonTemplates(team, level, raid);
            if (dungeons.empty())
                continue;

            DungeonTemplate dungeon = dungeons[urand(0, dungeons.size() - 1)];
            std::vector<BotCandidate> candidates = LoadEligibleBots(team, dungeon.MinLevel, dungeon.MaxLevel, dungeon.GroupSize * 10);
            std::vector<BotCandidate> group;

            if (BuildGroup(candidates, dungeon, group))
                CreateRun(dungeon, group);
        }
    }
}

class PlayerbotDungeonSimWorldScript : public WorldScript
{
public:
    PlayerbotDungeonSimWorldScript() : WorldScript("PlayerbotDungeonSimWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        using namespace PlayerbotDungeonSim;
        Enable = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.Enable", true);
        Debug = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.Debug", false);
        TickSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.TickSeconds", 60);
        MinBotLevel = sConfigMgr->GetOption<uint8>("PlayerbotDungeonSim.MinBotLevel", 10);
        MaxGroupsPerTick = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxGroupsPerTick", 2);
        PreferGuildGroups = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.PreferGuildGroups", true);
        AllowPugs = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AllowPugs", true);
        RequireFullGroup = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RequireFullGroup", true);
        MinDurationMinutes = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MinDurationMinutes", 20);
        MaxDurationMinutes = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxDurationMinutes", 45);
        RealRunFirst = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RealRunFirst", true);
        SimFallback = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.SimFallback", true);
        TeleportOnlineMembers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.TeleportOnlineMembers", true);
        CreateRealGroups = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.CreateRealGroups", true);
        TeleportOnlyIfOnline = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.TeleportOnlyIfOnline", false);
        ManualTravelForPlayerJoinedRuns = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.ManualTravelForPlayerJoinedRuns", true);
        AwardLoot = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AwardLoot", true);
        GiveLootToOnlinePlayers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.GiveLootToOnlinePlayers", true);
        EquipUsableLootOnline = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.EquipUsableLootOnline", false);
        DeliverPendingLootOnLogin = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.DeliverPendingLootOnLogin", true);
        MaxPendingDeliveriesPerLogin = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxPendingDeliveriesPerLogin", 12);
        EquipUpgradesOnDelivery = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.EquipUpgradesOnDelivery", false);
        OnlyEquipBetterItems = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.OnlyEquipBetterItems", true);
        InviteRealPlayers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.InviteRealPlayers", false);
        InviteOnlyIfGroupShort = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.InviteOnlyIfGroupShort", false);
        RequirePlayerPartyless = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RequirePlayerPartyless", true);
        RequirePlayerSameFaction = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RequirePlayerSameFaction", true);
        RequirePlayerLevelRange = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RequirePlayerLevelRange", true);
        InviteTimeoutSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.InviteTimeoutSeconds", 60);
        MaxRealPlayerInvitesPerRun = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxRealPlayerInvitesPerRun", 1);
        BotOnlyEligibilityFilter = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.BotOnlyEligibilityFilter", false);
        BotAccountPrefix = sConfigMgr->GetOption<std::string>("PlayerbotDungeonSim.BotAccountPrefix", "");
        BotAccountMin = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.BotAccountMin", 0);
        BotAccountMax = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.BotAccountMax", 0);
        RaidMode = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RaidMode", true);
        RaidMinServerLevelCap = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.RaidMinServerLevelCap", 60);
        RaidChanceAtCap = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.RaidChanceAtCap", 30);
        RaidWaiveBotAttunement = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RaidWaiveBotAttunement", true);
        RaidUseProgressMemory = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RaidUseProgressMemory", true);
        RaidLockouts = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RaidLockouts", true);
        RaidLockoutDays = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.RaidLockoutDays", 7);
        NormalDungeonLockouts = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.NormalDungeonLockouts", false);
        RespectProgressionPhase = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RespectProgressionPhase", true);
        ProgressionPhase = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.ProgressionPhase", 0);
        LootPerBossMin = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.LootPerBossMin", 1);
        LootPerBossMax = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.LootPerBossMax", 2);
        ConsoleStatus = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.ConsoleStatus", true);
        ConsoleStatusSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.ConsoleStatusSeconds", 120);
        GuildChatSimulation = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.GuildChatSimulation", true);
        GuildChatChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.GuildChatChance", 75);
        GuildChatToOnlineMembers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.GuildChatToOnlineMembers", true);
        GeneralChatSimulation = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.GeneralChatSimulation", true);
        GeneralChatChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.GeneralChatChance", 50);
        LocalZoneChatSimulation = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.LocalZoneChatSimulation", true);
        LocalZoneChatChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.LocalZoneChatChance", 35);
        SimulatedBotDeclineChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.SimulatedBotDeclineChance", 15);
        OrganicLfgActing = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.OrganicLfgActing", true);
        OrganicLfgChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.OrganicLfgChance", 85);
        RestrictOrganicCitiesToClassic = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RestrictOrganicCitiesToClassic", true);
        TradeChatSimulation = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.TradeChatSimulation", true);
        TradeChatChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.TradeChatChance", 20);
        TradeChatEveryTicks = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.TradeChatEveryTicks", 3);

        if (MinDurationMinutes > MaxDurationMinutes)
            std::swap(MinDurationMinutes, MaxDurationMinutes);
        if (LootPerBossMin > LootPerBossMax)
            std::swap(LootPerBossMin, LootPerBossMax);
        if (LootPerBossMin == 0)
            LootPerBossMin = 1;
        if (MaxPendingDeliveriesPerLogin == 0)
            MaxPendingDeliveriesPerLogin = 1;
        RaidChanceAtCap = std::min<uint32>(RaidChanceAtCap, 100);
        GuildChatChance = std::min<uint32>(GuildChatChance, 100);
        GeneralChatChance = std::min<uint32>(GeneralChatChance, 100);
        LocalZoneChatChance = std::min<uint32>(LocalZoneChatChance, 100);
        SimulatedBotDeclineChance = std::min<uint32>(SimulatedBotDeclineChance, 100);
        OrganicLfgChance = std::min<uint32>(OrganicLfgChance, 100);
        TradeChatChance = std::min<uint32>(TradeChatChance, 100);
        if (TradeChatEveryTicks == 0)
            TradeChatEveryTicks = 1;
        if (RaidMinServerLevelCap == 0)
            RaidMinServerLevelCap = 60;
        if (RaidLockoutDays == 0)
            RaidLockoutDays = 7;
        ProgressionPhase = std::min<uint32>(ProgressionPhase, 18);
    }

    void OnUpdate(uint32 diff) override
    {
        using namespace PlayerbotDungeonSim;
        if (!Enable)
            return;

        if (_timerMs <= diff)
        {
            _timerMs = TickSeconds * IN_MILLISECONDS;
            ExpireOldInvites();
            ResolveFinishedRuns();
            TryStartRuns();
        }
        else
            _timerMs -= diff;

        if (ConsoleStatus)
        {
            uint32 statusInterval = std::max<uint32>(ConsoleStatusSeconds, 30) * IN_MILLISECONDS;
            if (_statusTimerMs <= diff)
            {
                _statusTimerMs = statusInterval;
                PrintConsoleStatus();
            }
            else
                _statusTimerMs -= diff;
        }
    }
};

class PlayerbotDungeonSimPlayerScript : public PlayerScript
{
public:
    PlayerbotDungeonSimPlayerScript()
        : PlayerScript("PlayerbotDungeonSimPlayerScript",
            {
                PLAYERHOOK_ON_LOGIN,
                PLAYERHOOK_CAN_PLAYER_USE_CHAT
            })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        PlayerbotDungeonSim::DeliverPendingLoot(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg) override
    {
        (void)type;
        (void)language;

        // This core does not expose OnPlayerChat. It exposes OnPlayerCanUseChat,
        // so consume invite responses here before normal chat is sent.
        if (PlayerbotDungeonSim::HandleInviteChat(player, msg))
            return false;

        return true;
    }
};

void AddSC_mod_playerbot_dungeon_sim()
{
    new PlayerbotDungeonSimWorldScript();
    new PlayerbotDungeonSimPlayerScript();
}

// AzerothCore module loader symbol.
// The generated ModulesLoader.cpp expects this exact name from the module folder:
// mod-playerbot-dungeon-sim -> Addmod_playerbot_dungeon_simScripts
void Addmod_playerbot_dungeon_simScripts()
{
    AddSC_mod_playerbot_dungeon_sim();
}
