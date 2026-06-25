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
#include "MotionMaster.h"

#include <algorithm>
#include <vector>
#include <unordered_set>
#include <cctype>
#include <string>
#include <iterator>
#include <map>
#include <sstream>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ctime>

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

    enum RunExecutionMode : uint8
    {
        RUN_MODE_SIM = 0,
        RUN_MODE_REAL = 1
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
        uint32 AccountId = 0;
        bool DbOnline = false;
        std::string Name;
        uint8 Level = 1;
        uint8 Race = 0;
        uint8 Class = 0;
        uint8 Team = TEAM_ALLIANCE;
        uint32 GuildId = 0;
        Role PreferredRole = ROLE_DPS;
    };

    struct TeleportLocation
    {
        uint32 Map = 0;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float O = 0.0f;
        bool FromInstanceStart = false;
    };

    struct ProgressiveTravelStep
    {
        uint32 GuidLow = 0;
        uint32 AccountId = 0;
        std::string Name;
        std::string DungeonName;
        uint32 Map = 0;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float O = 0.0f;
        uint32 ExecuteAt = 0;
        uint8 Step = 0;
        uint8 Total = 0;
    };

    struct NativeEventQueueStep
    {
        uint64 RunId = 0;
        uint32 DungeonId = 0;
        uint32 GuidLow = 0;
        uint32 AccountId = 0;
        std::string Name;
        std::string DungeonName;
        uint32 StepOrder = 0;
        std::string StepType;
        uint32 Map = 0;
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float O = 0.0f;
        uint32 ExecuteAt = 0;
        uint8 RetryCount = 0;
        std::string Label;
    };

    struct NativeFollowerLeashStep
    {
        uint64 RunId = 0;
        uint32 LeaderGuidLow = 0;
        uint32 LeaderAccountId = 0;
        std::string LeaderName;
        std::vector<BotCandidate> Members;
        uint32 ExecuteAt = 0;
        uint32 Sequence = 0;
        uint8 RetryCount = 0;
    };

    static bool Enable;
    static bool Debug;
    static bool AuditFileLog;
    static std::string AuditFilePath;
    static bool AuditFileMovement;
    static bool AuditFileLoot;
    static bool AuditFileMembers;
    static uint32 TickSeconds;
    static uint8 MinBotLevel;
    static uint32 MaxGroupsPerTick;
    static RunExecutionMode ConfiguredRunMode;
    static bool PreferGuildGroups;
    static bool AllowPugs;
    static bool RequireFullGroup;
    static uint32 MinDurationMinutes;
    static uint32 MaxDurationMinutes;
    static bool RealRunFirst;
    static bool SimFallback;
    static bool HybridSimReal;
    static uint32 HybridRealChance;
    static bool HybridRealFallbackToSim;
    static uint32 SimMinimumBossKillsForLoot;
    static bool SimMinimumBossKillsApplyToRaids;
    static bool TeleportOnlineMembers;
    static bool CreateRealGroups;
    static bool TeleportOnlyIfOnline;
    static bool TeleportInsideInstance;
    static bool ProgressiveTeleport;
    static uint32 ProgressiveTeleportStepSeconds;
    static bool InstanceWaypointTeleport;
    static uint32 InstanceWaypointStepSeconds;
    static bool NativeEventSteps;
    static uint32 NativeEventStepSeconds;
    static bool NativeEventMoveInsteadTeleport;
    static bool NativeEventLeaderOnly;
    static bool NativeFollowerLeash;
    static uint32 NativeFollowerLeashSeconds;
    static float NativeFollowerLeashDistance;
    static float NativeFollowerHardTeleportDistance;
    static uint32 MinOnlineMembersForLiveRun;
    static bool RequireOnlineBotsForRun;
    static bool CleanupOfflineRunsWhenLiveOnly;
    static bool StatusShowOnlineMembers;
    static bool PreferOnlineCandidatesForLiveRuns;
    static bool LiveDebugRoster;
    static uint32 LiveDebugMaxRosterLines;
    static bool ManualTravelForPlayerJoinedRuns;
    static bool AllowOnlineCharacterTouch;
    static uint32 StartupDelaySeconds;
    static bool AwardLoot;
    static bool GiveLootToOnlinePlayers;
    static bool EquipUsableLootOnline;
    static bool DeliverPendingLootOnLogin;
    static uint32 MaxPendingDeliveriesPerLogin;
    static bool AwardXp;
    static bool GiveXpToOnlinePlayers;
    static bool DeliverPendingXpOnLogin;
    static bool XpScaleByBossKills;
    static uint32 XpPercentOfLevel;
    static uint32 XpMinimumPercentOnAnyKill;
    static uint32 XpLevelCap;
    static uint32 MaxPendingXpDeliveriesPerLogin;
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
    static bool UsePlayerbotConfig;
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
    static uint32 SimulatedBotDeclineMaxPerGroup;
    static bool SimulatedBotDeclineBlocksGroup;
    static bool OrganicLfgActing;
    static uint32 OrganicLfgChance;
    static bool RestrictOrganicCitiesToClassic;
    static bool StructuredLfgSimulation;
    static uint32 StructuredLfgChance;
    static uint32 StructuredLfgTradeChance;
    static bool AutoUseGmGroupSelection;
    static uint32 ReserveRealPlayerSlotChance;
    static bool PlayerWhisperJoin;
    static bool PlayerWhisperJoinRequiresRole;
    static bool PlayerWhisperJoinAllowGm;
    static bool TradeChatSimulation;
    static uint32 TradeChatChance;
    static uint32 TradeChatEveryTicks;
    static uint32 _tradeTickCounter = 0;
    static std::map<std::string, std::vector<BotCandidate>> _gmStagedGroups;
    static std::vector<ProgressiveTravelStep> _progressiveTravelQueue;
    static std::vector<NativeEventQueueStep> _nativeEventQueue;
    static std::vector<NativeFollowerLeashStep> _nativeFollowerLeashQueue;

    static uint32 _timerMs = 0;
    static uint32 _statusTimerMs = 0;
    static uint32 _startupElapsedMs = 0;
    static bool _startupDelayLogged = false;

    static uint32 GetRunGuildId(std::vector<BotCandidate> const& group);

    static std::string AuditTimestamp()
    {
        std::time_t now = std::time(nullptr);
        std::tm localTime;
#ifdef _WIN32
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif

        std::ostringstream ss;
        ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    static void AuditFileWrite(std::string const& category, std::string const& msg)
    {
        if (!AuditFileLog)
            return;

        std::string path = AuditFilePath.empty() ? "Logs/playerbot_dungeon_sim.log" : AuditFilePath;
        std::ofstream out(path, std::ios::app);
        if (!out.is_open())
            return;

        out << "[" << AuditTimestamp() << "] [" << category << "] " << msg << '\n';
    }

    static void AuditLog(std::string const& category, std::string const& msg)
    {
        AuditFileWrite(category, msg);
    }

    static void DebugLog(std::string const& msg)
    {
        if (Debug)
        {
            LOG_INFO("module", "[PlayerbotDungeonSim] {}", msg);
            AuditFileWrite("DEBUG", msg);
        }
    }

    static void StatusLog(std::string const& msg)
    {
        LOG_INFO("module", "[PlayerbotDungeonSim] {}", msg);
        AuditFileWrite("INFO", msg);
    }



    static bool VerifyDatabaseSchema()
    {
        struct RequiredTable
        {
            char const* DbName;
            char const* TableName;
            bool IsCharacterDb;
        };

        RequiredTable required[] =
        {
            { "characters", "playerbot_dungeon_run", true },
            { "characters", "playerbot_dungeon_run_member", true },
            { "characters", "playerbot_dungeon_run_boss", true },
            { "characters", "playerbot_dungeon_loot_award", true },
            { "characters", "playerbot_dungeon_xp_award", true },
            { "characters", "playerbot_dungeon_player_invite", true },
            { "characters", "playerbot_dungeon_raid_progress", true },
            { "characters", "playerbot_dungeon_lockout", true },
            { "world", "playerbot_dungeon_template", false },
            { "world", "playerbot_dungeon_boss_template", false },
            { "world", "playerbot_dungeon_waypoint", false },
            { "world", "playerbot_dungeon_event_step", false }
        };

        bool ok = true;
        for (RequiredTable const& t : required)
        {
            QueryResult result = t.IsCharacterDb
                ? CharacterDatabase.Query("SELECT 1 FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}' LIMIT 1", t.TableName)
                : WorldDatabase.Query("SELECT 1 FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}' LIMIT 1", t.TableName);

            if (!result)
            {
                LOG_ERROR("module", "[PlayerbotDungeonSim] SQL schema missing: {} table `{}` was not found. Import/apply the module SQL before enabling live runs.", t.DbName, t.TableName);
                ok = false;
            }
        }

        if (!ok)
            LOG_ERROR("module", "[PlayerbotDungeonSim] Disabled because required SQL tables are missing. Check data/sql/db-characters/updates and data/sql/db-world/updates, or run the manual install SQL against the correct databases.");

        return ok;
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

    static std::string ToLowerCopy(std::string value);
    static std::string LowerCopy(std::string msg);
    static std::string TrimCopy(std::string value);
    static RunExecutionMode ParseRunModeToken(std::string const& raw, RunExecutionMode fallback, bool* matched = nullptr);
    static std::string RunModeText(RunExecutionMode mode);

    static std::string NormalizeToken(std::string value)
    {
        value = ToLowerCopy(value);
        value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c)
        {
            return std::isspace(c) || c == '-' || c == '_' || c == '\'' || c == '`' || c == '.';
        }), value.end());
        return value;
    }


    static RunExecutionMode ParseRunModeToken(std::string const& raw, RunExecutionMode fallback, bool* matched)
    {
        std::string key = NormalizeToken(raw);
        bool ok = true;
        RunExecutionMode mode = fallback;

        if (key == "sim" || key == "simulation" || key == "simulated" || key == "timed" || key == "fake" || key == "offline")
            mode = RUN_MODE_SIM;
        else if (key == "real" || key == "live" || key == "native" || key == "walk" || key == "clear")
            mode = RUN_MODE_REAL;
        else
            ok = false;

        if (matched)
            *matched = ok;
        return ok ? mode : fallback;
    }

    static std::string RunModeText(RunExecutionMode mode)
    {
        return mode == RUN_MODE_REAL ? "REAL" : "SIM";
    }

    static std::string ResolveDungeonAlias(std::string const& rawName)
    {
        std::string key = NormalizeToken(rawName);

        if (key == "rfc" || key == "ragefire") return "Ragefire Chasm";
        if (key == "wc" || key == "wailing") return "Wailing Caverns";
        if (key == "dm" || key == "vc" || key == "deadmines" || key == "thedeadmines" || key == "deadmine") return "Deadmines";
        if (key == "sfk") return "Shadowfang Keep";
        if (key == "bfd") return "Blackfathom Deeps";
        if (key == "stocks" || key == "stockade" || key == "stockades" || key == "thestockade" || key == "thestockades" || key == "stk") return "The Stockade";
        if (key == "gnomer" || key == "gnomeregan" || key == "gno") return "Gnomeregan";
        if (key == "rfk") return "Razorfen Kraul";
        if (key == "rfd") return "Razorfen Downs";
        if (key == "smgy" || key == "smgraveyard" || key == "graveyard") return "Scarlet Monastery Graveyard";
        if (key == "smlib" || key == "smlibrary" || key == "library") return "Scarlet Monastery Library";
        if (key == "smarm" || key == "smarmory" || key == "armory") return "Scarlet Monastery Armory";
        if (key == "smcath" || key == "smcathedral" || key == "cathedral") return "Scarlet Monastery Cathedral";
        if (key == "zf") return "Zul'Farrak";
        if (key == "mara" || key == "maraudon") return "Maraudon";
        if (key == "st" || key == "sunken" || key == "sunkentemple") return "Sunken Temple";
        if (key == "brd") return "Blackrock Depths";
        if (key == "lbrs") return "Lower Blackrock Spire";
        if (key == "ubrs") return "Upper Blackrock Spire";
        if (key == "dme" || key == "diremauleast") return "Dire Maul East";
        if (key == "dmw" || key == "diremaulwest") return "Dire Maul West";
        if (key == "dmn" || key == "dmt" || key == "diremaulnorth") return "Dire Maul North";
        if (key == "scholo" || key == "scholomance") return "Scholomance";
        if (key == "strat" || key == "strath" || key == "stratholme") return "Stratholme";
        if (key == "mc") return "Molten Core";
        if (key == "ony" || key == "onyxia") return "Onyxia's Lair";
        if (key == "bwl") return "Blackwing Lair";
        if (key == "zg") return "Zul'Gurub";
        if (key == "aq20" || key == "raq") return "Ruins of Ahn'Qiraj";
        if (key == "aq40" || key == "taq") return "Temple of Ahn'Qiraj";
        if (key == "naxx" || key == "naxx40") return "Naxxramas 40";

        return rawName;
    }

    static uint8 DungeonFactionWeight(DungeonTemplate const& dungeon, uint8 team)
    {
        switch (dungeon.Id)
        {
            case 1:  // RFC, inside Orgrimmar
                return team == TEAM_HORDE ? 100 : 0;
            case 2:  // WC, Barrens
                return team == TEAM_HORDE ? 90 : 10;
            case 3:  // Deadmines, Westfall
                return team == TEAM_ALLIANCE ? 90 : 10;
            case 4:  // SFK, Silverpine
                return team == TEAM_HORDE ? 90 : 10;
            case 6:  // Stockades, Stormwind
                return team == TEAM_ALLIANCE ? 100 : 0;
            case 7:  // Gnomeregan
                return team == TEAM_ALLIANCE ? 85 : 15;
            case 8:  // RFK
            case 13: // RFD
                return team == TEAM_HORDE ? 70 : 30;
            case 9:
            case 10:
            case 11:
            case 12:
                return team == TEAM_HORDE ? 55 : 45;
            default:
                return 100;
        }
    }

    static bool IsDungeonFactionAllowedForTeam(DungeonTemplate const& dungeon, uint8 team, bool useWeightedRoll)
    {
        uint8 weight = DungeonFactionWeight(dungeon, team);
        if (weight == 0)
            return false;
        if (!useWeightedRoll || weight >= 100)
            return true;
        return urand(1, 100) <= weight;
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

    static bool IsAutoValue(std::string const& value)
    {
        std::string v = ToLowerCopy(value);
        return v.empty() || v == "auto" || v == "playerbots" || v == "playerbot";
    }

    static void ImportPlayerbotConfigDefaults()
    {
        if (!UsePlayerbotConfig)
            return;

        bool aiPlayerbotEnabled = sConfigMgr->GetOption<bool>("AiPlayerbot.Enabled", true);
        std::string playerbotPrefix = sConfigMgr->GetOption<std::string>("AiPlayerbot.RandomBotAccountPrefix", "rndbot");
        uint32 playerbotAccountCount = sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotAccountCount", 0);
        bool playerbotBotAutologin = sConfigMgr->GetOption<bool>("AiPlayerbot.BotAutologin", false);
        bool playerbotRandomAutologin = sConfigMgr->GetOption<bool>("AiPlayerbot.RandomBotAutologin", true);
        uint32 playerbotMinRandomBots = sConfigMgr->GetOption<uint32>("AiPlayerbot.MinRandomBots", 0);
        uint32 playerbotMaxRandomBots = sConfigMgr->GetOption<uint32>("AiPlayerbot.MaxRandomBots", 0);
        bool disabledWithoutRealPlayer = sConfigMgr->GetOption<bool>("AiPlayerbot.DisabledWithoutRealPlayer", false);
        uint32 loginDelay = sConfigMgr->GetOption<uint32>("AiPlayerbot.DisabledWithoutRealPlayerLoginDelay", 30);

        if (IsAutoValue(BotAccountPrefix))
            BotAccountPrefix = playerbotPrefix;

        // Prefix filtering is always required when we borrow Playerbot settings. This prevents accidental real-char selection.
        if (!BotAccountPrefix.empty())
            BotOnlyEligibilityFilter = true;

        if (StartupDelaySeconds < loginDelay)
            StartupDelaySeconds = loginDelay;

        LOG_INFO("module",
            "[PlayerbotDungeonSim] Playerbot config import: enabled={} randomPrefix='{}' accountCount={} botAutologin={} randomBotAutologin={} randomBots={}..{} disabledWithoutRealPlayer={} startupDelay={}s",
            aiPlayerbotEnabled ? 1 : 0, BotAccountPrefix, playerbotAccountCount, playerbotBotAutologin ? 1 : 0,
            playerbotRandomAutologin ? 1 : 0, playerbotMinRandomBots, playerbotMaxRandomBots, disabledWithoutRealPlayer ? 1 : 0, StartupDelaySeconds);
    }

    static bool AccountMatchesBotFilter(uint32 accountId)
    {
        if (!BotOnlyEligibilityFilter)
            return true;

        bool hasPrefix = !BotAccountPrefix.empty();
        bool hasRange = BotAccountMax >= BotAccountMin && BotAccountMax > 0;

        if (!hasPrefix && !hasRange)
            return false;

        if (hasRange && (accountId < BotAccountMin || accountId > BotAccountMax))
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

            if (IsDungeonAllowed(d.MapId) && IsRaidAllowedByServerCap(d) && IsDungeonFactionAllowedForTeam(d, team, true))
                out.push_back(d);
        } while (result->NextRow());

        return out;
    }

    static std::vector<BotCandidate> LoadEligibleBots(uint8 team, uint8 minLevel, uint8 maxLevel, uint32 limit)
    {
        std::vector<BotCandidate> out;

        // Bot safety filter is applied after row load so we can check auth.account.username via LoginDatabase.
        // In live-only testing, prefer characters that the characters table already marks online.
        // This avoids selecting five random offline DB bots when only a small number of Playerbots are actually loaded.
        uint32 rowLimit = (RequireOnlineBotsForRun && PreferOnlineCandidatesForLiveRuns) ? limit * 24 : limit * 8;
        std::string order = (RequireOnlineBotsForRun && PreferOnlineCandidatesForLiveRuns) ? "c.online DESC, RAND()" : "RAND()";

        QueryResult result = CharacterDatabase.Query(Acore::StringFormat(
            "SELECT c.guid, c.name, c.level, c.race, c.class, COALESCE(g.guildid,0), c.account, c.online "
            "FROM characters c "
            "LEFT JOIN guild_member g ON g.guid = c.guid "
            "WHERE c.level BETWEEN {} AND {} "
            "AND NOT EXISTS ("
            "  SELECT 1 FROM playerbot_dungeon_run_member rm "
            "  JOIN playerbot_dungeon_run r ON r.id = rm.run_id "
            "  WHERE rm.guid = c.guid AND r.state IN (0,1,4)"
            ") "
            "ORDER BY {} LIMIT {}",
            minLevel, maxLevel, order, rowLimit));

        if (!result)
            return out;

        std::unordered_set<uint32> seen;
        do
        {
            Field* f = result->Fetch();
            uint32 guidLow = f[0].Get<uint32>();
            if (!seen.insert(guidLow).second)
                continue;

            BotCandidate b;
            b.GuidLow = guidLow;
            b.Name = f[1].Get<std::string>();
            b.Level = f[2].Get<uint8>();
            b.Race = f[3].Get<uint8>();
            b.Class = f[4].Get<uint8>();
            b.GuildId = f[5].Get<uint32>();
            b.AccountId = f[6].Get<uint32>();
            b.DbOnline = f[7].Get<uint8>() != 0;
            b.Team = GetTeamFromRace(b.Race);
            b.PreferredRole = GuessRole(b.Class);

            if (b.Team == team && b.Level >= MinBotLevel && AccountMatchesBotFilter(b.AccountId))
                out.push_back(b);
        } while (result->NextRow());

        // Defensive dedupe: older DB states may have many historical run_member rows for the same bot.
        // Group building must never use the same character twice in one party.
        std::unordered_set<uint32> seenGuids;
        out.erase(std::remove_if(out.begin(), out.end(), [&](BotCandidate const& b)
        {
            if (seenGuids.find(b.GuidLow) != seenGuids.end())
                return true;
            seenGuids.insert(b.GuidLow);
            return false;
        }), out.end());

        if (Debug && RequireOnlineBotsForRun && LiveDebugRoster)
        {
            uint32 dbOnline = 0;
            for (BotCandidate const& b : out)
                if (b.DbOnline)
                    ++dbOnline;

            DebugLog("Candidate scan " + TeamName(team) + " levels " + std::to_string(minLevel) + "-" + std::to_string(maxLevel) +
                ": eligible=" + std::to_string(out.size()) + " dbOnline=" + std::to_string(dbOnline) +
                " prefix='" + BotAccountPrefix + "'.");
        }

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

    static bool RollSimulatedDecline()
    {
        return SimulatedBotDeclineChance != 0 && urand(1, 100) <= SimulatedBotDeclineChance;
    }

    static void LogSimulatedDecline(BotCandidate const& bot, DungeonTemplate const& dungeon)
    {
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
    }

    static bool ShouldSimulatedBotDecline(BotCandidate const& bot, DungeonTemplate const& dungeon, uint32& declineCount)
    {
        // By default declines are flavor only. They should not keep a valid group from forming.
        if (!SimulatedBotDeclineBlocksGroup)
            return false;

        if (SimulatedBotDeclineMaxPerGroup != 0 && declineCount >= SimulatedBotDeclineMaxPerGroup)
            return false;

        if (!RollSimulatedDecline())
            return false;

        ++declineCount;
        LogSimulatedDecline(bot, dungeon);
        return true;
    }

    static void EmitSimulatedDeclineFlavor(std::vector<BotCandidate> const& candidates, std::vector<BotCandidate> const& group, DungeonTemplate const& dungeon)
    {
        if (SimulatedBotDeclineChance == 0 || SimulatedBotDeclineMaxPerGroup == 0)
            return;

        uint32 declined = 0;
        for (BotCandidate const& b : candidates)
        {
            if (declined >= SimulatedBotDeclineMaxPerGroup)
                break;

            bool inGroup = std::any_of(group.begin(), group.end(), [&](BotCandidate const& g) { return g.GuidLow == b.GuidLow; });
            if (inGroup)
                continue;

            if (!RollSimulatedDecline())
                continue;

            ++declined;
            LogSimulatedDecline(b, dungeon);
        }
    }

    static bool BuildGroup(std::vector<BotCandidate>& candidates, DungeonTemplate const& dungeon, std::vector<BotCandidate>& group)
    {
        group.clear();
        uint32 declineCount = 0;
        if (candidates.size() < dungeon.GroupSize && RequireFullGroup)
            return false;

        std::sort(candidates.begin(), candidates.end(), [](BotCandidate const& a, BotCandidate const& b)
        {
            // If we are allowed to move/teleport online bots, prefer characters that are actually online.
            // This keeps SIM runs visible in /who and lets the same group also receive direct gear.
            if (AllowOnlineCharacterTouch && TeleportOnlineMembers && PreferOnlineCandidatesForLiveRuns && a.DbOnline != b.DbOnline)
                return a.DbOnline > b.DbOnline;
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
                std::unordered_set<uint32> sameGuildGuids;
                for (BotCandidate const& b : candidates)
                {
                    if (b.GuildId != seed.GuildId)
                        continue;
                    if (sameGuildGuids.find(b.GuidLow) != sameGuildGuids.end())
                        continue;
                    if (ShouldSimulatedBotDecline(b, dungeon, declineCount))
                        continue;

                    sameGuildGuids.insert(b.GuidLow);
                    sameGuild.push_back(b);
                }

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
            bool exists = std::any_of(group.begin(), group.end(), [&](BotCandidate const& g) { return g.GuidLow == b.GuidLow; });
            if (!exists && !hasTank && b.PreferredRole == ROLE_TANK && !ShouldSimulatedBotDecline(b, dungeon, declineCount))
            {
                group.push_back(b); hasTank = true;
            }
        }
        for (BotCandidate const& b : candidates)
        {
            if (group.size() >= dungeon.GroupSize)
                break;
            bool exists = std::any_of(group.begin(), group.end(), [&](BotCandidate const& g) { return g.GuidLow == b.GuidLow; });
            if (!exists && !hasHealer && b.PreferredRole == ROLE_HEALER && !ShouldSimulatedBotDecline(b, dungeon, declineCount))
            {
                group.push_back(b); hasHealer = true;
            }
        }
        for (BotCandidate const& b : candidates)
        {
            if (group.size() >= dungeon.GroupSize)
                break;
            bool exists = std::any_of(group.begin(), group.end(), [&](BotCandidate const& g) { return g.GuidLow == b.GuidLow; });
            if (!exists && !ShouldSimulatedBotDecline(b, dungeon, declineCount))
                group.push_back(b);
        }

        bool success = group.size() == dungeon.GroupSize || (!RequireFullGroup && !group.empty());
        if (success && !SimulatedBotDeclineBlocksGroup)
            EmitSimulatedDeclineFlavor(candidates, group, dungeon);

        return success;
    }


    static Player* FindOnlinePlayer(uint32 guidLow)
    {
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(guidLow);
        if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
            return player;

        // Playerbots on some branches may be in-world without a normal connected client session.
        // FindPlayer is a broader lookup and lets live-test mode see those bot Player objects.
        return ObjectAccessor::FindPlayer(guid);
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

    static std::string ClassNameText(uint8 cls)
    {
        switch (cls)
        {
            case CLASS_WARRIOR: return "warrior";
            case CLASS_PALADIN: return "paladin";
            case CLASS_HUNTER: return "hunter";
            case CLASS_ROGUE: return "rogue";
            case CLASS_PRIEST: return "priest";
            case CLASS_SHAMAN: return "shaman";
            case CLASS_MAGE: return "mage";
            case CLASS_WARLOCK: return "warlock";
            case CLASS_DRUID: return "druid";
            default: return "class" + std::to_string(uint32(cls));
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


    static std::string DungeonShortCode(DungeonTemplate const& dungeon)
    {
        std::string key = NormalizeToken(dungeon.Name);
        if (key == "ragefirechasm") return "RFC";
        if (key == "wailingcaverns") return "WC";
        if (key == "deadmines") return "DM";
        if (key == "shadowfangkeep") return "SFK";
        if (key == "blackfathomdeeps") return "BFD";
        if (key == "thestockade" || key == "thestockades") return "Stocks";
        if (key == "gnomeregan") return "Gnomer";
        if (key == "razorfenkraul") return "RFK";
        if (key == "razorfendowns") return "RFD";
        if (key == "scarletmonasterygraveyard") return "SMGY";
        if (key == "scarletmonasterylibrary") return "SMLib";
        if (key == "scarletmonasteryarmory") return "SMArm";
        if (key == "scarletmonasterycathedral") return "SMCath";
        if (key == "zulfarrak") return "ZF";
        if (key == "maraudon") return "Mara";
        if (key == "sunkentemple") return "ST";
        if (key == "blackrockdepths") return "BRD";
        if (key == "lowerblackrockspire") return "LBRS";
        if (key == "upperblackrockspire") return "UBRS";
        if (key == "diremauleast") return "DME";
        if (key == "diremaulwest") return "DMW";
        if (key == "diremaulnorth") return "DMN";
        if (key == "scholomance") return "Scholo";
        if (key == "stratholme") return "Strat";
        if (key == "moltencore") return "MC";
        if (key == "onyxiaslair") return "Ony";
        if (key == "blackwinglair") return "BWL";
        if (key == "zulgurub") return "ZG";
        if (key == "ruinsofahrqiraj") return "AQ20";
        if (key == "templeofahnqiraj") return "AQ40";
        if (key == "naxxramas40") return "Naxx";
        return dungeon.Name;
    }

    static std::string NeededRolesText(std::vector<BotCandidate> const& group, DungeonTemplate const& dungeon)
    {
        uint32 tanks = 0, heals = 0, dps = 0;
        for (BotCandidate const& b : group)
        {
            if (b.PreferredRole == ROLE_TANK) ++tanks;
            else if (b.PreferredRole == ROLE_HEALER) ++heals;
            else ++dps;
        }

        std::vector<std::string> parts;
        if (tanks < 1) parts.push_back("tank");
        if (heals < 1) parts.push_back("heals");
        uint32 targetDps = dungeon.GroupSize >= 5 ? dungeon.GroupSize - 2 : 1;
        if (dps < targetDps)
            parts.push_back(std::to_string(targetDps - dps) + " dps");

        if (parts.empty())
            return "last spot / backup";

        std::string out;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i) out += "/";
            out += parts[i];
        }
        return out;
    }

    static void MaybeReservePlayerSlot(std::vector<BotCandidate>& group, DungeonTemplate const& dungeon)
    {
        if (!InviteRealPlayers || ReserveRealPlayerSlotChance == 0 || group.size() < dungeon.GroupSize)
            return;

        if (urand(1, 100) > ReserveRealPlayerSlotChance)
            return;

        // Prefer reserving a DPS slot, not the only tank/healer.
        for (auto itr = group.rbegin(); itr != group.rend(); ++itr)
        {
            if (itr->PreferredRole == ROLE_DPS)
            {
                StatusLog("[LFGSim] " + itr->Name + " is holding off so a real player can fill a " + dungeon.Name + " slot.");
                group.erase(std::next(itr).base());
                return;
            }
        }
    }

    static void SendStructuredLfgSimulation(std::vector<BotCandidate> const& group, DungeonTemplate const& dungeon)
    {
        if (!StructuredLfgSimulation || group.empty())
            return;

        if (urand(1, 100) > StructuredLfgChance)
            return;

        BotCandidate const& leader = group.front();
        std::string code = DungeonShortCode(dungeon);
        std::string city = ClassicCapitalCityName(leader.Team);
        std::string need = NeededRolesText(group, dungeon);

        // Leader acts like the organizer. Members act like they are responding to the ad.
        SendAmbientChatMessage("GeneralSim:" + city, leader.Name, "LFM " + code + " need " + need + " pst role");

        if (TradeChatSimulation && urand(1, 100) <= StructuredLfgTradeChance)
            SendAmbientChatMessage("TradeSim:" + city, leader.Name, "LFM " + code + " - " + TeamName(leader.Team) + " group forming, whisper role");

        uint32 maxLines = std::min<uint32>(uint32(group.size()), 5);
        for (uint32 i = 0; i < maxLines; ++i)
        {
            BotCandidate const& b = group[i];
            std::string spec = ClassSpecText(b.Class, b.PreferredRole);
            std::string msg;

            if (i == 0)
            {
                msg = "inviting for " + code + ", say tank/heals/dps";
            }
            else if (b.PreferredRole == ROLE_TANK)
            {
                msg = urand(0, 1) ? "LFG " + code + " tank" : "I can tank " + code + " - " + spec;
            }
            else if (b.PreferredRole == ROLE_HEALER)
            {
                msg = urand(0, 1) ? "LFG " + code + " heals" : spec + " LFG " + code;
            }
            else
            {
                msg = urand(0, 1) ? "LFG " + code + " dps" : spec + " dps for " + code;
            }

            SendAmbientChatMessage("GeneralSim:" + city, b.Name, msg);
        }

        if (LocalZoneChatSimulation && urand(1, 100) <= LocalZoneChatChance)
        {
            BotCandidate const& b = group[urand(0, uint32(group.size() - 1))];
            SendAmbientChatMessage("LocalSim:" + dungeon.Name, b.Name, "LFG/LFM " + code + " at zone, pst role");
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

    static bool IsPlayerBotAccount(Player* player)
    {
        if (!player || !player->GetSession())
            return false;

        return AccountMatchesBotFilter(player->GetSession()->GetAccountId());
    }

    static TeleportLocation ResolveTeleportLocation(DungeonTemplate const& dungeon)
    {
        TeleportLocation loc;
        loc.Map = dungeon.EntranceMap;
        loc.X = dungeon.X;
        loc.Y = dungeon.Y;
        loc.Z = dungeon.Z;
        loc.O = dungeon.O;
        loc.FromInstanceStart = false;

        if (!TeleportInsideInstance)
            return loc;

        // Prefer the real instance start from AzerothCore's areatrigger_teleport table.
        // This keeps the module branch-friendly and avoids hardcoding every dungeon's internal coords.
        QueryResult at = WorldDatabase.Query(
            "SELECT target_map, target_position_x, target_position_y, target_position_z, target_orientation "
            "FROM areatrigger_teleport WHERE target_map = {} LIMIT 1",
            dungeon.MapId);

        if (!at)
        {
            DebugLog("No areatrigger_teleport instance-start found for " + dungeon.Name + "; using outdoor entrance coords.");
            return loc;
        }

        Field* f = at->Fetch();
        loc.Map = f[0].Get<uint32>();
        loc.X = f[1].Get<float>();
        loc.Y = f[2].Get<float>();
        loc.Z = f[3].Get<float>();
        loc.O = f[4].Get<float>();
        loc.FromInstanceStart = true;
        return loc;
    }

    static TeleportLocation MakeTeleportLocation(uint32 map, float x, float y, float z, float o, bool instanceStart = false)
    {
        TeleportLocation loc;
        loc.Map = map;
        loc.X = x;
        loc.Y = y;
        loc.Z = z;
        loc.O = o;
        loc.FromInstanceStart = instanceStart;
        return loc;
    }

    static std::vector<TeleportLocation> BuildProgressiveRoute(uint8 team, DungeonTemplate const& dungeon, TeleportLocation const& finalLoc)
    {
        std::vector<TeleportLocation> route;

        if (team == TEAM_ALLIANCE)
        {
            route.push_back(MakeTeleportLocation(0, -8833.38f, 628.62f, 94.00f, 0.50f)); // Stormwind

            if (dungeon.Id == 3) // Deadmines: SW -> Goldshire -> Westfall -> portal -> instance
            {
                route.push_back(MakeTeleportLocation(0, -9464.00f, 64.00f, 56.00f, 3.00f));
                route.push_back(MakeTeleportLocation(0, -10508.00f, 1047.00f, 60.50f, 1.60f));
                route.push_back(MakeTeleportLocation(dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O));
            }
            else if (dungeon.Id == 6) // Stockades
            {
                route.push_back(MakeTeleportLocation(dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O));
            }
            else
            {
                route.push_back(MakeTeleportLocation(dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O));
            }
        }
        else
        {
            route.push_back(MakeTeleportLocation(1, 1665.00f, -4343.00f, 61.00f, 3.20f)); // Orgrimmar

            if (dungeon.Id == 1) // RFC
            {
                route.push_back(MakeTeleportLocation(dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O));
            }
            else if (dungeon.Id == 2) // WC: Orgrimmar -> Crossroads-ish -> cave -> instance
            {
                route.push_back(MakeTeleportLocation(1, -456.00f, -2652.00f, 95.00f, 1.20f));
                route.push_back(MakeTeleportLocation(dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O));
            }
            else if (dungeon.Id == 4) // SFK: UC/Silverpine flavor via outside coords
            {
                route.push_back(MakeTeleportLocation(0, 1586.00f, 239.00f, -52.00f, 0.00f));
                route.push_back(MakeTeleportLocation(dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O));
            }
            else
            {
                route.push_back(MakeTeleportLocation(dungeon.EntranceMap, dungeon.X, dungeon.Y, dungeon.Z, dungeon.O));
            }
        }

        if (finalLoc.FromInstanceStart || finalLoc.Map != dungeon.EntranceMap)
            route.push_back(finalLoc);

        if (route.empty())
            route.push_back(finalLoc);

        return route;
    }


    static std::vector<TeleportLocation> LoadInstanceWaypointRoute(DungeonTemplate const& dungeon)
    {
        std::vector<TeleportLocation> route;

        if (!InstanceWaypointTeleport)
            return route;

        QueryResult result = WorldDatabase.Query(
            "SELECT map_id, position_x, position_y, position_z, orientation "
            "FROM playerbot_dungeon_waypoint "
            "WHERE dungeon_template_id = {} AND enabled = 1 "
            "ORDER BY step_order ASC",
            dungeon.Id);

        if (!result)
            return route;

        do
        {
            Field* f = result->Fetch();
            route.push_back(MakeTeleportLocation(f[0].Get<uint32>(), f[1].Get<float>(), f[2].Get<float>(), f[3].Get<float>(), f[4].Get<float>(), true));
        } while (result->NextRow());

        return route;
    }

    static bool IsPlayerSafeToMove(Player* player);
    static bool IsBotCandidateSafeToMove(Player* player, BotCandidate const& bot);

    static uint32 CountOnlineMovableGroupMembers(std::vector<BotCandidate> const& members)
    {
        uint32 count = 0;
        for (BotCandidate const& b : members)
        {
            Player* player = FindOnlinePlayer(b.GuidLow);
            if (IsBotCandidateSafeToMove(player, b))
                ++count;
        }
        return count;
    }

    static uint32 CountOnlineRunMembers(uint64 runId, bool requireMovable)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT rm.guid, c.account FROM playerbot_dungeon_run_member rm "
            "JOIN characters c ON c.guid = rm.guid "
            "WHERE rm.run_id = {} AND rm.joined_as_real_player = 0",
            runId);

        if (!result)
            return 0;

        uint32 count = 0;
        do
        {
            Field* f = result->Fetch();
            uint32 guidLow = f[0].Get<uint32>();
            uint32 accountId = f[1].Get<uint32>();
            Player* player = FindOnlinePlayer(guidLow);
            if (!player || !AccountMatchesBotFilter(accountId))
                continue;

            if (requireMovable)
            {
                BotCandidate b;
                b.GuidLow = guidLow;
                b.AccountId = accountId;
                if (!IsBotCandidateSafeToMove(player, b))
                    continue;
            }

            ++count;
        } while (result->NextRow());

        return count;
    }

    static bool IsPlayerSafeToMove(Player* player)
    {
        if (!player)
            return false;

        if (!player->IsInWorld())
            return false;

        if (player->IsBeingTeleported())
            return false;

        if (!IsPlayerBotAccount(player))
            return false;

        if (player->GetSession() && player->GetSession()->GetSecurity() > SEC_PLAYER)
            return false;

        // Absolute safety: only configured bot accounts may be moved by this module.
        return true;
    }

    static bool IsBotCandidateSafeToMove(Player* player, BotCandidate const& bot)
    {
        if (!player)
            return false;

        if (!player->IsInWorld())
            return false;

        if (player->IsBeingTeleported())
            return false;

        // Candidate account id came from characters.account and already passed the configured bot prefix/range filter.
        // This keeps live bot movement safe even if the bot has no normal client session.
        if (!AccountMatchesBotFilter(bot.AccountId))
            return false;

        if (player->GetSession() && player->GetSession()->GetSecurity() > SEC_PLAYER)
            return false;

        return true;
    }

    static void ProcessProgressiveTeleportQueue()
    {
        if (_progressiveTravelQueue.empty())
            return;

        uint32 now = GameTime::GetGameTime().count();
        std::vector<ProgressiveTravelStep> keep;
        keep.reserve(_progressiveTravelQueue.size());

        for (ProgressiveTravelStep const& step : _progressiveTravelQueue)
        {
            if (step.ExecuteAt > now)
            {
                keep.push_back(step);
                continue;
            }

            BotCandidate bot;
            bot.GuidLow = step.GuidLow;
            bot.AccountId = step.AccountId;

            Player* player = FindOnlinePlayer(step.GuidLow);
            if (!IsBotCandidateSafeToMove(player, bot))
            {
                DebugLog("Journey step skipped for " + step.Name + ": bot no longer online/movable.");
                continue;
            }

            if (player->IsInCombat())
                player->CombatStop(true);

            player->TeleportTo(step.Map, step.X, step.Y, step.Z, step.O);
            StatusLog("[JourneySim] " + step.Name + " -> " + step.DungeonName + " step " + std::to_string(uint32(step.Step)) + "/" + std::to_string(uint32(step.Total)));
            if (AuditFileMovement)
                AuditLog("MOVE", "JourneySim " + step.Name + " -> " + step.DungeonName + " step=" + std::to_string(uint32(step.Step)) + "/" + std::to_string(uint32(step.Total)) +
                    " map=" + std::to_string(step.Map) + " x=" + std::to_string(step.X) + " y=" + std::to_string(step.Y) + " z=" + std::to_string(step.Z));
        }

        _progressiveTravelQueue.swap(keep);
    }

    static std::string NormalizeNativeStepType(std::string value)
    {
        value = LowerCopy(TrimCopy(value));
        if (value == "tp")
            return "teleport";
        if (value == "walk")
            return "move";
        if (value == "pause")
            return "wait";
        if (value == "say" || value == "note" || value == "checkpoint")
            return "log";
        if (value != "move" && value != "teleport" && value != "wait" && value != "log" && value != "pull" && value != "anchor")
            return "move";
        return value;
    }

    static void ProcessNativeEventQueue()
    {
        if (_nativeEventQueue.empty())
            return;

        uint32 now = GameTime::GetGameTime().count();
        std::vector<NativeEventQueueStep> keep;
        keep.reserve(_nativeEventQueue.size());

        for (NativeEventQueueStep const& step : _nativeEventQueue)
        {
            if (step.ExecuteAt > now)
            {
                keep.push_back(step);
                continue;
            }

            BotCandidate bot;
            bot.GuidLow = step.GuidLow;
            bot.AccountId = step.AccountId;

            Player* player = FindOnlinePlayer(step.GuidLow);
            if (!IsBotCandidateSafeToMove(player, bot))
            {
                NativeEventQueueStep retry = step;
                if (retry.RetryCount < 6)
                {
                    ++retry.RetryCount;
                    retry.ExecuteAt = now + 2;
                    keep.push_back(retry);
                    DebugLog("Native step delayed for " + step.Name + ": bot not ready/teleporting yet, retry " + std::to_string(uint32(retry.RetryCount)) + ".");
                }
                else
                    DebugLog("Native step skipped for " + step.Name + ": bot no longer online/movable after retries.");
                continue;
            }

            if (player->IsInCombat())
                player->CombatStop(true);

            std::string type = NormalizeNativeStepType(step.StepType);

            if (type == "wait" || type == "log")
            {
                StatusLog("[NativeClear] run #" + std::to_string(step.RunId) + " " + step.Name + " " + step.DungeonName +
                    " step " + std::to_string(step.StepOrder) + " " + type + " '" + step.Label + "'");
                continue;
            }

            // anchor/pull are movement anchors. For now they route the bot to the recorded spot and let Playerbots engage nearby mobs.
            bool useTeleport = (type == "teleport") || !NativeEventMoveInsteadTeleport;
            if (useTeleport)
            {
                player->TeleportTo(step.Map, step.X, step.Y, step.Z, step.O);
                StatusLog("[NativeClear] run #" + std::to_string(step.RunId) + " TP " + step.Name + " -> " + step.DungeonName +
                    " step " + std::to_string(step.StepOrder) + " '" + step.Label + "'");
                if (AuditFileMovement)
                    AuditLog("MOVE", "run #" + std::to_string(step.RunId) + " TP " + step.Name + " -> " + step.DungeonName +
                        " step=" + std::to_string(step.StepOrder) + " label='" + step.Label + "' map=" + std::to_string(step.Map) +
                        " x=" + std::to_string(step.X) + " y=" + std::to_string(step.Y) + " z=" + std::to_string(step.Z));
            }
            else
            {
                player->GetMotionMaster()->MovePoint(step.StepOrder, step.X, step.Y, step.Z);
                StatusLog("[NativeClear] run #" + std::to_string(step.RunId) + " MOVE " + step.Name + " -> " + step.DungeonName +
                    " step " + std::to_string(step.StepOrder) + " '" + step.Label + "'");
                if (AuditFileMovement)
                    AuditLog("MOVE", "run #" + std::to_string(step.RunId) + " MOVE " + step.Name + " -> " + step.DungeonName +
                        " step=" + std::to_string(step.StepOrder) + " label='" + step.Label + "' map=" + std::to_string(step.Map) +
                        " x=" + std::to_string(step.X) + " y=" + std::to_string(step.Y) + " z=" + std::to_string(step.Z));
            }
        }

        _nativeEventQueue.swap(keep);
    }

    static float DistanceSq3d(Player const* a, Player const* b)
    {
        if (!a || !b)
            return 999999999.0f;

        float dx = a->GetPositionX() - b->GetPositionX();
        float dy = a->GetPositionY() - b->GetPositionY();
        float dz = a->GetPositionZ() - b->GetPositionZ();
        return dx * dx + dy * dy + dz * dz;
    }

    static BotCandidate SelectNativeLeader(std::vector<BotCandidate> const& group)
    {
        for (BotCandidate const& b : group)
        {
            if (b.PreferredRole != ROLE_TANK)
                continue;

            Player* player = FindOnlinePlayer(b.GuidLow);
            if (IsBotCandidateSafeToMove(player, b))
                return b;
        }

        for (BotCandidate const& b : group)
        {
            Player* player = FindOnlinePlayer(b.GuidLow);
            if (IsBotCandidateSafeToMove(player, b))
                return b;
        }

        return group.empty() ? BotCandidate() : group.front();
    }

    static void ScheduleNativeFollowerLeash(uint64 runId, std::vector<BotCandidate> const& group, BotCandidate const& leader, uint32 totalSeconds)
    {
        if (!NativeFollowerLeash || group.size() < 2 || !leader.GuidLow)
            return;

        uint32 now = GameTime::GetGameTime().count();
        uint32 interval = std::max<uint32>(1, NativeFollowerLeashSeconds);
        uint32 total = std::max<uint32>(interval, totalSeconds + interval);
        uint32 queued = 0;

        for (uint32 t = interval; t <= total; t += interval)
        {
            NativeFollowerLeashStep step;
            step.RunId = runId;
            step.LeaderGuidLow = leader.GuidLow;
            step.LeaderAccountId = leader.AccountId;
            step.LeaderName = leader.Name;
            step.Members = group;
            step.ExecuteAt = now + t;
            step.Sequence = ++queued;
            _nativeFollowerLeashQueue.push_back(step);
        }

        if (queued)
            DebugLog("[NativeClear] queued " + std::to_string(queued) + " follower leash checks for run #" + std::to_string(runId) + ".");
    }

    static void ProcessNativeFollowerLeashQueue()
    {
        if (_nativeFollowerLeashQueue.empty())
            return;

        uint32 now = GameTime::GetGameTime().count();
        std::vector<NativeFollowerLeashStep> keep;
        keep.reserve(_nativeFollowerLeashQueue.size());

        for (NativeFollowerLeashStep const& step : _nativeFollowerLeashQueue)
        {
            if (step.ExecuteAt > now)
            {
                keep.push_back(step);
                continue;
            }

            BotCandidate leaderBot;
            leaderBot.GuidLow = step.LeaderGuidLow;
            leaderBot.AccountId = step.LeaderAccountId;

            Player* leader = FindOnlinePlayer(step.LeaderGuidLow);
            if (!IsBotCandidateSafeToMove(leader, leaderBot))
            {
                NativeFollowerLeashStep retry = step;
                if (retry.RetryCount < 6)
                {
                    ++retry.RetryCount;
                    retry.ExecuteAt = now + 2;
                    keep.push_back(retry);
                    DebugLog("[NativeClear] follower leash delayed for run #" + std::to_string(step.RunId) + ": leader not ready/teleporting yet, retry " + std::to_string(uint32(retry.RetryCount)) + ".");
                }
                else
                    DebugLog("[NativeClear] follower leash skipped for run #" + std::to_string(step.RunId) + ": leader not online/movable after retries.");
                continue;
            }

            float softSq = NativeFollowerLeashDistance * NativeFollowerLeashDistance;
            float hardSq = NativeFollowerHardTeleportDistance * NativeFollowerHardTeleportDistance;

            for (BotCandidate const& b : step.Members)
            {
                if (b.GuidLow == step.LeaderGuidLow)
                    continue;

                Player* follower = FindOnlinePlayer(b.GuidLow);
                if (!IsBotCandidateSafeToMove(follower, b))
                    continue;

                if (follower->IsInCombat())
                    continue;

                if (follower->GetMapId() != leader->GetMapId())
                {
                    follower->TeleportTo(leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), leader->GetOrientation());
                    DebugLog("[NativeClear] follower leash TP " + b.Name + " to leader " + step.LeaderName + " for run #" + std::to_string(step.RunId) + ".");
                    if (AuditFileMovement)
                        AuditLog("LEASH", "run #" + std::to_string(step.RunId) + " TP " + b.Name + " to leader " + step.LeaderName);
                    continue;
                }

                float distSq = DistanceSq3d(follower, leader);
                if (NativeFollowerHardTeleportDistance > 0.0f && distSq > hardSq)
                {
                    follower->TeleportTo(leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), leader->GetOrientation());
                    DebugLog("[NativeClear] follower hard-leash TP " + b.Name + " to leader " + step.LeaderName + " for run #" + std::to_string(step.RunId) + ".");
                    if (AuditFileMovement)
                        AuditLog("LEASH", "run #" + std::to_string(step.RunId) + " HARD_TP " + b.Name + " to leader " + step.LeaderName);
                    continue;
                }

                if (distSq > softSq)
                {
                    follower->GetMotionMaster()->MovePoint(uint32(700000 + (step.Sequence % 10000)), leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ());
                    DebugLog("[NativeClear] follower move-leash " + b.Name + " toward leader " + step.LeaderName + " for run #" + std::to_string(step.RunId) + ".");
                    if (AuditFileMovement)
                        AuditLog("LEASH", "run #" + std::to_string(step.RunId) + " MOVE " + b.Name + " toward leader " + step.LeaderName);
                }
            }
        }

        _nativeFollowerLeashQueue.swap(keep);
    }

    static void ScheduleNativeEventSteps(uint64 runId, DungeonTemplate const& dungeon, std::vector<BotCandidate> const& group)
    {
        if (!NativeEventSteps || group.empty())
            return;

        QueryResult result = WorldDatabase.Query(
            "SELECT step_order, step_type, map_id, position_x, position_y, position_z, orientation, wait_seconds, label "
            "FROM playerbot_dungeon_event_step "
            "WHERE dungeon_template_id = {} AND enabled = 1 "
            "ORDER BY step_order ASC",
            dungeon.Id);

        if (!result)
            return;

        uint32 now = GameTime::GetGameTime().count();
        uint32 cumulativeDelay = std::max<uint32>(1, NativeEventStepSeconds);
        uint32 scheduled = 0;
        BotCandidate leader = SelectNativeLeader(group);
        if (NativeEventLeaderOnly && leader.GuidLow)
            StatusLog("[NativeClear] run #" + std::to_string(runId) + " leader/tank driver is " + leader.Name + " for " + dungeon.Name + ".");

        do
        {
            Field* f = result->Fetch();
            uint32 order = f[0].Get<uint32>();
            std::string type = NormalizeNativeStepType(f[1].Get<std::string>());
            uint32 map = f[2].Get<uint32>();
            float x = f[3].Get<float>();
            float y = f[4].Get<float>();
            float z = f[5].Get<float>();
            float o = f[6].Get<float>();
            uint32 wait = f[7].Get<uint32>();
            std::string label = f[8].Get<std::string>();

            uint32 delay = wait ? wait : cumulativeDelay;
            cumulativeDelay += std::max<uint32>(1, wait ? wait : NativeEventStepSeconds);

            for (BotCandidate const& b : group)
            {
                if (NativeEventLeaderOnly && type != "teleport" && leader.GuidLow && b.GuidLow != leader.GuidLow)
                    continue;

                Player* player = FindOnlinePlayer(b.GuidLow);
                if (!IsBotCandidateSafeToMove(player, b))
                    continue;

                NativeEventQueueStep step;
                step.RunId = runId;
                step.DungeonId = dungeon.Id;
                step.GuidLow = b.GuidLow;
                step.AccountId = b.AccountId;
                step.Name = b.Name;
                step.DungeonName = dungeon.Name;
                step.StepOrder = order;
                step.StepType = type;
                step.Map = map;
                step.X = x;
                step.Y = y;
                step.Z = z;
                step.O = o;
                step.ExecuteAt = now + delay;
                step.Label = label;
                _nativeEventQueue.push_back(step);
                ++scheduled;
            }
        } while (result->NextRow());

        if (scheduled)
            StatusLog("[NativeClear] queued " + std::to_string(scheduled) + " native route actions for run #" + std::to_string(runId) + " " + dungeon.Name + ".");

        ScheduleNativeFollowerLeash(runId, group, leader, cumulativeDelay);
    }

    static uint32 TeleportOnlineGroupMembers(std::vector<BotCandidate> const& members, DungeonTemplate const& dungeon, TeleportLocation const& loc)
    {
        if (!AllowOnlineCharacterTouch || !TeleportOnlineMembers)
            return 0;

        uint32 moved = 0;
        uint32 now = GameTime::GetGameTime().count();

        for (BotCandidate const& b : members)
        {
            Player* player = FindOnlinePlayer(b.GuidLow);
            if (!IsBotCandidateSafeToMove(player, b))
                continue;

            if (player->IsInCombat())
                player->CombatStop(true);

            if (ProgressiveTeleport || InstanceWaypointTeleport)
            {
                std::vector<TeleportLocation> route;
                if (ProgressiveTeleport)
                    route = BuildProgressiveRoute(b.Team, dungeon, loc);
                else
                    route.push_back(loc);

                std::vector<TeleportLocation> instanceWaypoints = LoadInstanceWaypointRoute(dungeon);
                route.insert(route.end(), instanceWaypoints.begin(), instanceWaypoints.end());

                if (route.empty())
                    route.push_back(loc);

                uint8 total = uint8(std::min<size_t>(route.size(), 255));
                uint32 stepSeconds = InstanceWaypointTeleport ? InstanceWaypointStepSeconds : ProgressiveTeleportStepSeconds;
                stepSeconds = std::max<uint32>(1, stepSeconds);

                for (uint8 i = 0; i < total; ++i)
                {
                    TeleportLocation const& stepLoc = route[i];
                    if (i == 0)
                    {
                        player->TeleportTo(stepLoc.Map, stepLoc.X, stepLoc.Y, stepLoc.Z, stepLoc.O);
                        StatusLog("[JourneySim] " + b.Name + " -> " + dungeon.Name + " step 1/" + std::to_string(uint32(total)));
                    }
                    else
                    {
                        ProgressiveTravelStep step;
                        step.GuidLow = b.GuidLow;
                        step.AccountId = b.AccountId;
                        step.Name = b.Name;
                        step.DungeonName = dungeon.Name;
                        step.Map = stepLoc.Map;
                        step.X = stepLoc.X;
                        step.Y = stepLoc.Y;
                        step.Z = stepLoc.Z;
                        step.O = stepLoc.O;
                        step.ExecuteAt = now + (uint32(i) * stepSeconds);
                        step.Step = i + 1;
                        step.Total = total;
                        _progressiveTravelQueue.push_back(step);
                    }
                }
            }
            else
            {
                player->TeleportTo(loc.Map, loc.X, loc.Y, loc.Z, loc.O);
            }

            ++moved;
        }

        if (moved > 0)
        {
            if (ProgressiveTeleport || InstanceWaypointTeleport)
                StatusLog("LIVE journey queued " + std::to_string(moved) + "/" + std::to_string(members.size()) + " online bot(s) for " + dungeon.Name + ".");
            else
                StatusLog("LIVE teleport " + std::to_string(moved) + "/" + std::to_string(members.size()) + " online bot(s) for " + dungeon.Name +
                    (loc.FromInstanceStart ? " to instance start." : " to outdoor entrance."));
        }

        return moved;
    }

    static bool CreateLiveGroupIfPossible(std::vector<BotCandidate> const& members)
    {
        if (!AllowOnlineCharacterTouch || !CreateRealGroups || members.empty())
            return false;

        std::vector<Player*> online;
        online.reserve(members.size());

        for (BotCandidate const& b : members)
        {
            Player* player = FindOnlinePlayer(b.GuidLow);
            if (IsBotCandidateSafeToMove(player, b) && !player->GetGroup())
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


    static Role ParseRequestedRole(std::string const& lower)
    {
        if (lower.find("tank") != std::string::npos || lower.find("prot") != std::string::npos)
            return ROLE_TANK;
        if (lower.find("heal") != std::string::npos || lower.find("heals") != std::string::npos || lower.find("healer") != std::string::npos || lower.find("holy") != std::string::npos || lower.find("disc") != std::string::npos || lower.find("resto") != std::string::npos)
            return ROLE_HEALER;
        if (lower.find("dps") != std::string::npos || lower.find("damage") != std::string::npos || lower.find("frost") != std::string::npos || lower.find("fire") != std::string::npos || lower.find("shadow") != std::string::npos || lower.find("arms") != std::string::npos || lower.find("combat") != std::string::npos)
            return ROLE_DPS;
        return ROLE_UNKNOWN;
    }

    static bool LooksLikeLfgWhisper(std::string const& lower)
    {
        return lower.find("lfg") != std::string::npos || lower.find("inv") != std::string::npos || lower.find("invite") != std::string::npos || lower.find("dps") != std::string::npos || lower.find("tank") != std::string::npos || lower.find("heal") != std::string::npos;
    }

    static bool HandleBotWhisperJoin(Player* player, Player* receiver, std::string const& msg)
    {
        if (!PlayerWhisperJoin || !InviteRealPlayers || !player || !receiver)
            return false;

        if (!IsPlayerBotAccount(receiver) || IsPlayerBotAccount(player))
            return false;

        if (!PlayerWhisperJoinAllowGm && player->GetSession() && player->GetSession()->GetSecurity() > SEC_PLAYER)
            return false;

        std::string lower = LowerCopy(msg);
        if (!LooksLikeLfgWhisper(lower))
            return false;

        Role requestedRole = ParseRequestedRole(lower);
        if (PlayerWhisperJoinRequiresRole && requestedRole == ROLE_UNKNOWN)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("DungeonSim: whisper a bot with a role, for example: `lfg dps`, `inv tank`, or `heals`.");
            return true;
        }
        if (requestedRole == ROLE_UNKNOWN)
            requestedRole = ROLE_DPS;

        uint32 botGuid = receiver->GetGUID().GetCounter();
        QueryResult runResult = CharacterDatabase.Query(
            "SELECT r.id, r.dungeon_template_id, r.team "
            "FROM playerbot_dungeon_run_member rm "
            "JOIN playerbot_dungeon_run r ON r.id = rm.run_id "
            "WHERE rm.guid = {} AND r.state IN ({},{}) "
            "ORDER BY r.started_at DESC LIMIT 1",
            botGuid, uint8(RUN_ACTIVE), uint8(RUN_REAL_ATTEMPT));

        if (!runResult)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("DungeonSim: {} is not currently leading or joining an open dungeon group.", receiver->GetName());
            return true;
        }

        Field* rf = runResult->Fetch();
        uint64 runId = rf[0].Get<uint64>();
        uint32 dungeonTemplateId = rf[1].Get<uint32>();
        uint8 team = rf[2].Get<uint8>();

        QueryResult dungeonResult = WorldDatabase.Query("SELECT name, min_level, max_level, group_size, entrance_map, entrance_x, entrance_y, entrance_z, entrance_o FROM playerbot_dungeon_template WHERE id = {} LIMIT 1", dungeonTemplateId);
        if (!dungeonResult)
            return true;

        Field* df = dungeonResult->Fetch();
        DungeonTemplate dungeon;
        dungeon.Id = dungeonTemplateId;
        dungeon.Name = df[0].Get<std::string>();
        dungeon.MinLevel = df[1].Get<uint8>();
        dungeon.MaxLevel = df[2].Get<uint8>();
        dungeon.GroupSize = df[3].Get<uint8>();
        dungeon.EntranceMap = df[4].Get<uint32>();
        dungeon.X = df[5].Get<float>();
        dungeon.Y = df[6].Get<float>();
        dungeon.Z = df[7].Get<float>();
        dungeon.O = df[8].Get<float>();

        if (!IsRealPlayerEligibleForInvite(player, dungeon, team))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("DungeonSim: you are not eligible for {} right now.", dungeon.Name);
            return true;
        }

        QueryResult memberCountResult = CharacterDatabase.Query("SELECT COUNT(DISTINCT guid) FROM playerbot_dungeon_run_member WHERE run_id = {}", runId);
        uint32 memberCount = memberCountResult ? memberCountResult->Fetch()[0].Get<uint32>() : dungeon.GroupSize;
        if (memberCount >= dungeon.GroupSize)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("DungeonSim: {}'s {} group is already full.", receiver->GetName(), dungeon.Name);
            return true;
        }

        uint32 playerGuid = player->GetGUID().GetCounter();
        CharacterDatabase.Execute(
            "INSERT IGNORE INTO playerbot_dungeon_run_member "
            "(run_id, guid, role, guild_id, level_at_start, joined_as_real_player) VALUES ({},{},{},0,{},1)",
            runId, playerGuid, uint8(requestedRole), player->GetLevel());

        if (!player->GetGroup() && receiver->GetGroup())
        {
            Group* group = receiver->GetGroup();
            group->AddMember(player);
        }

        ChatHandler(player->GetSession()).PSendSysMessage("DungeonSim: you joined {}'s {} group as {}. No teleport was used; travel/summon normally.", receiver->GetName(), dungeon.Name, RoleText(requestedRole));
        StatusLog("[LFGSim] " + player->GetName() + " whispered " + receiver->GetName() + " and joined " + dungeon.Name + " as " + RoleText(requestedRole) + ".");
        return true;
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

    static uint64 AllocateRunId()
    {
        // Do not call SELECT MAX(id)+1 for every run. CharacterDatabase.Execute() is async on many AC branches,
        // so two starts in the same tick can otherwise allocate the same id before the first insert is visible.
        static uint64 nextRunId = 0;

        if (!nextRunId)
        {
            QueryResult result = CharacterDatabase.Query("SELECT COALESCE(MAX(id), 0) + 1 FROM playerbot_dungeon_run");
            nextRunId = result ? result->Fetch()[0].Get<uint64>() : 1;
            if (!nextRunId)
                nextRunId = 1;
        }

        for (uint8 attempt = 0; attempt < 16; ++attempt)
        {
            uint64 candidate = nextRunId++;
            QueryResult exists = CharacterDatabase.Query("SELECT 1 FROM playerbot_dungeon_run WHERE id = {} LIMIT 1", candidate);
            if (!exists)
                return candidate;
        }

        return 0;
    }

    static void CleanupEmptyActiveRuns()
    {
        // Safety cleanup for older builds or failed inserts: an active run with no member rows is invalid.
        CharacterDatabase.Execute(
            "UPDATE playerbot_dungeon_run r "
            "LEFT JOIN playerbot_dungeon_run_member rm ON rm.run_id = r.id "
            "SET r.state = {}, r.result = {} "
            "WHERE r.state IN (0,1,4) AND rm.run_id IS NULL",
            uint8(RUN_FAILED), uint8(RESULT_ABANDONED));

        // Live-only test mode should not keep old fake/offline active rows around.
        // This makes console status match what you are trying to test: actual online bots moved into dungeons.
        if (RequireOnlineBotsForRun && CleanupOfflineRunsWhenLiveOnly)
        {
            CharacterDatabase.Execute(
                "UPDATE playerbot_dungeon_run SET state = {}, result = {} "
                "WHERE state IN (0,1,4) AND online_members_moved < {}",
                uint8(RUN_FAILED), uint8(RESULT_ABANDONED), MinOnlineMembersForLiveRun);
        }
    }

    static void LogLiveRosterDiagnostics(DungeonTemplate const& dungeon, std::vector<BotCandidate> const& group, uint32 onlineReady)
    {
        if (!Debug || !LiveDebugRoster)
            return;

        uint32 dbOnline = 0;
        uint32 objectFound = 0;
        uint32 movable = 0;
        for (BotCandidate const& b : group)
        {
            if (b.DbOnline)
                ++dbOnline;
            Player* player = FindOnlinePlayer(b.GuidLow);
            if (player)
                ++objectFound;
            if (IsBotCandidateSafeToMove(player, b))
                ++movable;
        }

        uint32 tanks = 0;
        uint32 healers = 0;
        uint32 dps = 0;
        for (BotCandidate const& b : group)
        {
            if (b.PreferredRole == ROLE_TANK)
                ++tanks;
            else if (b.PreferredRole == ROLE_HEALER)
                ++healers;
            else
                ++dps;
        }

        DebugLog("Live roster check for " + dungeon.Name + ": group=" + std::to_string(group.size()) +
            " roles=" + std::to_string(tanks) + "T/" + std::to_string(healers) + "H/" + std::to_string(dps) + "D" +
            " dbOnline=" + std::to_string(dbOnline) + " objectFound=" + std::to_string(objectFound) +
            " movable=" + std::to_string(movable) + " required=" + std::to_string(MinOnlineMembersForLiveRun) + ".");

        uint32 shown = 0;
        for (BotCandidate const& b : group)
        {
            if (shown >= LiveDebugMaxRosterLines)
                break;
            Player* player = FindOnlinePlayer(b.GuidLow);
            DebugLog("  member " + b.Name + " guid=" + std::to_string(b.GuidLow) +
                " lvl=" + std::to_string(uint32(b.Level)) +
                " class=" + ClassNameText(b.Class) +
                " role=" + RoleText(b.PreferredRole) +
                " spec='" + ClassSpecText(b.Class, b.PreferredRole) + "'" +
                " dbOnline=" + std::to_string(b.DbOnline ? 1 : 0) +
                " object=" + std::to_string(player ? 1 : 0) +
                " movable=" + std::to_string(IsBotCandidateSafeToMove(player, b) ? 1 : 0));
            ++shown;
        }
    }

    static void AuditRunMembers(uint64 runId, DungeonTemplate const& dungeon, std::vector<BotCandidate> const& group, std::string const& phase)
    {
        if (!AuditFileLog || !AuditFileMembers)
            return;

        uint32 tanks = 0;
        uint32 healers = 0;
        uint32 dps = 0;
        uint32 online = 0;
        uint32 movable = 0;
        std::unordered_set<uint32> seen;
        uint32 duplicates = 0;

        for (BotCandidate const& b : group)
        {
            if (!seen.insert(b.GuidLow).second)
                ++duplicates;

            if (b.PreferredRole == ROLE_TANK)
                ++tanks;
            else if (b.PreferredRole == ROLE_HEALER)
                ++healers;
            else
                ++dps;

            Player* player = FindOnlinePlayer(b.GuidLow);
            if (player)
                ++online;
            if (IsBotCandidateSafeToMove(player, b))
                ++movable;
        }

        AuditLog("RUN", phase + " run #" + std::to_string(runId) + " " + TeamName(group.empty() ? TEAM_ALLIANCE : group.front().Team) + " " + dungeon.Name +
            " members=" + std::to_string(group.size()) + " distinct=" + std::to_string(seen.size()) +
            " duplicates=" + std::to_string(duplicates) + " roles=" + std::to_string(tanks) + "T/" +
            std::to_string(healers) + "H/" + std::to_string(dps) + "D online=" + std::to_string(online) +
            " movable=" + std::to_string(movable));

        for (BotCandidate const& b : group)
        {
            Player* player = FindOnlinePlayer(b.GuidLow);
            AuditLog("MEMBER", "run #" + std::to_string(runId) + " " + b.Name +
                " guid=" + std::to_string(b.GuidLow) +
                " account=" + std::to_string(b.AccountId) +
                " level=" + std::to_string(uint32(b.Level)) +
                " class=" + ClassNameText(b.Class) +
                " role=" + RoleText(b.PreferredRole) +
                " team=" + TeamName(b.Team) +
                " guild=" + std::to_string(b.GuildId) +
                " dbOnline=" + std::to_string(b.DbOnline ? 1 : 0) +
                " object=" + std::to_string(player ? 1 : 0) +
                " movable=" + std::to_string(IsBotCandidateSafeToMove(player, b) ? 1 : 0));
        }
    }

    static void CreateRun(DungeonTemplate const& dungeon, std::vector<BotCandidate> const& group, RunExecutionMode mode)
    {
        if (group.empty())
        {
            DebugLog("Refused to create " + dungeon.Name + " run because group has zero bot members.");
            return;
        }

        uint32 now = GameTime::GetGameTime().count();
        uint32 duration = urand(MinDurationMinutes * MINUTE, MaxDurationMinutes * MINUTE);
        int32 quality = CalculateQuality(group, dungeon);
        uint32 guildId = GetRunGuildId(group);

        if (IsRunLocked(dungeon, group.front().Team, guildId))
        {
            DebugLog("Skipped " + dungeon.Name + " because this raid team is locked until reset.");
            return;
        }

        bool realMode = mode == RUN_MODE_REAL;
        TeleportLocation runLoc = ResolveTeleportLocation(dungeon);
        uint32 onlineReady = AllowOnlineCharacterTouch ? CountOnlineMovableGroupMembers(group) : 0;
        if (realMode && MinOnlineMembersForLiveRun > 0)
            LogLiveRosterDiagnostics(dungeon, group, onlineReady);

        if (realMode && MinOnlineMembersForLiveRun > 0 && onlineReady < MinOnlineMembersForLiveRun)
        {
            DebugLog("Skipped live attempt for " + dungeon.Name + ": only " + std::to_string(onlineReady) +
                " online/movable bot(s), need " + std::to_string(MinOnlineMembersForLiveRun) + ".");

            // Test/live mode guard: when enabled, do not create an offline/fake run at all.
            // This is for the phase where we want to see actual online Playerbot characters grouped/moved/looted.
            if (RequireOnlineBotsForRun)
            {
                DebugLog("Skipped " + dungeon.Name + " completely because RequireOnlineBotsForRun is enabled.");
                return;
            }

            if (TeleportOnlyIfOnline)
                return;
        }

        // Organic auto-runs should look like groups formed through chat first, then entered the dungeon.
        // GM forced runs use StartGmRun directly and remain immediate test tools.
        MaybeSendGuildLfm(group, dungeon);
        MaybeSendGeneralLfg(group, dungeon);
        SendStructuredLfgSimulation(group, dungeon);
        if (!StructuredLfgSimulation)
            SendOrganicLfgActing(group, dungeon);

        bool madeLiveGroup = realMode && CreateLiveGroupIfPossible(group);

        // Bot-only runs may auto-teleport. Runs that may invite a real player should not yank anyone,
        // because those are meant to feel like a real party that travels/summons together.
        bool mayInviteRealPlayer = realMode && InviteRealPlayers && MaxRealPlayerInvitesPerRun > 0 && (!InviteOnlyIfGroupShort || group.size() < dungeon.GroupSize);
        bool allowAutoTeleport = TeleportOnlineMembers && !(ManualTravelForPlayerJoinedRuns && mayInviteRealPlayer);
        uint32 movedOnline = (AllowOnlineCharacterTouch && allowAutoTeleport) ? TeleportOnlineGroupMembers(group, dungeon, runLoc) : 0;
        uint8 initialState = realMode ? RUN_REAL_ATTEMPT : RUN_ACTIVE;

        if (ManualTravelForPlayerJoinedRuns && mayInviteRealPlayer)
            DebugLog("Manual travel mode for " + dungeon.Name + ": real-player invite possible, no auto-teleport used.");

        if (TeleportOnlyIfOnline && realMode && allowAutoTeleport && movedOnline == 0)
        {
            DebugLog("Skipped run for " + dungeon.Name + " because no online members could be teleported.");
            return;
        }

        uint64 runId = AllocateRunId();
        if (!runId)
        {
            DebugLog("Failed to allocate run id for " + dungeon.Name + ".");
            return;
        }

        CharacterDatabase.Execute(
            "INSERT INTO playerbot_dungeon_run "
            "(id, dungeon_template_id, team, guild_id, state, started_at, ends_at, quality_score, planned_duration_sec, "
            "real_group_created, online_members_moved, entrance_map, entrance_x, entrance_y, entrance_z, entrance_o) "
            "VALUES ({},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{})",
            runId, dungeon.Id, group.front().Team, guildId, initialState, now, now + duration, quality, duration,
            madeLiveGroup ? 1 : 0, movedOnline, runLoc.Map, runLoc.X, runLoc.Y, runLoc.Z, runLoc.O);

        AuditRunMembers(runId, dungeon, group, "START " + RunModeText(mode));

        SaveRunLockout(dungeon, group.front().Team, guildId);

        if (realMode)
            CreateRealPlayerInvites(runId, dungeon, group);

        for (BotCandidate const& b : group)
        {
            CharacterDatabase.Execute(
                "INSERT IGNORE INTO playerbot_dungeon_run_member "
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

        StatusLog("START " + RunModeText(mode) + " run #" + std::to_string(runId) + " " + TeamName(group.front().Team) + " " + dungeon.Name +
            " members=" + std::to_string(group.size()) + " guild=" + std::to_string(guildId) +
            " quality=" + std::to_string(quality) + " duration=" + std::to_string(duration / MINUTE) + "m" +
            " liveGroup=" + std::to_string(madeLiveGroup ? 1 : 0) + " moved=" + std::to_string(movedOnline) +
            " dest=" + (runLoc.FromInstanceStart ? "instance" : "entrance"));

        if (realMode)
            ScheduleNativeEventSteps(runId, dungeon, group);

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
            "SELECT c.guid, c.name, c.level, c.race, c.class, COALESCE(g.guildid,0), rm.role, c.account, c.online "
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
            b.AccountId = f[7].Get<uint32>();
            b.DbOnline = f[8].Get<uint8>() != 0;
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
                if (AllowOnlineCharacterTouch && GiveLootToOnlinePlayers)
                {
                    Player* player = FindOnlinePlayer(winner->GuidLow);
                    if (player)
                        storedOnline = TryStoreOnlineSmart(player, picked.ItemEntry, EquipUsableLootOnline, equipped, upgradeScore, equipSlot);
                }

                RecordLootAward(runId, winner->GuidLow, picked.ItemEntry, bossName, storedOnline, equipped, upgradeScore, equipSlot);
                std::string itemName = proto ? proto->Name1 : std::string("item#") + std::to_string(picked.ItemEntry);
                DebugLog("Awarded item " + std::to_string(picked.ItemEntry) + " from " + bossName + " to " + winner->Name +
                    (storedOnline ? " delivered" : " pending") + (equipped ? " and equipped." : "."));
                if (AuditFileLoot)
                    AuditLog("LOOT", "run #" + std::to_string(runId) + " boss='" + bossName + "' winner=" + winner->Name +
                        " guid=" + std::to_string(winner->GuidLow) + " item=" + std::to_string(picked.ItemEntry) +
                        " itemName='" + itemName + "' delivery=" + (storedOnline ? "online" : "pending") +
                        " equipped=" + std::to_string(equipped ? 1 : 0) + " upgradeScore=" + std::to_string(upgradeScore) +
                        " equipSlot=" + std::to_string(uint32(equipSlot)));
            }
        } while (killedBosses->NextRow());
    }


    static uint32 GetEffectiveXpLevelCap()
    {
        if (XpLevelCap > 0)
            return std::min<uint32>(XpLevelCap, 80);

        uint32 cap = GetEffectiveServerLevelCap();
        if (cap == 0)
            cap = 80;
        return std::min<uint32>(cap, 80);
    }

    static uint32 GetXpForLevel(uint32 level)
    {
        level = std::max<uint32>(1, std::min<uint32>(level, 79));

        static bool checkedColumns = false;
        static std::string levelColumn = "lvl";
        static std::string xpColumn = "xp_for_next_level";

        if (!checkedColumns)
        {
            checkedColumns = true;

            QueryResult levelCol = WorldDatabase.Query(
                "SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
                "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_xp_for_level' "
                "AND COLUMN_NAME IN ('lvl','Level') ORDER BY FIELD(COLUMN_NAME,'lvl','Level') LIMIT 1");
            if (levelCol)
                levelColumn = levelCol->Fetch()[0].Get<std::string>();

            QueryResult xpCol = WorldDatabase.Query(
                "SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
                "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_xp_for_level' "
                "AND COLUMN_NAME IN ('xp_for_next_level','Experience') ORDER BY FIELD(COLUMN_NAME,'xp_for_next_level','Experience') LIMIT 1");
            if (xpCol)
                xpColumn = xpCol->Fetch()[0].Get<std::string>();
        }

        QueryResult result = WorldDatabase.Query(
            "SELECT `{}` FROM player_xp_for_level WHERE `{}` = {} LIMIT 1",
            xpColumn, levelColumn, level);

        if (result)
        {
            uint32 xp = result->Fetch()[0].Get<uint32>();
            if (xp > 0)
                return xp;
        }

        // Fallback only if the core table is missing/unexpected. Close enough for SIM math.
        return std::max<uint32>(400, level * 1000);
    }

    static uint32 CalculateDungeonXpPercent(uint8 killedBosses, uint8 totalBosses)
    {
        if (XpPercentOfLevel == 0 || killedBosses == 0)
            return 0;

        uint32 percent = std::min<uint32>(XpPercentOfLevel, 100);
        if (XpScaleByBossKills && totalBosses > 0)
        {
            percent = uint32(std::ceil((double(percent) * double(killedBosses)) / double(totalBosses)));
            if (killedBosses > 0)
                percent = std::max<uint32>(std::min<uint32>(XpMinimumPercentOnAnyKill, XpPercentOfLevel), percent);
        }

        return std::min<uint32>(percent, 100);
    }

    static bool GiveXpOnline(Player* player, uint32 xpAmount)
    {
        if (!player || xpAmount == 0)
            return false;

        if (!IsPlayerBotAccount(player))
            return false;

        if (player->GetLevel() >= GetEffectiveXpLevelCap())
            return false;

        player->GiveXP(xpAmount, nullptr);
        return true;
    }

    static void RecordXpAward(uint64 runId, BotCandidate const& member, uint32 xpAmount, uint32 percent, uint8 killedBosses, uint8 totalBosses, bool delivered, std::string note)
    {
        uint8 deliveryState = delivered ? 2 : (note.find("failed") != std::string::npos ? 1 : 0);
        CharacterDatabase.EscapeString(note);
        CharacterDatabase.Execute(
            "INSERT INTO playerbot_dungeon_xp_award "
            "(run_id, guid, level_at_award, xp_amount, percent_of_level, killed_bosses, total_bosses, delivered, delivered_at, awarded_at, note) "
            "VALUES ({},{},{},{},{},{},{},{},{},{},'{}')",
            runId, member.GuidLow, uint32(member.Level), xpAmount, percent, uint32(killedBosses), uint32(totalBosses),
            deliveryState, delivered ? GameTime::GetGameTime().count() : 0, GameTime::GetGameTime().count(), note);
    }

    static void AwardXpForRun(uint64 runId, uint32 dungeonTemplateId, uint8 killedBosses, uint8 totalBosses)
    {
        (void)dungeonTemplateId;

        if (!AwardXp || killedBosses == 0)
            return;

        uint32 percent = CalculateDungeonXpPercent(killedBosses, totalBosses);
        if (percent == 0)
            return;

        uint32 cap = GetEffectiveXpLevelCap();
        std::vector<BotCandidate> members = LoadRunMembers(runId);
        if (members.empty())
            return;

        for (BotCandidate const& member : members)
        {
            if (!AccountMatchesBotFilter(member.AccountId))
                continue;

            if (member.Level >= cap)
            {
                if (AuditFileLoot)
                    AuditLog("XP", "run #" + std::to_string(runId) + " skipped " + member.Name +
                        " guid=" + std::to_string(member.GuidLow) + " level=" + std::to_string(uint32(member.Level)) +
                        " cap=" + std::to_string(cap));
                continue;
            }

            uint32 xpForLevel = GetXpForLevel(member.Level);
            uint32 xpAmount = std::max<uint32>(1, (xpForLevel * percent) / 100);
            bool delivered = false;
            std::string note = "pending";

            if (AllowOnlineCharacterTouch && GiveXpToOnlinePlayers)
            {
                Player* player = FindOnlinePlayer(member.GuidLow);
                if (player)
                {
                    delivered = GiveXpOnline(player, xpAmount);
                    note = delivered ? "delivered_online" : "online_delivery_failed_or_capped";
                }
            }

            RecordXpAward(runId, member, xpAmount, percent, killedBosses, totalBosses, delivered, note);

            if (AuditFileLoot)
                AuditLog("XP", "run #" + std::to_string(runId) + " member=" + member.Name +
                    " guid=" + std::to_string(member.GuidLow) + " level=" + std::to_string(uint32(member.Level)) +
                    " xp=" + std::to_string(xpAmount) + " percent=" + std::to_string(percent) +
                    " bosses=" + std::to_string(uint32(killedBosses)) + "/" + std::to_string(uint32(totalBosses)) +
                    " delivery=" + (delivered ? "online" : "pending") + " cap=" + std::to_string(cap));
        }
    }

    static void DeliverPendingXp(Player* player)
    {
        if (!AllowOnlineCharacterTouch || !DeliverPendingXpOnLogin || !player || !IsPlayerBotAccount(player))
            return;

        if (player->GetLevel() >= GetEffectiveXpLevelCap())
            return;

        uint32 guidLow = player->GetGUID().GetCounter();
        QueryResult pending = CharacterDatabase.Query(
            "SELECT id, xp_amount FROM playerbot_dungeon_xp_award "
            "WHERE guid = {} AND delivered = 0 ORDER BY id ASC LIMIT {}",
            guidLow, MaxPendingXpDeliveriesPerLogin);

        if (!pending)
            return;

        uint32 delivered = 0;
        do
        {
            Field* f = pending->Fetch();
            uint64 awardId = f[0].Get<uint64>();
            uint32 xpAmount = f[1].Get<uint32>();

            bool ok = GiveXpOnline(player, xpAmount);
            CharacterDatabase.Execute(
                "UPDATE playerbot_dungeon_xp_award SET delivered = {}, delivered_at = {}, note = '{}' WHERE id = {}",
                ok ? 2 : 1, ok ? GameTime::GetGameTime().count() : 0, ok ? "delivered_on_login" : "delivery_failed_or_capped", awardId);

            if (AuditFileLoot)
                AuditLog("XP_DELIVERY", "player=" + player->GetName() + " guid=" + std::to_string(guidLow) +
                    " awardId=" + std::to_string(awardId) + " xp=" + std::to_string(xpAmount) +
                    " delivered=" + std::to_string(ok ? 1 : 0));

            if (ok)
                ++delivered;

            if (player->GetLevel() >= GetEffectiveXpLevelCap())
                break;
        } while (pending->NextRow());

        if (delivered > 0)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Dungeon sim delivered {} pending XP award(s).", delivered);
            DebugLog("Delivered " + std::to_string(delivered) + " pending XP award(s) to " + player->GetName());
        }
    }

    static void DeliverPendingLoot(Player* player)
    {
        if (!AllowOnlineCharacterTouch || !DeliverPendingLootOnLogin || !player || !IsPlayerBotAccount(player))
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

            if (AuditFileLoot)
                AuditLog("LOOT_DELIVERY", "player=" + player->GetName() + " guid=" + std::to_string(guidLow) +
                    " awardId=" + std::to_string(awardId) + " item=" + std::to_string(itemEntry) +
                    " stored=" + std::to_string(stored ? 1 : 0) + " equipped=" + std::to_string(equipped ? 1 : 0) +
                    " upgradeScore=" + std::to_string(upgradeScore));

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

            uint8 originalKilled = killed;
            uint8 minBossKills = uint8(std::min<uint32>(SimMinimumBossKillsForLoot, bossCount));
            if (AwardLoot && bossCount > 0 && minBossKills > 0 && (!isRaid || SimMinimumBossKillsApplyToRaids) && killed < minBossKills)
            {
                killed = minBossKills;
                if (result == RESULT_WIPE || result == RESULT_ABANDONED)
                    result = RESULT_PARTIAL;

                AuditLog("LOOT", "run #" + std::to_string(runId) + " minimum loot boss floor raised kills " +
                    std::to_string(originalKilled) + " -> " + std::to_string(killed) +
                    " for " + GetDungeonTemplateName(dungeonId));
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
            AuditLog("RUN_END", "run #" + std::to_string(runId) + " " + TeamName(team) + " " + dungeonName +
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
            {
                AwardLootForRun(runId, dungeonId);
                AwardXpForRun(runId, dungeonId, killed, bossCount);
            }
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

            QueryResult members = CharacterDatabase.Query("SELECT COUNT(DISTINCT guid) FROM playerbot_dungeon_run_member WHERE run_id = {}", runId);
            uint32 memberCount = members ? members->Fetch()[0].Get<uint32>() : 0;
            uint32 onlineCount = StatusShowOnlineMembers ? CountOnlineRunMembers(runId, false) : 0;
            uint32 movableCount = StatusShowOnlineMembers ? CountOnlineRunMembers(runId, true) : 0;

            std::string onlineText;
            if (StatusShowOnlineMembers)
                onlineText = " online=" + std::to_string(onlineCount) + " movable=" + std::to_string(movableCount);

            StatusLog(" - #" + std::to_string(runId) + " " + TeamName(team) + " " + GetDungeonTemplateName(dungeonId) +
                " guild=" + std::to_string(guildId) + " members=" + std::to_string(memberCount) + onlineText +
                " state=" + std::to_string(state) + " q=" + std::to_string(quality) +
                " moved=" + std::to_string(moved) + " eta=" + std::to_string(remaining / 60) + "m");
        } while (active->NextRow());
    }


    static std::string TrimCopy(std::string value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.erase(value.begin());
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.pop_back();
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);
        return value;
    }

    static std::string PopFirstToken(std::string& value)
    {
        value = TrimCopy(value);
        if (value.empty())
            return "";

        size_t pos = value.find_first_of(" \t\r\n");
        if (pos == std::string::npos)
        {
            std::string token = value;
            value.clear();
            return token;
        }

        std::string token = value.substr(0, pos);
        value = TrimCopy(value.substr(pos + 1));
        return token;
    }

    static std::string PopLastToken(std::string& value)
    {
        value = TrimCopy(value);
        if (value.empty())
            return "";

        size_t pos = value.find_last_of(" \t\r\n");
        if (pos == std::string::npos)
        {
            std::string token = value;
            value.clear();
            return token;
        }

        std::string token = value.substr(pos + 1);
        value = TrimCopy(value.substr(0, pos));
        return token;
    }

    static bool IsUnsignedNumber(std::string const& value)
    {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); });
    }

    static bool LoadDungeonTemplateByNameOrId(std::string const& rawName, DungeonTemplate& dungeon)
    {
        std::string name = TrimCopy(rawName);
        if (name.empty())
            return false;

        name = ResolveDungeonAlias(name);

        QueryResult result;
        if (IsUnsignedNumber(name))
        {
            result = WorldDatabase.Query(
                "SELECT id, name, map_id, difficulty, min_level, max_level, target_level, group_size, is_raid, "
                "entrance_map, entrance_x, entrance_y, entrance_z, entrance_o "
                "FROM playerbot_dungeon_template WHERE id = {} LIMIT 1",
                uint32(std::stoul(name)));
        }
        else
        {
            std::string escaped = name;
            WorldDatabase.EscapeString(escaped);
            result = WorldDatabase.Query(
                "SELECT id, name, map_id, difficulty, min_level, max_level, target_level, group_size, is_raid, "
                "entrance_map, entrance_x, entrance_y, entrance_z, entrance_o "
                "FROM playerbot_dungeon_template "
                "WHERE LOWER(name) = LOWER('{}') OR LOWER(name) LIKE LOWER('{}%') "
                "ORDER BY CASE WHEN LOWER(name) = LOWER('{}') THEN 0 ELSE 1 END, id LIMIT 1",
                escaped, escaped, escaped);
        }

        if (!result)
            return false;

        Field* f = result->Fetch();
        dungeon.Id = f[0].Get<uint32>();
        dungeon.Name = f[1].Get<std::string>();
        dungeon.MapId = f[2].Get<uint32>();
        dungeon.Difficulty = f[3].Get<uint8>();
        dungeon.MinLevel = f[4].Get<uint8>();
        dungeon.MaxLevel = f[5].Get<uint8>();
        dungeon.TargetLevel = f[6].Get<uint8>();
        dungeon.GroupSize = f[7].Get<uint8>();
        dungeon.IsRaid = f[8].Get<uint8>() != 0;
        dungeon.EntranceMap = f[9].Get<uint32>();
        dungeon.X = f[10].Get<float>();
        dungeon.Y = f[11].Get<float>();
        dungeon.Z = f[12].Get<float>();
        dungeon.O = f[13].Get<float>();
        return true;
    }

    static bool LoadBotCandidateByName(std::string const& rawName, BotCandidate& bot)
    {
        std::string name = TrimCopy(rawName);
        if (name.empty())
            return false;

        std::string escaped = name;
        CharacterDatabase.EscapeString(escaped);
        QueryResult result = CharacterDatabase.Query(
            "SELECT c.guid, c.name, c.level, c.race, c.class, COALESCE(g.guildid,0), c.account, c.online "
            "FROM characters c "
            "LEFT JOIN guild_member g ON g.guid = c.guid "
            "WHERE LOWER(c.name) = LOWER('{}') LIMIT 1",
            escaped);

        if (!result)
            return false;

        Field* f = result->Fetch();
        bot.GuidLow = f[0].Get<uint32>();
        bot.Name = f[1].Get<std::string>();
        bot.Level = f[2].Get<uint8>();
        bot.Race = f[3].Get<uint8>();
        bot.Class = f[4].Get<uint8>();
        bot.GuildId = f[5].Get<uint32>();
        bot.AccountId = f[6].Get<uint32>();
        bot.DbOnline = f[7].Get<uint8>() != 0;
        bot.Team = GetTeamFromRace(bot.Race);
        bot.PreferredRole = GuessRole(bot.Class);

        return AccountMatchesBotFilter(bot.AccountId);
    }

    static std::string GmStageKey(uint32 dungeonId, uint8 team)
    {
        return std::to_string(dungeonId) + ":" + std::to_string(uint32(team));
    }

    static bool InsertRunBossRows(uint64 runId, uint32 dungeonId)
    {
        QueryResult bosses = WorldDatabase.Query(
            "SELECT boss_order, boss_name FROM playerbot_dungeon_boss_template WHERE dungeon_template_id = {} ORDER BY boss_order",
            dungeonId);
        if (!bosses)
            return false;

        do
        {
            Field* f = bosses->Fetch();
            std::string bossName = f[1].Get<std::string>();
            CharacterDatabase.EscapeString(bossName);
            CharacterDatabase.Execute(
                "INSERT INTO playerbot_dungeon_run_boss (run_id, boss_order, boss_name, killed) VALUES ({},{},'{}',0)",
                runId, f[0].Get<uint8>(), bossName);
        } while (bosses->NextRow());

        return true;
    }

    static bool StartGmRun(ChatHandler* handler, DungeonTemplate const& dungeon, std::vector<BotCandidate> const& group, RunExecutionMode mode)
    {
        if (!handler)
            return false;

        bool realMode = mode == RUN_MODE_REAL;

        if (!Enable)
        {
            handler->PSendSysMessage("DungeonSim is disabled.");
            return false;
        }

        if (realMode && !AllowOnlineCharacterTouch)
        {
            handler->PSendSysMessage("DungeonSim REAL mode needs PlayerbotDungeonSim.AllowOnlineCharacterTouch = 1.");
            return false;
        }

        if (realMode && !TeleportOnlineMembers)
        {
            handler->PSendSysMessage("DungeonSim REAL mode needs PlayerbotDungeonSim.TeleportOnlineMembers = 1.");
            return false;
        }

        if (group.empty())
        {
            handler->PSendSysMessage("No bot members selected for {}.", dungeon.Name);
            return false;
        }

        if (!IsDungeonAllowed(dungeon.MapId) || !IsRaidAllowedByServerCap(dungeon))
        {
            handler->PSendSysMessage("{} is not available right now. Check disables/progression/level cap.", dungeon.Name);
            return false;
        }

        uint32 onlineReady = CountOnlineMovableGroupMembers(group);
        if (realMode && onlineReady < std::max<uint32>(1, MinOnlineMembersForLiveRun))
        {
            handler->PSendSysMessage("{} has {} staged bot(s), but only {} online/movable. Need at least {} for REAL mode.",
                dungeon.Name, uint32(group.size()), onlineReady, std::max<uint32>(1, MinOnlineMembersForLiveRun));
            LogLiveRosterDiagnostics(dungeon, group, onlineReady);
            return false;
        }

        uint32 now = GameTime::GetGameTime().count();
        uint32 duration = urand(MinDurationMinutes * MINUTE, MaxDurationMinutes * MINUTE);
        int32 quality = CalculateQuality(group, dungeon);
        uint32 guildId = GetRunGuildId(group);
        TeleportLocation loc = ResolveTeleportLocation(dungeon);

        bool madeLiveGroup = realMode && CreateLiveGroupIfPossible(group);
        uint32 movedOnline = (AllowOnlineCharacterTouch && TeleportOnlineMembers) ? TeleportOnlineGroupMembers(group, dungeon, loc) : 0;
        if (realMode && movedOnline == 0)
        {
            handler->PSendSysMessage("No staged online bots could be teleported for {} in REAL mode.", dungeon.Name);
            return false;
        }

        uint64 runId = AllocateRunId();
        if (!runId)
        {
            handler->PSendSysMessage("Could not allocate DungeonSim run id.");
            return false;
        }

        CharacterDatabase.Execute(
            "INSERT INTO playerbot_dungeon_run "
            "(id, dungeon_template_id, team, guild_id, state, started_at, ends_at, quality_score, planned_duration_sec, "
            "real_group_created, online_members_moved, entrance_map, entrance_x, entrance_y, entrance_z, entrance_o) "
            "VALUES ({},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{})",
            runId, dungeon.Id, group.front().Team, guildId, uint8(realMode ? RUN_REAL_ATTEMPT : RUN_ACTIVE), now, now + duration, quality, duration,
            madeLiveGroup ? 1 : 0, movedOnline, loc.Map, loc.X, loc.Y, loc.Z, loc.O);

        AuditRunMembers(runId, dungeon, group, "GM START " + RunModeText(mode));

        for (BotCandidate const& b : group)
        {
            CharacterDatabase.Execute(
                "INSERT IGNORE INTO playerbot_dungeon_run_member "
                "(run_id, guid, role, guild_id, level_at_start, joined_as_real_player) "
                "VALUES ({},{},{},{},{},0)",
                runId, b.GuidLow, uint8(b.PreferredRole), b.GuildId, b.Level);
        }

        InsertRunBossRows(runId, dungeon.Id);

        StatusLog("GM START " + RunModeText(mode) + " run #" + std::to_string(runId) + " " + TeamName(group.front().Team) + " " + dungeon.Name +
            " members=" + std::to_string(group.size()) + " liveGroup=" + std::to_string(madeLiveGroup ? 1 : 0) +
            " moved=" + std::to_string(movedOnline) + " dest=" + (loc.FromInstanceStart ? "instance" : "entrance"));

        if (realMode)
            ScheduleNativeEventSteps(runId, dungeon, group);

        handler->PSendSysMessage("DungeonSim GM {} run #{} started: {} {} member(s), moved {} online bot(s).",
            RunModeText(mode), uint32(runId), dungeon.Name, uint32(group.size()), movedOnline);
        return true;
    }


    static bool SelectBestBotGroupForDungeon(DungeonTemplate const& dungeon, RunExecutionMode mode, std::vector<BotCandidate>& bestGroup, uint8& bestFactionWeight, uint32& bestOnline)
    {
        bestGroup.clear();
        bestFactionWeight = 0;
        bestOnline = 0;

        for (uint8 team : { uint8(TEAM_ALLIANCE), uint8(TEAM_HORDE) })
        {
            uint8 factionWeight = DungeonFactionWeight(dungeon, team);
            if (factionWeight == 0)
                continue;

            std::vector<BotCandidate> candidates = LoadEligibleBots(team, dungeon.MinLevel, dungeon.MaxLevel, dungeon.GroupSize * 40);
            if (mode == RUN_MODE_REAL && RequireOnlineBotsForRun)
            {
                candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](BotCandidate const& b)
                {
                    return !IsBotCandidateSafeToMove(FindOnlinePlayer(b.GuidLow), b);
                }), candidates.end());
            }

            std::vector<BotCandidate> group;
            if (!BuildGroup(candidates, dungeon, group))
                continue;

            uint32 online = CountOnlineMovableGroupMembers(group);
            if (bestGroup.empty() || factionWeight > bestFactionWeight || (factionWeight == bestFactionWeight && online > bestOnline))
            {
                bestOnline = online;
                bestFactionWeight = factionWeight;
                bestGroup = group;
            }
        }

        return !bestGroup.empty();
    }

    static bool CommandCreate(ChatHandler* handler, std::string const& rawArgs)
    {
        std::string args = TrimCopy(rawArgs);
        RunExecutionMode mode = ConfiguredRunMode;
        std::string first = PopFirstToken(args);
        std::string dungeonName;

        bool matchedMode = false;
        RunExecutionMode parsedMode = ParseRunModeToken(first, mode, &matchedMode);
        if (matchedMode)
        {
            mode = parsedMode;
            dungeonName = TrimCopy(args);
        }
        else
        {
            dungeonName = TrimCopy(rawArgs);
        }

        if (dungeonName.empty())
        {
            handler->PSendSysMessage("Usage: .dng-sim create [sim|real] <INSTANCE>");
            return true;
        }

        DungeonTemplate dungeon;
        if (!LoadDungeonTemplateByNameOrId(dungeonName, dungeon))
        {
            handler->PSendSysMessage("DungeonSim: unknown dungeon `{}`.", dungeonName);
            return true;
        }

        std::vector<BotCandidate> bestGroup;
        uint32 bestOnline = 0;
        uint8 bestFactionWeight = 0;
        SelectBestBotGroupForDungeon(dungeon, mode, bestGroup, bestFactionWeight, bestOnline);

        if (bestGroup.empty())
        {
            handler->PSendSysMessage("DungeonSim: no {} bot group found for {} levels {}-{}.",
                RunModeText(mode), dungeon.Name, uint32(dungeon.MinLevel), uint32(dungeon.MaxLevel));
            return true;
        }

        handler->PSendSysMessage("DungeonSim selected {} group for {} in {} mode (faction weight {}%).", TeamName(bestGroup.front().Team), dungeon.Name, RunModeText(mode), uint32(bestFactionWeight));

        StartGmRun(handler, dungeon, bestGroup, mode);
        return true;
    }

    static bool CommandInvite(ChatHandler* handler, std::string const& rest)
    {
        std::string args = rest;
        std::string botName = PopLastToken(args);
        std::string dungeonName = TrimCopy(args);

        if (dungeonName.empty() || botName.empty())
        {
            handler->PSendSysMessage("Usage: .dng-sim inv <INSTANCE> <BOTNAME>");
            return true;
        }

        DungeonTemplate dungeon;
        if (!LoadDungeonTemplateByNameOrId(dungeonName, dungeon))
        {
            handler->PSendSysMessage("DungeonSim: unknown dungeon `{}`.", dungeonName);
            return true;
        }

        BotCandidate bot;
        if (!LoadBotCandidateByName(botName, bot))
        {
            handler->PSendSysMessage("DungeonSim: bot `{}` not found or does not match configured bot account prefix/range.", botName);
            return true;
        }

        uint8 factionWeight = DungeonFactionWeight(dungeon, bot.Team);
        if (factionWeight == 0)
        {
            handler->PSendSysMessage("{} cannot be staged for {}: {} is not allowed for this instance by DungeonSim faction rules.", bot.Name, dungeon.Name, TeamName(bot.Team));
            return true;
        }
        if (factionWeight < 100)
            handler->PSendSysMessage("Note: {} is an unusual {} choice for {} (weight {}%).", TeamName(bot.Team), TeamName(bot.Team), dungeon.Name, uint32(factionWeight));

        if (bot.Level < dungeon.MinLevel || bot.Level > dungeon.MaxLevel)
            handler->PSendSysMessage("Warning: {} is level {}, outside {} range {}-{}.", bot.Name, uint32(bot.Level), dungeon.Name, uint32(dungeon.MinLevel), uint32(dungeon.MaxLevel));

        if (!IsBotCandidateSafeToMove(FindOnlinePlayer(bot.GuidLow), bot))
            handler->PSendSysMessage("Warning: {} is not currently online/movable, so the staged run may not start yet.", bot.Name);

        std::string key = GmStageKey(dungeon.Id, bot.Team);
        std::vector<BotCandidate>& staged = _gmStagedGroups[key];
        if (std::any_of(staged.begin(), staged.end(), [&](BotCandidate const& b) { return b.GuidLow == bot.GuidLow; }))
        {
            handler->PSendSysMessage("{} is already staged for {}.", bot.Name, dungeon.Name);
            return true;
        }

        staged.push_back(bot);
        handler->PSendSysMessage("Staged {} for {} ({}/{}).", bot.Name, dungeon.Name, uint32(staged.size()), uint32(dungeon.GroupSize));

        if (staged.size() >= dungeon.GroupSize)
        {
            std::vector<BotCandidate> group(staged.begin(), staged.begin() + dungeon.GroupSize);
            std::unordered_set<uint32> seen;
            group.erase(std::remove_if(group.begin(), group.end(), [&](BotCandidate const& b)
            {
                if (seen.find(b.GuidLow) != seen.end())
                    return true;
                seen.insert(b.GuidLow);
                return false;
            }), group.end());

            if (group.size() < dungeon.GroupSize)
            {
                handler->PSendSysMessage("DungeonSim: staged {} has duplicate bot entries; removed duplicates, now {}/{}. Stage another bot.", dungeon.Name, uint32(group.size()), uint32(dungeon.GroupSize));
                staged = group;
                return true;
            }

            if (StartGmRun(handler, dungeon, group, ConfiguredRunMode))
                staged.erase(staged.begin(), staged.begin() + dungeon.GroupSize);
        }

        return true;
    }

    static bool CommandRemove(ChatHandler* handler, std::string const& rest)
    {
        std::string args = rest;
        std::string botName = PopLastToken(args);
        std::string dungeonName = TrimCopy(args);

        if (dungeonName.empty() || botName.empty())
        {
            handler->PSendSysMessage("Usage: .dng-sim rmv <INSTANCE> <BOTNAME>");
            return true;
        }

        DungeonTemplate dungeon;
        if (!LoadDungeonTemplateByNameOrId(dungeonName, dungeon))
        {
            handler->PSendSysMessage("DungeonSim: unknown dungeon `{}`.", dungeonName);
            return true;
        }

        uint32 removed = 0;
        for (uint8 team : { uint8(TEAM_ALLIANCE), uint8(TEAM_HORDE) })
        {
            std::string key = GmStageKey(dungeon.Id, team);
            auto it = _gmStagedGroups.find(key);
            if (it == _gmStagedGroups.end())
                continue;

            auto& staged = it->second;
            size_t before = staged.size();
            staged.erase(std::remove_if(staged.begin(), staged.end(), [&](BotCandidate const& b)
            {
                return LowerCopy(b.Name) == LowerCopy(botName);
            }), staged.end());
            removed += uint32(before - staged.size());
        }

        handler->PSendSysMessage("DungeonSim: removed {} staged bot(s) named {} from {}.", removed, botName, dungeon.Name);
        return true;
    }

    static bool CommandWipe(ChatHandler* handler, std::string const& dungeonName)
    {
        DungeonTemplate dungeon;
        if (!LoadDungeonTemplateByNameOrId(dungeonName, dungeon))
        {
            handler->PSendSysMessage("DungeonSim: unknown dungeon `{}`.", dungeonName);
            return true;
        }

        for (uint8 team : { uint8(TEAM_ALLIANCE), uint8(TEAM_HORDE) })
            _gmStagedGroups.erase(GmStageKey(dungeon.Id, team));

        uint32 now = GameTime::GetGameTime().count();
        CharacterDatabase.Execute(
            "UPDATE playerbot_dungeon_run SET state = {}, result = {}, ends_at = {} "
            "WHERE dungeon_template_id = {} AND state IN (0,1,4)",
            uint8(RUN_FAILED), uint8(RESULT_ABANDONED), now, dungeon.Id);
        CharacterDatabase.Execute(
            "UPDATE playerbot_dungeon_player_invite SET state = 4, note = 'gm_wipe' "
            "WHERE dungeon_template_id = {} AND state = 0",
            dungeon.Id);

        handler->PSendSysMessage("DungeonSim: wiped staged groups and active runs for {}.", dungeon.Name);
        StatusLog("GM WIPE " + dungeon.Name + ": staged groups cleared and active runs marked abandoned.");
        return true;
    }


    static bool CommandWaypoint(ChatHandler* handler, std::string args)
    {
        std::string action = LowerCopy(PopFirstToken(args));
        std::string instanceToken = PopFirstToken(args);

        if (action.empty() || instanceToken.empty())
        {
            handler->PSendSysMessage("Usage: .dng-sim wp add|list|del|clear <INSTANCE> [ORDER] [LABEL]");
            handler->PSendSysMessage("Example: stand in WC, then .dng-sim wp add WC 1 first_pull");
            return true;
        }

        DungeonTemplate dungeon;
        if (!LoadDungeonTemplateByNameOrId(instanceToken, dungeon))
        {
            handler->PSendSysMessage("DungeonSim: unknown dungeon `{}`.", instanceToken);
            return true;
        }

        if (action == "list")
        {
            QueryResult result = WorldDatabase.Query(
                "SELECT step_order, map_id, position_x, position_y, position_z, orientation, label, enabled "
                "FROM playerbot_dungeon_waypoint WHERE dungeon_template_id = {} ORDER BY step_order ASC",
                dungeon.Id);

            handler->PSendSysMessage("DungeonSim waypoints for {}:", dungeon.Name);
            if (!result)
            {
                handler->PSendSysMessage("  none");
                return true;
            }

            do
            {
                Field* f = result->Fetch();
                handler->PSendSysMessage("  #{} map={} x={} y={} z={} o={} label='{}' enabled={}",
                    f[0].Get<uint32>(), f[1].Get<uint32>(), f[2].Get<float>(), f[3].Get<float>(), f[4].Get<float>(), f[5].Get<float>(), f[6].Get<std::string>(), f[7].Get<uint8>());
            } while (result->NextRow());
            return true;
        }

        if (action == "clear")
        {
            WorldDatabase.Execute("DELETE FROM playerbot_dungeon_waypoint WHERE dungeon_template_id = {}", dungeon.Id);
            handler->PSendSysMessage("DungeonSim: cleared all waypoints for {}.", dungeon.Name);
            return true;
        }

        if (action == "del" || action == "delete" || action == "rm")
        {
            std::string orderText = PopFirstToken(args);
            if (!IsUnsignedNumber(orderText))
            {
                handler->PSendSysMessage("Usage: .dng-sim wp del <INSTANCE> <ORDER>");
                return true;
            }
            uint32 order = uint32(std::stoul(orderText));
            WorldDatabase.Execute("DELETE FROM playerbot_dungeon_waypoint WHERE dungeon_template_id = {} AND step_order = {}", dungeon.Id, order);
            handler->PSendSysMessage("DungeonSim: removed waypoint #{} for {}.", order, dungeon.Name);
            return true;
        }

        if (action == "add")
        {
            std::string orderText = PopFirstToken(args);
            if (!IsUnsignedNumber(orderText))
            {
                handler->PSendSysMessage("Usage: .dng-sim wp add <INSTANCE> <ORDER> [LABEL]");
                return true;
            }

            Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
            if (!player)
            {
                handler->PSendSysMessage("Waypoint add must be used in-game by a GM player.");
                return true;
            }

            uint32 order = uint32(std::stoul(orderText));
            std::string label = TrimCopy(args);
            if (label.empty())
                label = "step_" + std::to_string(order);
            WorldDatabase.EscapeString(label);

            WorldDatabase.Execute(
                "INSERT INTO playerbot_dungeon_waypoint "
                "(dungeon_template_id, step_order, map_id, position_x, position_y, position_z, orientation, label, enabled) "
                "VALUES ({},{},{},{},{},{},{},'{}',1) "
                "ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=1",
                dungeon.Id, order, player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation(), label);

            handler->PSendSysMessage("DungeonSim: saved waypoint #{} for {} at map={} x={} y={} z={}.",
                order, dungeon.Name, player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
            return true;
        }

        handler->PSendSysMessage("DungeonSim: unknown waypoint action `{}`. Use add/list/del/clear.", action);
        return true;
    }

    static bool CommandNativeStep(ChatHandler* handler, std::string args)
    {
        std::string action = LowerCopy(PopFirstToken(args));
        std::string instanceToken = PopFirstToken(args);

        if (action.empty() || instanceToken.empty())
        {
            handler->PSendSysMessage("Usage: .dng-sim step add|list|del|clear <INSTANCE> [ORDER] [TYPE] [WAIT] [LABEL]");
            handler->PSendSysMessage("Types: move, teleport, wait, log, pull, anchor. Example: .dng-sim step add WC 10 move 15 first_pull");
            return true;
        }

        DungeonTemplate dungeon;
        if (!LoadDungeonTemplateByNameOrId(instanceToken, dungeon))
        {
            handler->PSendSysMessage("DungeonSim: unknown dungeon `{}`.", instanceToken);
            return true;
        }

        if (action == "list")
        {
            QueryResult result = WorldDatabase.Query(
                "SELECT step_order, step_type, map_id, position_x, position_y, position_z, orientation, wait_seconds, label, enabled "
                "FROM playerbot_dungeon_event_step WHERE dungeon_template_id = {} ORDER BY step_order ASC",
                dungeon.Id);

            handler->PSendSysMessage("DungeonSim native event steps for {}:", dungeon.Name);
            if (!result)
            {
                handler->PSendSysMessage("  none");
                return true;
            }

            do
            {
                Field* f = result->Fetch();
                handler->PSendSysMessage("  #{} type={} wait={} map={} x={} y={} z={} o={} label='{}' enabled={}",
                    f[0].Get<uint32>(), f[1].Get<std::string>(), f[7].Get<uint32>(), f[2].Get<uint32>(), f[3].Get<float>(), f[4].Get<float>(), f[5].Get<float>(), f[6].Get<float>(), f[8].Get<std::string>(), f[9].Get<uint8>());
            } while (result->NextRow());
            return true;
        }

        if (action == "clear")
        {
            WorldDatabase.Execute("DELETE FROM playerbot_dungeon_event_step WHERE dungeon_template_id = {}", dungeon.Id);
            handler->PSendSysMessage("DungeonSim: cleared all native event steps for {}.", dungeon.Name);
            return true;
        }

        if (action == "del" || action == "delete" || action == "rm")
        {
            std::string orderText = PopFirstToken(args);
            if (!IsUnsignedNumber(orderText))
            {
                handler->PSendSysMessage("Usage: .dng-sim step del <INSTANCE> <ORDER>");
                return true;
            }
            uint32 order = uint32(std::stoul(orderText));
            WorldDatabase.Execute("DELETE FROM playerbot_dungeon_event_step WHERE dungeon_template_id = {} AND step_order = {}", dungeon.Id, order);
            handler->PSendSysMessage("DungeonSim: removed native step #{} for {}.", order, dungeon.Name);
            return true;
        }

        if (action == "add")
        {
            std::string orderText = PopFirstToken(args);
            std::string typeText = PopFirstToken(args);
            std::string waitText = PopFirstToken(args);

            if (!IsUnsignedNumber(orderText) || typeText.empty())
            {
                handler->PSendSysMessage("Usage: .dng-sim step add <INSTANCE> <ORDER> <TYPE> [WAIT_SECONDS] [LABEL]");
                return true;
            }

            Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
            if (!player)
            {
                handler->PSendSysMessage("Native step add must be used in-game by a GM player.");
                return true;
            }

            uint32 order = uint32(std::stoul(orderText));
            std::string type = NormalizeNativeStepType(typeText);
            uint32 waitSeconds = 0;
            if (IsUnsignedNumber(waitText))
                waitSeconds = uint32(std::stoul(waitText));
            else if (!waitText.empty())
                args = waitText + " " + args;

            std::string label = TrimCopy(args);
            if (label.empty())
                label = type + "_" + std::to_string(order);
            WorldDatabase.EscapeString(label);
            WorldDatabase.EscapeString(type);

            WorldDatabase.Execute(
                "INSERT INTO playerbot_dungeon_event_step "
                "(dungeon_template_id, step_order, step_type, map_id, position_x, position_y, position_z, orientation, wait_seconds, label, enabled) "
                "VALUES ({},{},'{}',{},{},{},{},{},{},'{}',1) "
                "ON DUPLICATE KEY UPDATE step_type=VALUES(step_type), map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), wait_seconds=VALUES(wait_seconds), label=VALUES(label), enabled=1",
                dungeon.Id, order, type, player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation(), waitSeconds, label);

            handler->PSendSysMessage("DungeonSim: saved native step #{} type={} wait={} for {} at map={} x={} y={} z={}.",
                order, type, waitSeconds, dungeon.Name, player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
            return true;
        }

        handler->PSendSysMessage("DungeonSim: unknown native step action `{}`. Use add/list/del/clear.", action);
        return true;
    }

    static bool CommandMode(ChatHandler* handler, std::string const& rest)
    {
        std::string token = TrimCopy(rest);
        bool matched = false;
        RunExecutionMode parsed = ParseRunModeToken(token, ConfiguredRunMode, &matched);
        if (token.empty() || !matched)
        {
            handler->PSendSysMessage("DungeonSim mode is {}. Config key: PlayerbotDungeonSim.RunMode = SIM or REAL.", RunModeText(ConfiguredRunMode));
            handler->PSendSysMessage("SIM  = timed dungeon society run: form group, optional one-shot teleport, resolve bosses/loot by simulation.");
            handler->PSendSysMessage("REAL = live online bot run: require movable bots, create real group, teleport in, execute native route/event steps.");
            handler->PSendSysMessage("For one GM run use: .dng-sim create sim DM  or  .dng-sim create real WC");
            return true;
        }

        handler->PSendSysMessage("DungeonSim parsed mode `{}` as {}. This command does not rewrite worldserver.conf; set PlayerbotDungeonSim.RunMode to make it persistent.", token, RunModeText(parsed));
        return true;
    }


    static uint64 ParseOptionalRunId(std::string const& rest)
    {
        std::string token = TrimCopy(rest);
        if (!token.empty() && IsUnsignedNumber(token))
            return uint64(std::stoull(token));

        QueryResult active = CharacterDatabase.Query("SELECT id FROM playerbot_dungeon_run WHERE state IN (1,4) ORDER BY ends_at ASC LIMIT 1");
        if (!active)
            return 0;

        return active->Fetch()[0].Get<uint64>();
    }

    static std::string RunModeLabelFromState(uint8 state)
    {
        if (state == uint8(RUN_REAL_ATTEMPT))
            return "REAL";
        if (state == uint8(RUN_ACTIVE))
            return "SIM";
        return "state" + std::to_string(uint32(state));
    }

    static bool CommandWhere(ChatHandler* handler, std::string const& rest)
    {
        if (!handler)
            return false;

        uint64 runId = ParseOptionalRunId(rest);
        if (!runId)
        {
            handler->PSendSysMessage("DungeonSim: no active run found. Usage: .dng-sim where [RUN_ID]");
            return true;
        }

        QueryResult run = CharacterDatabase.Query(
            "SELECT r.id, r.dungeon_template_id, r.team, r.state, r.online_members_moved, r.entrance_map, r.entrance_x, r.entrance_y, r.entrance_z, r.entrance_o "
            "FROM playerbot_dungeon_run r WHERE r.id = {} LIMIT 1", runId);
        if (!run)
        {
            handler->PSendSysMessage("DungeonSim: run #{} not found.", runId);
            return true;
        }

        Field* rf = run->Fetch();
        uint32 dungeonId = rf[1].Get<uint32>();
        uint8 team = rf[2].Get<uint8>();
        uint8 state = rf[3].Get<uint8>();
        uint32 moved = rf[4].Get<uint32>();
        uint32 entranceMap = rf[5].Get<uint32>();
        float entranceX = rf[6].Get<float>();
        float entranceY = rf[7].Get<float>();
        float entranceZ = rf[8].Get<float>();

        handler->PSendSysMessage("DungeonSim run #{} {} {} {} moved={} storedDest map={} x={} y={} z={}",
            runId, RunModeLabelFromState(state), TeamName(team), GetDungeonTemplateName(dungeonId), moved, entranceMap, entranceX, entranceY, entranceZ);

        QueryResult members = CharacterDatabase.Query(
            "SELECT DISTINCT rm.guid, c.name, c.level, c.class, rm.role, COALESCE(c.online,0), c.map, c.position_x, c.position_y, c.position_z, c.zone "
            "FROM playerbot_dungeon_run_member rm "
            "LEFT JOIN characters c ON c.guid = rm.guid "
            "WHERE rm.run_id = {} ORDER BY rm.role ASC, c.name ASC", runId);

        if (!members)
        {
            handler->PSendSysMessage("  no members recorded");
            return true;
        }

        do
        {
            Field* f = members->Fetch();
            uint32 guid = f[0].Get<uint32>();
            std::string name = f[1].Get<std::string>();
            uint8 level = f[2].Get<uint8>();
            uint8 cls = f[3].Get<uint8>();
            uint8 role = f[4].Get<uint8>();
            bool dbOnline = f[5].Get<uint8>() != 0;
            uint32 dbMap = f[6].Get<uint32>();
            float dbX = f[7].Get<float>();
            float dbY = f[8].Get<float>();
            float dbZ = f[9].Get<float>();
            uint32 dbZone = f[10].Get<uint32>();

            Player* player = FindOnlinePlayer(guid);
            if (player)
            {
                handler->PSendSysMessage("  {} guid={} lvl={} class={} role={} dbOnline={} LIVE map={} zone={} x={} y={} z={} group={} teleporting={}",
                    name, guid, uint32(level), uint32(cls), RoleText(Role(role)), dbOnline ? 1 : 0,
                    player->GetMapId(), player->GetZoneId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
                    player->GetGroup() ? 1 : 0, player->IsBeingTeleported() ? 1 : 0);
            }
            else
            {
                handler->PSendSysMessage("  {} guid={} lvl={} class={} role={} dbOnline={} OFFLINE/NO_OBJECT dbMap={} dbZone={} x={} y={} z={}",
                    name, guid, uint32(level), uint32(cls), RoleText(Role(role)), dbOnline ? 1 : 0, dbMap, dbZone, dbX, dbY, dbZ);
            }
        } while (members->NextRow());

        return true;
    }

    static bool CommandObserve(ChatHandler* handler, std::string const& rest)
    {
        if (!handler || !handler->GetSession() || !handler->GetSession()->GetPlayer())
            return false;

        Player* gm = handler->GetSession()->GetPlayer();
        uint64 runId = ParseOptionalRunId(rest);
        if (!runId)
        {
            handler->PSendSysMessage("DungeonSim: no active run found. Usage: .dng-sim observe [RUN_ID]");
            return true;
        }

        QueryResult members = CharacterDatabase.Query(
            "SELECT DISTINCT rm.guid, c.name, c.account, rm.role "
            "FROM playerbot_dungeon_run_member rm "
            "LEFT JOIN characters c ON c.guid = rm.guid "
            "WHERE rm.run_id = {} ORDER BY CASE WHEN rm.role = 1 THEN 0 ELSE 1 END, c.name ASC", runId);

        if (!members)
        {
            handler->PSendSysMessage("DungeonSim: run #{} has no members.", runId);
            return true;
        }

        Player* target = nullptr;
        std::string targetName;
        do
        {
            Field* f = members->Fetch();
            uint32 guid = f[0].Get<uint32>();
            std::string name = f[1].Get<std::string>();
            uint32 accountId = f[2].Get<uint32>();
            BotCandidate b;
            b.GuidLow = guid;
            b.Name = name;
            b.AccountId = accountId;

            Player* candidate = FindOnlinePlayer(guid);
            if (IsBotCandidateSafeToMove(candidate, b))
            {
                target = candidate;
                targetName = name;
                break;
            }
        } while (members->NextRow());

        if (!target)
        {
            handler->PSendSysMessage("DungeonSim: no online/movable bot found for run #{}.", runId);
            return true;
        }

        if (target->GetGroup() && !gm->GetGroup())
        {
            target->GetGroup()->AddMember(gm);
            handler->PSendSysMessage("DungeonSim: temporarily joined you to {}'s group so the instance copy should match.", targetName);
        }
        else if (target->GetGroup() && gm->GetGroup() && gm->GetGroup() != target->GetGroup())
        {
            handler->PSendSysMessage("DungeonSim: you are already in a different group; teleporting anyway may put you in a different instance copy.");
        }

        gm->TeleportTo(target->GetMapId(), target->GetPositionX() + 2.0f, target->GetPositionY() + 2.0f, target->GetPositionZ(), target->GetOrientation());
        handler->PSendSysMessage("DungeonSim: observing run #{} near {} at map={} x={} y={} z={}.",
            runId, targetName, target->GetMapId(), target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());

        AuditLog("OBSERVE", "GM " + gm->GetName() + " observing run #" + std::to_string(runId) + " near " + targetName +
            " map=" + std::to_string(target->GetMapId()) + " x=" + std::to_string(target->GetPositionX()) +
            " y=" + std::to_string(target->GetPositionY()) + " z=" + std::to_string(target->GetPositionZ()));
        return true;
    }

    static bool HandleGmDngSimCommand(ChatHandler* handler, std::string args)
    {
        if (!handler)
            return false;

        args = TrimCopy(args);
        if (args.rfind(".dng-sim", 0) == 0)
            args = TrimCopy(args.substr(8));

        std::string sub = LowerCopy(PopFirstToken(args));
        if (sub.empty() || sub == "help")
        {
            handler->PSendSysMessage("DungeonSim GM commands:");
            handler->PSendSysMessage(".dng-sim create [sim|real] <INSTANCE> - auto-build a bot group and start a SIM or REAL run");
            handler->PSendSysMessage("Common aliases: RFC, WC, DM, SFK, BFD, Stocks, Gnomer, RFK, RFD, SMGY/SMLib/SMArm/SMCath, ZF, Mara, ST, BRD, LBRS, UBRS, DME/DMW/DMN, Scholo, Strat, MC, Ony.");
            handler->PSendSysMessage(".dng-sim inv <INSTANCE> <BOTNAME> - stage named bot; starts when group is full");
            handler->PSendSysMessage(".dng-sim rmv <INSTANCE> <BOTNAME> - remove named bot from staged group");
            handler->PSendSysMessage(".dng-sim wipe <INSTANCE> - clear staged group and mark active runs abandoned");
            handler->PSendSysMessage(".dng-sim mode - explain current SIM/REAL mode");
            handler->PSendSysMessage(".dng-sim where [RUN_ID] - list exact member positions for an active run");
            handler->PSendSysMessage(".dng-sim observe [RUN_ID] - GM teleport near an online run member, joining their group when possible");
            handler->PSendSysMessage(".dng-sim wp add|list|del|clear <INSTANCE> [ORDER] [LABEL] - record/tune in-instance teleport waypoints from your GM position");
            handler->PSendSysMessage(".dng-sim step add|list|del|clear <INSTANCE> [ORDER] [TYPE] [WAIT] [LABEL] - record native route/event steps");
            return true;
        }

        if (sub == "create")
            return CommandCreate(handler, args);
        if (sub == "inv")
            return CommandInvite(handler, args);
        if (sub == "rmv")
            return CommandRemove(handler, args);
        if (sub == "wipe")
            return CommandWipe(handler, args);
        if (sub == "mode")
            return CommandMode(handler, args);
        if (sub == "where" || sub == "loc" || sub == "location")
            return CommandWhere(handler, args);
        if (sub == "observe" || sub == "watch" || sub == "goto")
            return CommandObserve(handler, args);
        if (sub == "wp" || sub == "waypoint" || sub == "waypoints")
            return CommandWaypoint(handler, args);
        if (sub == "step" || sub == "event" || sub == "native")
            return CommandNativeStep(handler, args);

        handler->PSendSysMessage("DungeonSim: unknown subcommand `{}`. Use .dng-sim help.", sub);
        return true;
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
            std::vector<BotCandidate> group;

            RunExecutionMode selectedMode = ConfiguredRunMode;
            if (HybridSimReal)
                selectedMode = (urand(1, 100) <= std::min<uint32>(100, HybridRealChance)) ? RUN_MODE_REAL : RUN_MODE_SIM;

            auto selectGroupForMode = [&](RunExecutionMode modeToUse, std::vector<BotCandidate>& outGroup, uint8& outWeight, uint32& outOnline) -> bool
            {
                outGroup.clear();
                outWeight = 0;
                outOnline = 0;

                if (AutoUseGmGroupSelection)
                    return SelectBestBotGroupForDungeon(dungeon, modeToUse, outGroup, outWeight, outOnline);

                std::vector<BotCandidate> candidates = LoadEligibleBots(team, dungeon.MinLevel, dungeon.MaxLevel, dungeon.GroupSize * 10);
                if (modeToUse == RUN_MODE_REAL && RequireOnlineBotsForRun)
                {
                    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](BotCandidate const& b)
                    {
                        return !IsBotCandidateSafeToMove(FindOnlinePlayer(b.GuidLow), b);
                    }), candidates.end());
                }

                bool ok = BuildGroup(candidates, dungeon, outGroup);
                outWeight = outGroup.empty() ? 0 : DungeonFactionWeight(dungeon, outGroup.front().Team);
                outOnline = outGroup.empty() ? 0 : CountOnlineMovableGroupMembers(outGroup);
                return ok;
            };

            uint8 selectedWeight = 0;
            uint32 selectedOnline = 0;
            if (!selectGroupForMode(selectedMode, group, selectedWeight, selectedOnline))
            {
                if (HybridSimReal && selectedMode == RUN_MODE_REAL && HybridRealFallbackToSim)
                {
                    DebugLog("Hybrid pass found no REAL group for " + dungeon.Name + "; falling back to SIM group selection.");
                    selectedMode = RUN_MODE_SIM;
                    if (!selectGroupForMode(selectedMode, group, selectedWeight, selectedOnline))
                    {
                        DebugLog("Auto organic pass found no SIM group for " + dungeon.Name + " after REAL fallback.");
                        continue;
                    }
                }
                else
                {
                    DebugLog("Auto organic pass found no " + RunModeText(selectedMode) + " group for " + dungeon.Name +
                        (AutoUseGmGroupSelection ? " using GM-equivalent selection." : "."));
                    continue;
                }
            }

            if (AutoUseGmGroupSelection)
            {
                DebugLog("Auto organic selected " + TeamName(group.front().Team) + " group for " + dungeon.Name +
                    " in " + RunModeText(selectedMode) + " mode, factionWeight=" + std::to_string(uint32(selectedWeight)) +
                    " online=" + std::to_string(selectedOnline) + ".");
            }

            if (selectedMode == RUN_MODE_REAL)
                MaybeReservePlayerSlot(group, dungeon);
            CreateRun(dungeon, group, selectedMode);
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
        AuditFileLog = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.FileLog", true);
        AuditFilePath = sConfigMgr->GetOption<std::string>("PlayerbotDungeonSim.FileLogPath", "Logs/playerbot_dungeon_sim.log");
        AuditFileMovement = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.FileLogMovement", true);
        AuditFileLoot = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.FileLogLoot", true);
        AuditFileMembers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.FileLogMembers", true);
        TickSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.TickSeconds", 60);
        MinBotLevel = sConfigMgr->GetOption<uint8>("PlayerbotDungeonSim.MinBotLevel", 10);
        MaxGroupsPerTick = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxGroupsPerTick", 2);
        PreferGuildGroups = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.PreferGuildGroups", true);
        AllowPugs = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AllowPugs", true);
        RequireFullGroup = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RequireFullGroup", true);
        MinDurationMinutes = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MinDurationMinutes", 20);
        MaxDurationMinutes = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxDurationMinutes", 45);
        RealRunFirst = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RealRunFirst", true);
        ConfiguredRunMode = ParseRunModeToken(sConfigMgr->GetOption<std::string>("PlayerbotDungeonSim.RunMode", RealRunFirst ? "REAL" : "SIM"), RealRunFirst ? RUN_MODE_REAL : RUN_MODE_SIM);
        SimFallback = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.SimFallback", true);
        HybridSimReal = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.HybridSimReal", false);
        HybridRealChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.HybridRealChance", 20);
        HybridRealFallbackToSim = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.HybridRealFallbackToSim", true);
        SimMinimumBossKillsForLoot = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.SimMinimumBossKillsForLoot", 1);
        SimMinimumBossKillsApplyToRaids = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.SimMinimumBossKillsApplyToRaids", false);
        TeleportOnlineMembers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.TeleportOnlineMembers", true);
        CreateRealGroups = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.CreateRealGroups", true);
        TeleportOnlyIfOnline = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.TeleportOnlyIfOnline", false);
        TeleportInsideInstance = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.TeleportInsideInstance", true);
        ProgressiveTeleport = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.ProgressiveTeleport", false);
        ProgressiveTeleportStepSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.ProgressiveTeleportStepSeconds", 20);
        InstanceWaypointTeleport = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.InstanceWaypointTeleport", true);
        InstanceWaypointStepSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.InstanceWaypointStepSeconds", 15);
        NativeEventSteps = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.NativeEventSteps", true);
        NativeEventStepSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.NativeEventStepSeconds", 20);
        NativeEventMoveInsteadTeleport = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.NativeEventMoveInsteadTeleport", false);
        NativeEventLeaderOnly = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.NativeEventLeaderOnly", true);
        NativeFollowerLeash = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.NativeFollowerLeash", true);
        NativeFollowerLeashSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.NativeFollowerLeashSeconds", 5);
        NativeFollowerLeashDistance = sConfigMgr->GetOption<float>("PlayerbotDungeonSim.NativeFollowerLeashDistance", 18.0f);
        NativeFollowerHardTeleportDistance = sConfigMgr->GetOption<float>("PlayerbotDungeonSim.NativeFollowerHardTeleportDistance", 80.0f);
        MinOnlineMembersForLiveRun = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MinOnlineMembersForLiveRun", 1);
        RequireOnlineBotsForRun = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RequireOnlineBotsForRun", false);
        CleanupOfflineRunsWhenLiveOnly = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.CleanupOfflineRunsWhenLiveOnly", true);
        StatusShowOnlineMembers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.StatusShowOnlineMembers", true);
        PreferOnlineCandidatesForLiveRuns = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.PreferOnlineCandidatesForLiveRuns", true);
        LiveDebugRoster = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.LiveDebugRoster", true);
        LiveDebugMaxRosterLines = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.LiveDebugMaxRosterLines", 5);
        ManualTravelForPlayerJoinedRuns = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.ManualTravelForPlayerJoinedRuns", true);
        AllowOnlineCharacterTouch = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AllowOnlineCharacterTouch", false);
        StartupDelaySeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.StartupDelaySeconds", 180);
        AwardLoot = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AwardLoot", true);
        GiveLootToOnlinePlayers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.GiveLootToOnlinePlayers", true);
        EquipUsableLootOnline = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.EquipUsableLootOnline", false);
        DeliverPendingLootOnLogin = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.DeliverPendingLootOnLogin", true);
        MaxPendingDeliveriesPerLogin = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxPendingDeliveriesPerLogin", 12);
        AwardXp = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AwardXp", true);
        GiveXpToOnlinePlayers = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.GiveXpToOnlinePlayers", true);
        DeliverPendingXpOnLogin = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.DeliverPendingXpOnLogin", true);
        XpPercentOfLevel = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.XpPercentOfLevel", 25);
        XpScaleByBossKills = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.XpScaleByBossKills", true);
        XpMinimumPercentOnAnyKill = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.XpMinimumPercentOnAnyKill", 5);
        XpLevelCap = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.XpLevelCap", 0);
        MaxPendingXpDeliveriesPerLogin = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxPendingXpDeliveriesPerLogin", 12);
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
        UsePlayerbotConfig = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.UsePlayerbotConfig", true);
        BotAccountPrefix = sConfigMgr->GetOption<std::string>("PlayerbotDungeonSim.BotAccountPrefix", "auto");
        BotAccountMin = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.BotAccountMin", 0);
        BotAccountMax = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.BotAccountMax", 0);
        ImportPlayerbotConfigDefaults();
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
        SimulatedBotDeclineChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.SimulatedBotDeclineChance", 8);
        SimulatedBotDeclineMaxPerGroup = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.SimulatedBotDeclineMaxPerGroup", 2);
        SimulatedBotDeclineBlocksGroup = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.SimulatedBotDeclineBlocksGroup", false);
        OrganicLfgActing = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.OrganicLfgActing", true);
        OrganicLfgChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.OrganicLfgChance", 85);
        RestrictOrganicCitiesToClassic = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RestrictOrganicCitiesToClassic", true);
        StructuredLfgSimulation = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.StructuredLfgSimulation", true);
        StructuredLfgChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.StructuredLfgChance", 100);
        StructuredLfgTradeChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.StructuredLfgTradeChance", 20);
        AutoUseGmGroupSelection = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AutoUseGmGroupSelection", true);
        ReserveRealPlayerSlotChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.ReserveRealPlayerSlotChance", 0);
        PlayerWhisperJoin = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.PlayerWhisperJoin", true);
        PlayerWhisperJoinRequiresRole = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.PlayerWhisperJoinRequiresRole", true);
        PlayerWhisperJoinAllowGm = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.PlayerWhisperJoinAllowGm", true);
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
        if (MaxPendingXpDeliveriesPerLogin == 0)
            MaxPendingXpDeliveriesPerLogin = 1;
        XpPercentOfLevel = std::min<uint32>(XpPercentOfLevel, 100);
        XpMinimumPercentOnAnyKill = std::min<uint32>(XpMinimumPercentOnAnyKill, XpPercentOfLevel);
        if (XpLevelCap > 80)
            XpLevelCap = 80;
        RaidChanceAtCap = std::min<uint32>(RaidChanceAtCap, 100);
        GuildChatChance = std::min<uint32>(GuildChatChance, 100);
        GeneralChatChance = std::min<uint32>(GeneralChatChance, 100);
        LocalZoneChatChance = std::min<uint32>(LocalZoneChatChance, 100);
        SimulatedBotDeclineChance = std::min<uint32>(SimulatedBotDeclineChance, 100);
        SimulatedBotDeclineMaxPerGroup = std::min<uint32>(SimulatedBotDeclineMaxPerGroup, 5);
        OrganicLfgChance = std::min<uint32>(OrganicLfgChance, 100);
        StructuredLfgChance = std::min<uint32>(StructuredLfgChance, 100);
        StructuredLfgTradeChance = std::min<uint32>(StructuredLfgTradeChance, 100);
        ReserveRealPlayerSlotChance = std::min<uint32>(ReserveRealPlayerSlotChance, 100);
        TradeChatChance = std::min<uint32>(TradeChatChance, 100);
        if (TradeChatEveryTicks == 0)
            TradeChatEveryTicks = 1;
        if (ProgressiveTeleportStepSeconds == 0)
            ProgressiveTeleportStepSeconds = 1;
        if (InstanceWaypointStepSeconds == 0)
            InstanceWaypointStepSeconds = 1;
        if (HybridRealChance > 100)
            HybridRealChance = 100;
        if (NativeEventStepSeconds == 0)
            NativeEventStepSeconds = 1;
        if (NativeFollowerLeashSeconds == 0)
            NativeFollowerLeashSeconds = 1;
        if (NativeFollowerLeashDistance < 3.0f)
            NativeFollowerLeashDistance = 3.0f;
        if (NativeFollowerHardTeleportDistance > 0.0f && NativeFollowerHardTeleportDistance < NativeFollowerLeashDistance)
            NativeFollowerHardTeleportDistance = NativeFollowerLeashDistance;
        MinOnlineMembersForLiveRun = std::min<uint32>(MinOnlineMembersForLiveRun, 40);
        LiveDebugMaxRosterLines = std::min<uint32>(std::max<uint32>(LiveDebugMaxRosterLines, 1), 40);
        if (RaidMinServerLevelCap == 0)
            RaidMinServerLevelCap = 60;
        if (RaidLockoutDays == 0)
            RaidLockoutDays = 7;
        ProgressionPhase = std::min<uint32>(ProgressionPhase, 18);

        bool hasBotPrefix = !BotAccountPrefix.empty();
        bool hasBotRange = BotAccountMax >= BotAccountMin && BotAccountMax > 0;
        if (Enable && (!BotOnlyEligibilityFilter || (!hasBotPrefix && !hasBotRange)))
        {
            LOG_ERROR("module", "[PlayerbotDungeonSim] SAFETY: disabled. BotOnlyEligibilityFilter must be enabled with BotAccountPrefix or BotAccountMin/Max before DungeonSim can run.");
            Enable = false;
        }

        if (Enable && !VerifyDatabaseSchema())
            Enable = false;

        if (Enable)
            LOG_INFO("module", "[PlayerbotDungeonSim] RunMode={} HybridSimReal={} HybridRealChance={} SimMinimumBossKillsForLoot={} (SIM=timed simulation/loot, REAL=live group + native route/event executor).", RunModeText(ConfiguredRunMode), HybridSimReal ? 1 : 0, HybridRealChance, SimMinimumBossKillsForLoot);

        if (Enable && !AllowOnlineCharacterTouch)
            LOG_INFO("module", "[PlayerbotDungeonSim] Safe mode: online character touching is disabled. No teleports, live group edits, or direct loot delivery will occur.");
        else if (Enable)
            LOG_INFO("module", "[PlayerbotDungeonSim] Live mode: online bot character touching is enabled. Prefix/range safety filter is active; only configured bot accounts may be moved or receive direct loot.");

        if (Enable && ConfiguredRunMode == RUN_MODE_REAL && RequireOnlineBotsForRun)
            LOG_INFO("module", "[PlayerbotDungeonSim] REAL live-only mode: offline/fake dungeon starts are disabled; at least {} online/movable bot(s) required per run. cleanupOfflineRuns={} statusOnline={} preferOnlineCandidates={} liveDebugRoster={}", MinOnlineMembersForLiveRun, CleanupOfflineRunsWhenLiveOnly ? 1 : 0, StatusShowOnlineMembers ? 1 : 0, PreferOnlineCandidatesForLiveRuns ? 1 : 0, LiveDebugRoster ? 1 : 0);

        if (AuditFileLog)
            AuditLog("CONFIG", "File logging enabled path='" + AuditFilePath + "' movement=" + std::to_string(AuditFileMovement ? 1 : 0) +
                " loot=" + std::to_string(AuditFileLoot ? 1 : 0) + " members=" + std::to_string(AuditFileMembers ? 1 : 0) +
                " runMode=" + RunModeText(ConfiguredRunMode) + " hybrid=" + std::to_string(HybridSimReal ? 1 : 0) + " hybridRealChance=" + std::to_string(HybridRealChance) +
                " minLootBosses=" + std::to_string(SimMinimumBossKillsForLoot) + " awardXp=" + std::to_string(AwardXp ? 1 : 0) +
                " xpPercent=" + std::to_string(XpPercentOfLevel) + " xpCap=" + std::to_string(GetEffectiveXpLevelCap()) +
                " xpScaleByBosses=" + std::to_string(XpScaleByBossKills ? 1 : 0) + " enabled=" + std::to_string(Enable ? 1 : 0));

        _startupElapsedMs = 0;
        _startupDelayLogged = false;
    }

    void OnUpdate(uint32 diff) override
    {
        using namespace PlayerbotDungeonSim;
        if (!Enable)
            return;

        if (StartupDelaySeconds > 0 && _startupElapsedMs < StartupDelaySeconds * IN_MILLISECONDS)
        {
            _startupElapsedMs += diff;
            if (!_startupDelayLogged)
            {
                _startupDelayLogged = true;
                LOG_INFO("module", "[PlayerbotDungeonSim] Startup delay active for {} seconds so Playerbots can finish login/save activity.", StartupDelaySeconds);
            }
            return;
        }

        ProcessProgressiveTeleportQueue();
        ProcessNativeEventQueue();
        ProcessNativeFollowerLeashQueue();

        if (_timerMs <= diff)
        {
            _timerMs = TickSeconds * IN_MILLISECONDS;
            ExpireOldInvites();
            CleanupEmptyActiveRuns();
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
                PLAYERHOOK_CAN_PLAYER_USE_CHAT,
                PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT
            })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        PlayerbotDungeonSim::DeliverPendingLoot(player);
        PlayerbotDungeonSim::DeliverPendingXp(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg) override
    {
        (void)type;
        (void)language;

        // This core does not expose OnPlayerChat. It exposes OnPlayerCanUseChat,
        // so consume invite responses here before normal chat is sent.
        if (PlayerbotDungeonSim::HandleInviteChat(player, msg))
            return false;

        // Fallback parser for GM test commands on branches where command registration
        // does not catch dashed command names. CommandScript below is still the primary path.
        if (msg.rfind(".dng-sim", 0) == 0 && player && player->GetSession() && player->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
        {
            ChatHandler handler(player->GetSession());
            PlayerbotDungeonSim::HandleGmDngSimCommand(&handler, msg);
            return false;
        }

        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        (void)type;
        (void)language;

        if (PlayerbotDungeonSim::HandleBotWhisperJoin(player, receiver, msg))
            return false;

        return true;
    }
};


class PlayerbotDungeonSimCommandScript : public CommandScript
{
public:
    PlayerbotDungeonSimCommandScript() : CommandScript("PlayerbotDungeonSimCommandScript") { }

    std::vector<Acore::ChatCommands::ChatCommandBuilder> GetCommands() const override
    {
        using namespace Acore::ChatCommands;

        static std::vector<ChatCommandBuilder> commandTable =
        {
            { "dng-sim", HandleDngSimCommand, SEC_GAMEMASTER, Console::No }
        };
        return commandTable;
    }

    static bool HandleDngSimCommand(ChatHandler* handler, char const* args)
    {
        return PlayerbotDungeonSim::HandleGmDngSimCommand(handler, args ? args : "");
    }
};

void AddSC_mod_playerbot_dungeon_sim()
{
    new PlayerbotDungeonSimWorldScript();
    new PlayerbotDungeonSimPlayerScript();
    new PlayerbotDungeonSimCommandScript();
}

// AzerothCore module loader symbol.
// The generated ModulesLoader.cpp expects this exact name from the module folder:
// mod-playerbot-dungeon-sim -> Addmod_playerbot_dungeon_simScripts
void Addmod_playerbot_dungeon_simScripts()
{
    AddSC_mod_playerbot_dungeon_sim();
}
