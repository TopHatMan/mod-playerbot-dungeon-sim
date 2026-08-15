/*
 * mod-playerbot-dungeon-sim — autonomous rebuild.
 *
 * Replaces the old timed-simulation module. Instead of faking dungeon runs with
 * a dice roll on a timer, this version assembles a real, role-correct bot party,
 * teleports it inside the instance, and hands the run to the REAL dungeon-clear
 * AI (mod-dungeon-clear) via DungeonClearControl::StartAutonomousClear. The bots
 * actually fight through the dungeon; XP and loot come from real kills through
 * normal gameplay, so there is no award/loot/boss-credit subsystem here at all.
 *
 * Flow per run:
 *   FORM      gather online random bots, classify by real role (PlayerbotAI::
 *             IsTank/IsHeal), build {tank + healer + dps} (tank REQUIRED), make a
 *             real Group with the tank as leader.
 *   ENTERING  teleport the leader to the instance start (creates the instance),
 *             then teleport followers into the same instance copy, spread out.
 *             Once everyone is inside and alive, call StartAutonomousClear.
 *   CLEARING  poll the leader's "dungeon clear enabled" flag; dungeon clear drives
 *             the whole run. Done when the flag clears (full clear or wipe) or the
 *             safety timeout elapses.
 *   RETURNING send the bots back to their capital and disband the group (leaving
 *             the dungeon map auto-strips the dungeon-clear strategy via the gate).
 *
 * Requires mod-dungeon-clear with DungeonClear.AllowAutonomousBotRuns = 1, and
 * this module's CMake must add mod-dungeon-clear/src to its include dirs so
 * DungeonClearAutoStart.h resolves.
 */

#include "ScriptMgr.h"
#include "WorldScript.h"
#include "Config.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Map.h"
#include "DatabaseEnv.h"
#include "DisableMgr.h"
#include "ObjectMgr.h"
#include "ItemTemplate.h"
#include "Item.h"
#include "World.h"
#include "GameTime.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Log.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "WorldSession.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"
#include "Channel.h"
#include "ChannelMgr.h"

 // mod-dungeon-clear entry point. Forward-declared rather than #included so this
 // module needs no include path into mod-dungeon-clear — both compile into the
 // shared `modules` static library, so the linker resolves the symbol directly.
 // (Definition: mod-dungeon-clear/src/Ai/Dungeon/DungeonClear/Action/DungeonClearChatActions.cpp,
 //  declared in mod-dungeon-clear/src/DungeonClearAutoStart.h.)
namespace DungeonClearControl
{
    bool StartAutonomousClear(Player* anyGroupMember);
}

#include <algorithm>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <vector>

// Shared cross-module bot-activity reservation (header-only; see the header).
// mod-playerbot-world-pvp includes an identical copy, so the two mods coordinate
// through one shared registry with no link dependency between them.
#include "BotActivityRegistry.h"

using namespace Acore::ChatCommands;

namespace PBDSim
{
    // ---- config ----
    static bool     Enable = true;
    static bool     OnlyRandomBots = true;   // only ever touch sRandomPlayerbotMgr bots
    static uint32   TickSeconds = 30;     // how often we try to launch a new run
    static uint32   MaxConcurrentRuns = 3;
    static uint32   MinBotLevel = 15;
    static uint32   LevelBandWidth = 4;      // max level spread between tank and a groupmate
    static uint32   MinGroupSize = 3;      // need at least this many (incl. tank) to launch
    static float    EntranceSpread = 4.0f;   // yards of scatter at the instance start
    static uint32   EnterTimeoutSeconds = 60;    // give up assembling inside after this
    static uint32   MaxRunMinutes = 45;     // hard cap on a clear before we recall the party
    static uint32   StartupDelaySeconds = 120;   // let playerbots finish login churn first
    static bool     ReturnToCityOnEnd = true;

    // ---- progression / gearing ----
    static bool     AwardEnabled = true;   // guaranteed gearing on completion
    static uint32   AwardItemsPerRun = 1;      // best upgrades granted per bot per run
    static bool     AwardEquip = true;   // auto-equip awarded upgrades
    static uint32   AwardGoldCopper = 50000;  // 5g per completed run
    static uint32   ClearsToAdvanceTier = 8;     // completions before the next content tier
    static uint32   LegendaryChance = 2000;   // 1-in-N legendary roll per completed run (0 = off)
    static uint32   LegendaryMinTier = 4;      // must be this deep in the raid ladder to be eligible
    static uint32   LegendaryMinItemLevel = 70;  // gear gate: avg equipped item level required

    // ---- raids / sim mode ----
    static uint32   SimDurationMinutes = 30;     // offscreen raid "run" length before award
    static bool     RequireRealAttunement = false; // gate raids on the actual attune quest

    // ---- social / LFG ----
    static bool     AnnounceLfgChat = true;   // bots post in the LFG channel when forming
    static bool     AnnounceGeneralChat = true;  // ...and in their current zone General channel
    static bool     AnnounceTradeChat = false;  // ...and Trade (city only); off to avoid spam
    static bool     AllowPlayerJoin = true;   // real players may .dngsim join a forming run

    // ---- city staging (populate cities with LFG groups) ----
    static bool     StageInCity = true;   // all-bot 5-mans loiter in a city, advertising, before entering
    static uint32   StageSeconds = 40;     // how long they hang out in the city first

    // ---- data ----
    struct DungeonTemplate
    {
        uint32 id = 0;
        std::string name;
        uint32 mapId = 0;          // the instance map the party clears
        uint8 minLevel = 0;
        uint8 maxLevel = 0;
        uint8 groupSize = 5;
        float ex = 0.f, ey = 0.f, ez = 0.f, eo = 0.f;  // outdoor entrance (fallback only)
    };

    enum RunState : uint8
    {
        RUN_ENTERING = 0,
        RUN_CLEARING = 1,
        RUN_RETURNING = 2,
        RUN_SIMMING = 3,  // offscreen raid progression (no teleport / no dungeon clear)
        RUN_STAGING = 4   // all-bot 5-man loitering in a city, advertising LFG, before it heads in
    };

    enum RunMode : uint8
    {
        RUN_MODE_REAL = 0,  // 5-man: real party, teleport in, driven by dungeon clear
        RUN_MODE_SIM = 1   // raid: abstract offscreen run, timed, then tier award
    };

    struct Run
    {
        uint64 id = 0;
        uint32 dungeonId = 0;
        std::string name;
        uint32 mapId = 0;
        float sx = 0.f, sy = 0.f, sz = 0.f, so = 0.f;  // instance-start landing
        ObjectGuid leaderGuid;
        std::vector<ObjectGuid> members;
        uint8 state = RUN_ENTERING;
        uint8 mode = RUN_MODE_REAL;
        uint32 guildId = 0;      // non-zero if this is a guild run
        uint32 stateMs = 0;
        uint32 totalMs = 0;
        bool clearStarted = false;
        bool midAnnounced = false;   // re-advertised once mid-staging
    };

    static std::vector<Run> g_runs;
    static uint64 g_nextRunId = 1;

    // A live online candidate, classified by its REAL playerbots role.
    struct Cand
    {
        Player* player = nullptr;
        ObjectGuid guid;
        uint8 level = 0;
        uint8 team = 0;     // TeamId
        bool tank = false;
        bool heal = false;
    };

    // ----------------------------------------------------------------- helpers
    static bool InAnyRun(ObjectGuid guid)
    {
        for (Run const& r : g_runs)
            for (ObjectGuid const& g : r.members)
                if (g == guid)
                    return true;
        return false;
    }

    // A bot we may safely pull into a fresh run: alive, idle, ungrouped, not
    // already inside an instance, and one of our random bots.
    static bool IsBotFree(Player* p)
    {
        if (!p || !p->IsInWorld())
            return false;
        if (!p->IsAlive() || p->IsBeingTeleported() || p->IsInFlight())
            return false;
        if (p->IsInCombat())
            return false;
        if (p->GetGroup())
            return false;
        if (p->GetMap() && p->GetMap()->IsDungeon())
            return false;
        if (p->GetSession() && p->GetSession()->GetSecurity() > SEC_PLAYER)
            return false;
        if (InAnyRun(p->GetGUID()))
            return false;
        if (BotActivity::IsReserved(p->GetGUID().GetCounter()))
            return false;   // busy in a world-PvP event
        return true;
    }

    static bool RunHasRealPlayer(Run const& run)
    {
        for (ObjectGuid const& g : run.members)
        {
            Player* m = ObjectAccessor::FindPlayer(g);
            if (!m)
                continue;
            PlayerbotAI* ai = GET_PLAYERBOT_AI(m);
            if (!ai || ai->IsRealPlayer())
                return true;
        }
        return false;
    }

    static void GatherCandidates(std::vector<Cand>& out)
    {
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* p = pair.second;
            if (!p)
                continue;
            PlayerbotAI* ai = GET_PLAYERBOT_AI(p);
            if (!ai || ai->IsRealPlayer())
                continue;
            if (OnlyRandomBots && !sRandomPlayerbotMgr.IsRandomBot(p))
                continue;
            if (p->GetLevel() < MinBotLevel)
                continue;
            if (!IsBotFree(p))
                continue;

            Cand c;
            c.player = p;
            c.guid = p->GetGUID();
            c.level = p->GetLevel();
            c.team = uint8(p->GetTeamId());
            c.tank = PlayerbotAI::IsTank(p);
            c.heal = PlayerbotAI::IsHeal(p);
            out.push_back(c);
        }
    }

    static bool IsMapAvailable(uint32 mapId);  // defined below; used by LoadTemplates
    static uint32 MaxPlayerLevel();            // defined below; used by AdvanceProgression

    static std::vector<DungeonTemplate> LoadTemplates(uint8 level, bool isRaid, std::vector<uint32> const& mapFilter)
    {
        std::vector<DungeonTemplate> out;
        std::string q =
            "SELECT id, name, map_id, min_level, max_level, group_size, "
            "entrance_x, entrance_y, entrance_z, entrance_o "
            "FROM playerbot_dungeon_template WHERE is_raid = ";
        q += isRaid ? "1" : "0";
        q += " AND min_level <= " + std::to_string(uint32(level));
        q += " AND max_level >= " + std::to_string(uint32(level));
        if (!isRaid)
            q += " AND group_size <= 5";
        if (!mapFilter.empty())
        {
            q += " AND map_id IN (";
            for (size_t i = 0; i < mapFilter.size(); ++i)
            {
                if (i) q += ",";
                q += std::to_string(mapFilter[i]);
            }
            q += ")";
        }

        QueryResult res = WorldDatabase.Query(q.c_str());
        if (!res)
            return out;
        do
        {
            Field* f = res->Fetch();
            DungeonTemplate d;
            d.id = f[0].Get<uint32>();
            d.name = f[1].Get<std::string>();
            d.mapId = f[2].Get<uint16>();
            d.minLevel = f[3].Get<uint8>();
            d.maxLevel = f[4].Get<uint8>();
            d.groupSize = f[5].Get<uint8>();
            d.ex = f[6].Get<float>();
            d.ey = f[7].Get<float>();
            d.ez = f[8].Get<float>();
            d.eo = f[9].Get<float>();
            if (IsMapAvailable(d.mapId))          // honor the disables table
                out.push_back(d);
        } while (res->NextRow());
        return out;
    }

    // Resolve a landing spot INSIDE the instance map. Prefer the real instance
    // start from areatrigger_teleport (coords on the dungeon map); fall back to
    // the template's stored coords on the same map if none is registered.
    static void ResolveInstanceStart(DungeonTemplate const& d, float& x, float& y, float& z, float& o)
    {
        x = d.ex; y = d.ey; z = d.ez; o = d.eo;
        QueryResult at = WorldDatabase.Query(
            "SELECT target_position_x, target_position_y, target_position_z, target_orientation "
            "FROM areatrigger_teleport WHERE target_map = {} LIMIT 1",
            d.mapId);
        if (!at)
            return;
        Field* f = at->Fetch();
        x = f[0].Get<float>();
        y = f[1].Get<float>();
        z = f[2].Get<float>();
        o = f[3].Get<float>();
    }

    static bool OnInstanceMap(Player* p, uint32 mapId)
    {
        return p && p->GetMapId() == mapId && p->GetMap() && p->GetMap()->IsDungeon();
    }

    static bool SameInstance(Player* m, Player* leader)
    {
        return m->GetMapId() == leader->GetMapId() && m->GetInstanceId() == leader->GetInstanceId();
    }

    // True while the dungeon-clear run is still active. StartAutonomousClear
    // enables the clear on the tank FindLeaderTank elects, which may not be
    // run.leaderGuid, so we check EVERY member rather than one assumed leader.
    // The value lookup is null-checked: a bot whose context has no DC values
    // registered returns null from GetValue, and dereferencing that null was the
    // crash (mod_playerbot_dungeon_sim.cpp line 348).
    static bool ClearStillActive(Run const& run)
    {
        for (ObjectGuid const& g : run.members)
        {
            Player* m = ObjectAccessor::FindPlayer(g);
            if (!m || !m->IsInWorld())
                continue;
            PlayerbotAI* ai = GET_PLAYERBOT_AI(m);
            if (!ai)
                continue;
            AiObjectContext* ctx = ai->GetAiObjectContext();
            if (!ctx)
                continue;
            if (auto* v = ctx->GetValue<bool>("dungeon clear enabled"))
                if (v->Get())
                    return true;
        }
        return false;
    }

    // ----------------------------------------------------- disabled content
    // Honor the core `disables` table: never send a party to a disabled map
    // (e.g. on this realm Maraudon and Dire Maul are disabled).
    static bool IsMapAvailable(uint32 mapId)
    {
        return !DisableMgr::IsDisabledFor(DISABLE_TYPE_MAP, mapId, nullptr);
    }

    // ------------------------------------------------------ progression state
    // Per-bot ladder position. Level 60 framework first: tier 0 = the level-60
    // 5-man dungeons; higher tiers (raids, with attunements) slot in later. The
    // table carries the cap so 70/80 ladders extend the same row.
    struct Progression
    {
        uint8 levelCap = 60;
        uint32 tier = 0;
        uint32 clearsInTier = 0;
    };

    static uint8 CapForLevel(uint8 level)
    {
        if (level >= 80) return 80;
        if (level >= 70) return 70;
        return 60;
    }

    static Progression LoadProgression(uint32 guidLow, uint8 botLevel)
    {
        Progression p;
        p.levelCap = CapForLevel(botLevel);
        QueryResult r = CharacterDatabase.Query(
            "SELECT level_cap, tier, clears_in_tier FROM playerbot_dungeon_progression WHERE guid = {}",
            guidLow);
        if (r)
        {
            Field* f = r->Fetch();
            p.levelCap = f[0].Get<uint8>();
            p.tier = f[1].Get<uint32>();
            p.clearsInTier = f[2].Get<uint32>();
            // A bot that out-leveled its recorded cap moves up to the new cap at tier 0.
            uint8 const nowCap = CapForLevel(botLevel);
            if (nowCap > p.levelCap)
            {
                p.levelCap = nowCap;
                p.tier = 0;
                p.clearsInTier = 0;
            }
        }
        return p;
    }

    static void SaveProgression(uint32 guidLow, Progression const& p)
    {
        CharacterDatabase.Execute(
            "REPLACE INTO playerbot_dungeon_progression (guid, level_cap, tier, clears_in_tier, updated) "
            "VALUES ({}, {}, {}, {}, {})",
            guidLow, uint32(p.levelCap), p.tier, p.clearsInTier, uint32(GameTime::GetGameTime().count()));
    }

    static void AdvanceProgression(uint32 guidLow, uint8 botLevel)
    {
        Progression p = LoadProgression(guidLow, botLevel);
        ++p.clearsInTier;
        if (p.clearsInTier >= ClearsToAdvanceTier)
        {
            // The raid ladder is endgame: only climb past the dungeon tier once the
            // bot is actually at max level. A leveling bot keeps banking dungeon
            // clears but won't phantom-advance into raids it can't do yet, so its
            // raid progression genuinely begins at cap (and takes time from there).
            if (botLevel >= MaxPlayerLevel())
            {
                ++p.tier;
                p.clearsInTier = 0;
            }
            else
            {
                p.clearsInTier = ClearsToAdvanceTier;  // hold at the gate until capped
            }
        }
        SaveProgression(guidLow, p);
    }

    // -------------------------------------------------------------- loot/award
    static std::vector<uint32> LoadDungeonBossEntries(uint32 dungeonId)
    {
        std::vector<uint32> out;
        QueryResult r = WorldDatabase.Query(
            "SELECT creature_entry FROM playerbot_dungeon_boss_template "
            "WHERE dungeon_template_id = {} AND creature_entry > 0 ORDER BY boss_order",
            dungeonId);
        if (!r)
            return out;
        do { out.push_back(r->Fetch()[0].Get<uint32>()); } while (r->NextRow());
        return out;
    }

    // Pull a boss's real drop list from the world DB. Resolves creature_template
    // .lootid first (the canonical loot key), falling back to the entry itself.
    static std::vector<uint32> LoadBossLoot(uint32 creatureEntry)
    {
        std::vector<uint32> items;
        if (!creatureEntry)
            return items;

        uint32 lootId = creatureEntry;
        if (QueryResult lr = WorldDatabase.Query(
            "SELECT lootid FROM creature_template WHERE entry = {}", creatureEntry))
        {
            uint32 const lid = lr->Fetch()[0].Get<uint32>();
            if (lid)
                lootId = lid;
        }

        QueryResult r = WorldDatabase.Query(
            "SELECT Item FROM creature_loot_template WHERE Entry = {} AND Item > 0 LIMIT 200",
            lootId);
        if (!r)
            return items;
        do { items.push_back(r->Fetch()[0].Get<uint32>()); } while (r->NextRow());
        return items;
    }

    static bool ClassAllowed(ItemTemplate const* p, uint8 cls)
    {
        if (p->AllowableClass == -1)
            return true;
        return (uint32(p->AllowableClass) & (1u << (cls - 1))) != 0;
    }

    static bool RaceAllowed(ItemTemplate const* p, uint8 race)
    {
        if (p->AllowableRace == -1)
            return true;
        return (uint32(p->AllowableRace) & (1u << (race - 1))) != 0;
    }

    static void EquipSlotsFor(uint8 invType, std::vector<uint8>& slots)
    {
        switch (invType)
        {
        case INVTYPE_HEAD:           slots = { EQUIPMENT_SLOT_HEAD }; break;
        case INVTYPE_NECK:           slots = { EQUIPMENT_SLOT_NECK }; break;
        case INVTYPE_SHOULDERS:      slots = { EQUIPMENT_SLOT_SHOULDERS }; break;
        case INVTYPE_CLOAK:          slots = { EQUIPMENT_SLOT_BACK }; break;
        case INVTYPE_CHEST:
        case INVTYPE_ROBE:           slots = { EQUIPMENT_SLOT_CHEST }; break;
        case INVTYPE_WRISTS:         slots = { EQUIPMENT_SLOT_WRISTS }; break;
        case INVTYPE_HANDS:          slots = { EQUIPMENT_SLOT_HANDS }; break;
        case INVTYPE_WAIST:          slots = { EQUIPMENT_SLOT_WAIST }; break;
        case INVTYPE_LEGS:           slots = { EQUIPMENT_SLOT_LEGS }; break;
        case INVTYPE_FEET:           slots = { EQUIPMENT_SLOT_FEET }; break;
        case INVTYPE_FINGER:         slots = { EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2 }; break;
        case INVTYPE_TRINKET:        slots = { EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 }; break;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND:
        case INVTYPE_2HWEAPON:       slots = { EQUIPMENT_SLOT_MAINHAND }; break;
        case INVTYPE_WEAPONOFFHAND:
        case INVTYPE_SHIELD:
        case INVTYPE_HOLDABLE:       slots = { EQUIPMENT_SLOT_OFFHAND }; break;
        case INVTYPE_RANGED:
        case INVTYPE_THROWN:
        case INVTYPE_RANGEDRIGHT:    slots = { EQUIPMENT_SLOT_RANGED }; break;
        default: break;
        }
    }

    static bool ItemUsableBy(Player* bot, ItemTemplate const* p)
    {
        if (!p)
            return false;
        if (p->Class != ITEM_CLASS_ARMOR && p->Class != ITEM_CLASS_WEAPON)
            return false;                       // gear only
        if (p->RequiredLevel && bot->GetLevel() < p->RequiredLevel)
            return false;
        if (!ClassAllowed(p, bot->getClass()) || !RaceAllowed(p, bot->getRace()))
            return false;
        std::vector<uint8> slots;
        EquipSlotsFor(p->InventoryType, slots);
        return !slots.empty();
    }

    // An item is worth awarding if a target slot is empty or holds a lower item
    // level — keeps gearing gradual (greens -> dungeon blues -> tier, slot by slot).
    static bool ItemIsUpgrade(Player* bot, ItemTemplate const* p)
    {
        std::vector<uint8> slots;
        EquipSlotsFor(p->InventoryType, slots);
        for (uint8 s : slots)
        {
            Item* cur = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, s);
            if (!cur)
                return true;
            if (ItemTemplate const* cp = cur->GetTemplate())
                if (p->ItemLevel > cp->ItemLevel)
                    return true;
        }
        return false;
    }

    static bool GiveItem(Player* bot, uint32 itemId)
    {
        ItemPosCountVec dest;
        if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, 1) != EQUIP_ERR_OK)
            return false;
        Item* item = bot->StoreNewItem(dest, itemId, true);
        if (!item)
            return false;
        bot->SendNewItem(item, 1, true, false);
        if (AwardEquip)
        {
            uint16 eDest = 0;
            if (bot->CanEquipItem(NULL_SLOT, eDest, item, false) == EQUIP_ERR_OK)
            {
                bot->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
                bot->EquipItem(eDest, item, true);
            }
        }
        return true;
    }

    static uint32 MaxPlayerLevel()
    {
        return sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    }

    // Average item level across equipped gear — the "gear gate" measure.
    static uint32 AverageEquipItemLevel(Player* bot)
    {
        uint32 sum = 0, count = 0;
        for (uint8 s = EQUIPMENT_SLOT_START; s < EQUIPMENT_SLOT_END; ++s)
        {
            Item* it = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, s);
            if (!it)
                continue;
            if (ItemTemplate const* p = it->GetTemplate())
            {
                sum += p->ItemLevel;
                ++count;
            }
        }
        return count ? (sum / count) : 0;
    }

    // Legendaries simulate the long tail of guild raid progression: only a fully
    // leveled, well-geared bot that has climbed deep into the raid ladder is
    // eligible, and then only on the tiny per-run roll. So they surface slowly,
    // across the population, over many raid clears — never to a leveling bot.
    static uint32 RollLegendary(Player* bot)
    {
        if (!LegendaryChance || !bot)
            return 0;
        if (bot->GetLevel() < MaxPlayerLevel())                      // level gate
            return 0;
        Progression const prog = LoadProgression(bot->GetGUID().GetCounter(), bot->GetLevel());
        if (prog.tier < LegendaryMinTier)                            // raid-progression gate
            return 0;
        if (AverageEquipItemLevel(bot) < LegendaryMinItemLevel)      // gear gate
            return 0;
        if (urand(1, LegendaryChance) != 1)                         // the slow roll
            return 0;

        uint8 const cap = CapForLevel(bot->GetLevel());
        static std::vector<uint32> const lego60 = { 19019, 17182 };               // Thunderfury, Sulfuras
        static std::vector<uint32> const lego70 = { 32837, 32838 };               // Warglaives (MH/OH)
        static std::vector<uint32> const lego80 = { 49623 };                      // Shadowmourne
        std::vector<uint32> const& pool = cap >= 80 ? lego80 : cap >= 70 ? lego70 : lego60;
        if (pool.empty())
            return 0;
        return pool[urand(0, uint32(pool.size() - 1))];
    }

    // Guaranteed gearing on a real completion, independent of whether live looting
    // worked. Pulls the dungeon's boss drops from the DB, gives each bot its best
    // few slot upgrades, plus gold, a tiny legendary roll, and a tier credit.
    static void AwardRun(Run const& run)
    {
        if (!AwardEnabled)
            return;

        std::vector<uint32> bosses = LoadDungeonBossEntries(run.dungeonId);
        std::vector<uint32> pool;
        for (uint32 e : bosses)
        {
            std::vector<uint32> drops = LoadBossLoot(e);
            pool.insert(pool.end(), drops.begin(), drops.end());
        }
        std::sort(pool.begin(), pool.end());
        pool.erase(std::unique(pool.begin(), pool.end()), pool.end());

        for (ObjectGuid const& g : run.members)
        {
            Player* m = ObjectAccessor::FindPlayer(g);
            if (!m || !m->IsInWorld())
                continue;

            // Award up to N best distinct upgrades from this dungeon's drops.
            std::unordered_set<uint32> awarded;
            for (uint32 n = 0; n < AwardItemsPerRun; ++n)
            {
                ItemTemplate const* best = nullptr;
                for (uint32 id : pool)
                {
                    if (awarded.count(id))
                        continue;
                    ItemTemplate const* p = sObjectMgr->GetItemTemplate(id);
                    if (!p || !ItemUsableBy(m, p) || !ItemIsUpgrade(m, p))
                        continue;
                    if (!best || p->ItemLevel > best->ItemLevel)
                        best = p;
                }
                if (!best)
                    break;
                if (GiveItem(m, best->ItemId))
                    awarded.insert(best->ItemId);
            }

            if (uint32 lego = RollLegendary(m))
                GiveItem(m, lego);

            if (AwardGoldCopper)
                m->ModifyMoney(int32(AwardGoldCopper));

            AdvanceProgression(m->GetGUID().GetCounter(), m->GetLevel());
        }

        LOG_INFO("module", "[DungeonSim] Run #{} {}: awarded boss loot + progression to party.",
            run.id, run.name);
    }

    static void ReserveRunBots(Run const& run)
    {
        for (ObjectGuid const& g : run.members)
            BotActivity::Reserve(g.GetCounter());
    }

    static void ReleaseRunBots(Run const& run)
    {
        for (ObjectGuid const& g : run.members)
            BotActivity::Release(g.GetCounter());
    }

    static void ReturnAndDisband(Run& run)
    {
        ReleaseRunBots(run);   // free these bots for other activities (incl. world PvP)
        Group* group = nullptr;
        for (ObjectGuid const& g : run.members)
        {
            Player* m = ObjectAccessor::FindPlayer(g);
            if (!m)
                continue;
            if (!group && m->GetGroup())
                group = m->GetGroup();
            if (ReturnToCityOnEnd && run.mode == RUN_MODE_REAL && m->IsInWorld() && !m->IsBeingTeleported())
            {
                if (m->GetTeamId() == TEAM_ALLIANCE)
                    m->TeleportTo(0, -8833.38f, 628.62f, 94.00f, 0.50f);   // Stormwind
                else
                    m->TeleportTo(1, 1568.00f, -4400.00f, 8.40f, 4.00f);   // Orgrimmar
            }
        }
        if (group)
            group->Disband();
    }

    // -------------------------------------------------------------- formation
    // ------------------------------------------------------- content ladder
    struct ContentStage { bool isRaid; std::vector<uint32> mapIds; uint32 attuneQuest; char const* label; };

    static std::vector<ContentStage> const& Ladder(uint8 cap)
    {
        // Level-60 ladder: 5-man dungeons, then the classic raid progression.
        // Raids run in SIM mode (dungeon clear can't drive 20/40-mans); the tier
        // itself is the progression gate ("earn your way up"), with an optional
        // real-attunement check layered on top.
        static std::vector<ContentStage> const l60 = {
            { false, {},          0,    "Level 60 Dungeons" },
            { true,  {409},       0,    "Molten Core" },
            { true,  {249},       0,    "Onyxia's Lair" },
            { true,  {469},       7761, "Blackwing Lair" },
            { true,  {309, 509},  0,    "Zul'Gurub / Ruins of AQ" },
            { true,  {531},       0,    "Temple of Ahn'Qiraj" },
            { true,  {533},       0,    "Naxxramas" },
        };
        // 70/80 raid ladders slot in here next; for now higher caps run their
        // max-level 5-mans (heroics are is_raid = 0, so they flow through here).
        static std::vector<ContentStage> const lDungeonOnly = {
            { false, {}, 0, "Max-level Dungeons" },
        };
        return cap >= 70 ? lDungeonOnly : l60;
    }

    // Choose the content this bot attempts now: its tier's stage, honoring the
    // disables table (inside LoadTemplates) and optional real attunement. Falls
    // back to dungeons rather than stalling. False if nothing is runnable.
    // Per-dungeon faction bias: Alliance weight 0..100 (Horde weight = 100 - it).
    // 0 means that faction is never routed there. Matches the classic geography:
    // RFC is Horde-only, the Barrens/Silverpine dungeons lean Horde, Westfall's
    // Deadmines leans Alliance; everything else is normalized 50/50. (Code table
    // for now — easy to move to a DB column later.)
    static uint32 AllianceWeight(uint32 mapId)
    {
        switch (mapId)
        {
        case 389: return 0;    // Ragefire Chasm  — Horde only
        case 43:  return 15;   // Wailing Caverns — Horde-favored
        case 33:  return 25;   // Shadowfang Keep — Horde-favored
        case 34:  return 100;  // Stormwind Stockade — Alliance only
        case 36:  return 85;   // Deadmines        — Alliance-favored
        default:  return 50;   // normalized
        }
    }

    static uint32 TeamWeight(uint32 mapId, uint8 team)
    {
        uint32 const a = AllianceWeight(mapId);
        return team == uint8(TEAM_ALLIANCE) ? a : (100 - a);
    }

    // Weighted pick honoring faction bias; entries with weight 0 are excluded
    // (so Alliance never rolls RFC, Horde never rolls the Stockade, etc.).
    static bool PickDungeonByFaction(std::vector<DungeonTemplate> const& pool, uint8 team, DungeonTemplate& out)
    {
        uint32 total = 0;
        for (DungeonTemplate const& d : pool)
            total += TeamWeight(d.mapId, team);
        if (total == 0)
            return false;
        uint32 roll = urand(1, total);
        for (DungeonTemplate const& d : pool)
        {
            uint32 const w = TeamWeight(d.mapId, team);
            if (w == 0)
                continue;
            if (roll <= w)
            {
                out = d;
                return true;
            }
            roll -= w;
        }
        out = pool.back();
        return true;
    }

    static bool SelectContentForBot(Player* tank, DungeonTemplate& out, bool& isRaid)
    {
        Progression const prog = LoadProgression(tank->GetGUID().GetCounter(), tank->GetLevel());
        std::vector<ContentStage> const& ladder = Ladder(prog.levelCap);
        if (ladder.empty())
            return false;
        size_t const idx = prog.tier < ladder.size() ? size_t(prog.tier) : ladder.size() - 1;
        ContentStage stage = ladder[idx];

        if (stage.isRaid && stage.attuneQuest && RequireRealAttunement &&
            !tank->GetQuestRewardStatus(stage.attuneQuest))
            stage = ladder.front();   // not attuned -> keep running dungeons

        std::vector<DungeonTemplate> pool = LoadTemplates(tank->GetLevel(), stage.isRaid, stage.mapIds);
        isRaid = stage.isRaid;
        if (pool.empty() && stage.isRaid)
        {
            pool = LoadTemplates(tank->GetLevel(), false, {});   // fall back to dungeons
            isRaid = false;
        }
        if (pool.empty())
            return false;

        if (!isRaid)
        {
            // Route by faction bias for 5-mans (RFC Horde-only, DM Alliance-favored,
            // SFK/WC Horde-favored, rest 50/50). If nothing is faction-appropriate,
            // no dungeon this pick.
            return PickDungeonByFaction(pool, tank->GetTeamId(), out);
        }
        out = pool[urand(0, uint32(pool.size() - 1))];   // raids: both factions run them
        return true;
    }

    // ------------------------------------------------------- social / guild
    // -------------------------------------------------------- city staging
    struct CityLoc { uint32 mapId; float x, y, z, o; };
    static std::vector<CityLoc> g_allianceCities;
    static std::vector<CityLoc> g_hordeCities;
    static bool g_citiesLoaded = false;

    // Pull known-good capital coordinates from the server's own game_tele table
    // rather than hardcoding risky Z values. Vanilla-only capitals — no Exodar or
    // Silvermoon on a progression realm.
    static void LoadCityInto(std::vector<char const*> const& aliases, std::vector<CityLoc>& out)
    {
        for (char const* name : aliases)
        {
            QueryResult r = WorldDatabase.Query(
                "SELECT map, position_x, position_y, position_z, orientation FROM game_tele WHERE name = '{}' LIMIT 1",
                name);
            if (!r)
                continue;
            Field* f = r->Fetch();
            CityLoc c;
            c.mapId = f[0].Get<uint16>();
            c.x = f[1].Get<float>();
            c.y = f[2].Get<float>();
            c.z = f[3].Get<float>();
            c.o = f[4].Get<float>();
            out.push_back(c);
            return;
        }
        LOG_WARN("module", "[DungeonSim] No game_tele entry for city '{}' — staging will skip it.", aliases.front());
    }

    static void LoadCities()
    {
        g_allianceCities.clear();
        g_hordeCities.clear();
        LoadCityInto({ "stormwind" }, g_allianceCities);
        LoadCityInto({ "ironforge" }, g_allianceCities);
        LoadCityInto({ "darnassus" }, g_allianceCities);
        LoadCityInto({ "orgrimmar" }, g_hordeCities);
        LoadCityInto({ "thunderbluff", "thunder bluff" }, g_hordeCities);
        LoadCityInto({ "undercity" }, g_hordeCities);
        g_citiesLoaded = true;
        LOG_INFO("module", "[DungeonSim] Loaded {} Alliance / {} Horde city stage points.",
            uint32(g_allianceCities.size()), uint32(g_hordeCities.size()));
    }

    static bool StageGroupInCity(Run const& run, uint8 team)
    {
        std::vector<CityLoc> const& cities = (team == uint8(TEAM_ALLIANCE)) ? g_allianceCities : g_hordeCities;
        if (cities.empty())
            return false;
        CityLoc const& c = cities[urand(0, uint32(cities.size() - 1))];
        for (ObjectGuid const& g : run.members)
        {
            Player* m = ObjectAccessor::FindPlayer(g);
            if (!m || !m->IsInWorld() || m->IsBeingTeleported())
                continue;
            float const ox = c.x + frand(-6.0f, 6.0f);
            float const oy = c.y + frand(-6.0f, 6.0f);
            m->TeleportTo(c.mapId, ox, oy, c.z, c.o);
        }
        return true;
    }

    static uint32 ComputeGroupGuild(std::vector<ObjectGuid> const& members, ObjectGuid leaderGuid)
    {
        Player* leader = ObjectAccessor::FindPlayer(leaderGuid);
        if (!leader)
            return 0;
        uint32 const gid = leader->GetGuildId();
        if (!gid)
            return 0;
        uint32 shared = 0, total = 0;
        for (ObjectGuid const& g : members)
            if (Player* m = ObjectAccessor::FindPlayer(g))
            {
                ++total;
                if (m->GetGuildId() == gid)
                    ++shared;
            }
        return (total > 0 && shared * 2 >= total) ? gid : 0;   // majority share = guild run
    }

    // Post to a channel the bot already belongs to, by id, WITHOUT playerbots'
    // SayToChannel zone filter (which never matches Trade — the channel is
    // "Trade - City" but the bot's zone is "Orgrimmar"/etc., so every Trade post
    // is silently dropped). Bots join "Trade - City" and the LFG channel at login.
    static bool SayInChannel(Player* bot, uint32 chanId, std::string const& msg)
    {
        if (!bot || msg.empty())
            return false;
        ChannelMgr* cMgr = ChannelMgr::forTeam(bot->GetTeamId());
        if (!cMgr)
            return false;
        for (auto const& kv : cMgr->GetChannels())
        {
            Channel* channel = kv.second;
            if (!channel || channel->GetChannelId() != chanId || channel->GetName().empty())
                continue;
            // Channel::Say drops the message if the sender isn't a member. IsOn()
            // is private, but JoinChannel() is a safe no-op when already joined, so
            // just call it to guarantee membership before speaking.
            channel->JoinChannel(bot, "");
            channel->Say(bot->GetGUID(), msg.c_str(), LANG_UNIVERSAL);
            return true;
        }
        return false;
    }

    static void AnnounceLfg(Player* leader, std::string const& content, bool guildRun, uint32 have, uint32 need)
    {
        if (!AnnounceLfgChat || !leader)
            return;
        std::string msg;
        if (guildRun)
            msg = "[Guild] " + content + " forming up - guildies welcome!";
        else if (have < need)
            msg = "LFG " + content + " " + std::to_string(have) + "/" + std::to_string(need) + ", whisper for invite";
        else
            msg = "LFM " + content + " - one more welcome";

        // Global LFG channel always; plus the faction-wide "Trade - City" channel
        // and the bot's zone General when enabled. Uses the membership the bot got
        // at login and bypasses the broken zone filter, so these actually post.
        SayInChannel(leader, ChatChannelId::LOOKING_FOR_GROUP, msg);
        if (AnnounceGeneralChat)
            SayInChannel(leader, ChatChannelId::GENERAL, msg);
        if (AnnounceTradeChat)
            SayInChannel(leader, ChatChannelId::TRADE, msg);
    }

    // Shared roster: tank + one healer + fill to `need`, same team within band.
    static std::vector<Cand*> BuildRoster(std::vector<Cand>& cands, Cand* tank, uint8 need)
    {
        std::vector<Cand*> pool;
        for (Cand& c : cands)
        {
            if (&c == tank || c.team != tank->team)
                continue;
            if (uint32(std::abs(int(c.level) - int(tank->level))) > LevelBandWidth)
                continue;
            pool.push_back(&c);
        }
        std::vector<Cand*> chosen;
        chosen.push_back(tank);
        for (Cand* c : pool)
        {
            if (chosen.size() >= need) break;
            if (c->heal) { chosen.push_back(c); break; }
        }
        for (Cand* c : pool)
        {
            if (chosen.size() >= need) break;
            if (std::find(chosen.begin(), chosen.end(), c) == chosen.end())
                chosen.push_back(c);
        }
        return chosen;
    }

    // -------------------------------------------------------- real 5-man run
    static bool FormRealRun(std::vector<Cand*> const& chosen, DungeonTemplate const& dungeon)
    {
        Player* leader = chosen.front()->player;
        if (!leader || !leader->IsInWorld() || leader->GetGroup())
            return false;

        Group* group = new Group();
        if (!group->Create(leader))
        {
            delete group;
            return false;
        }
        sGroupMgr->AddGroup(group);

        Run run;
        run.id = g_nextRunId++;
        run.mode = RUN_MODE_REAL;
        run.dungeonId = dungeon.id;
        run.name = dungeon.name;
        run.mapId = dungeon.mapId;
        run.leaderGuid = leader->GetGUID();
        run.members.push_back(leader->GetGUID());

        for (Cand* c : chosen)
        {
            if (c->player == leader)
                continue;
            if (!c->player || !c->player->IsInWorld() || c->player->GetGroup())
                continue;
            if (group->AddMember(c->player))
                run.members.push_back(c->player->GetGUID());
        }

        if (run.members.size() < MinGroupSize)
        {
            group->Disband();
            return false;
        }

        run.guildId = ComputeGroupGuild(run.members, run.leaderGuid);
        ResolveInstanceStart(dungeon, run.sx, run.sy, run.sz, run.so);

        // All-bot group: loiter in a random faction capital first (advertising
        // LFG), so cities look populated. Player-led groups skip staging and go
        // straight in. Falls back to ENTERING if no city stage points loaded.
        bool const botOnly = !RunHasRealPlayer(run);
        if (StageInCity && botOnly && StageGroupInCity(run, leader->GetTeamId()))
            run.state = RUN_STAGING;
        else
            run.state = RUN_ENTERING;

        g_runs.push_back(run);

        ReserveRunBots(run);
        AnnounceLfg(leader, run.name, run.guildId != 0, uint32(run.members.size()), 5);
        LOG_INFO("module", "[DungeonSim] Real run #{} {} (map {}) - {} bots{}{}.",
            run.id, run.name, run.mapId, uint32(run.members.size()),
            run.guildId ? " [guild]" : "", run.state == RUN_STAGING ? " [staging in city]" : "");
        return true;
    }

    // ----------------------------------------------------- offscreen raid sim
    static bool FormSimRun(std::vector<Cand*> const& chosen, DungeonTemplate const& raid)
    {
        Player* leader = chosen.front()->player;
        if (!leader || !leader->IsInWorld())
            return false;

        Run run;
        run.id = g_nextRunId++;
        run.mode = RUN_MODE_SIM;
        run.dungeonId = raid.id;
        run.name = raid.name;
        run.mapId = raid.mapId;
        run.leaderGuid = leader->GetGUID();
        for (Cand* c : chosen)
            if (c->player && c->player->IsInWorld())
                run.members.push_back(c->player->GetGUID());

        if (run.members.empty())
            return false;

        run.guildId = ComputeGroupGuild(run.members, run.leaderGuid);
        run.state = RUN_SIMMING;
        g_runs.push_back(run);

        ReserveRunBots(run);
        AnnounceLfg(leader, run.name, run.guildId != 0, uint32(run.members.size()), 40);
        LOG_INFO("module", "[DungeonSim] Sim raid #{} {} - {} bots{}.",
            run.id, run.name, uint32(run.members.size()), run.guildId ? " [guild]" : "");
        return true;
    }

    static bool TryFormRun()
    {
        if (StageInCity && !g_citiesLoaded)
            LoadCities();   // DB is available by the time runs start forming

        std::vector<Cand> cands;
        GatherCandidates(cands);
        if (cands.empty())
            return false;

        std::vector<Cand*> tanks;
        for (Cand& c : cands)
            if (c.tank)
                tanks.push_back(&c);
        if (tanks.empty())
            return false;

        Cand* tank = tanks[urand(0, uint32(tanks.size() - 1))];

        DungeonTemplate content;
        bool isRaid = false;
        if (!SelectContentForBot(tank->player, content, isRaid))
            return false;

        uint8 need = content.groupSize ? content.groupSize : uint8(5);

        std::vector<Cand*> chosen = BuildRoster(cands, tank, need);

        if (isRaid)
        {
            // Raids (20/40-man) run in sim: gather what of the "guild" is online,
            // down to MinGroupSize. No teleport, so a partial roster is fine.
            if (chosen.size() < MinGroupSize)
                return false;
            return FormSimRun(chosen, content);
        }

        // Dungeons must be a full group to go in (your rule). If we can't field a
        // complete party right now, wait for more bots rather than sending a partial.
        if (chosen.size() < need)
            return false;
        return FormRealRun(chosen, content);
    }

    // ------------------------------------------------------------ run advance
    static uint32 CountLivingMembers(Run const& run, Player* leader, uint32& insideAndAlive)
    {
        insideAndAlive = 0;
        uint32 present = 0;
        for (ObjectGuid const& g : run.members)
        {
            Player* m = ObjectAccessor::FindPlayer(g);
            if (!m || !m->IsInWorld())
                continue;
            ++present;
            if (m->IsAlive() && (m == leader || SameInstance(m, leader)) && OnInstanceMap(m, run.mapId))
                ++insideAndAlive;
        }
        return present;
    }

    static void AdvanceRun(Run& run, uint32 diff)
    {
        run.stateMs += diff;
        run.totalMs += diff;

        Player* leader = ObjectAccessor::FindPlayer(run.leaderGuid);
        if (!leader || !leader->IsInWorld())
        {
            // Leader vanished (logout). Recall whoever is left and tear down.
            run.state = RUN_RETURNING;
        }

        switch (run.state)
        {
        case RUN_STAGING:
        {
            // Loiter in the city, advertising, then head to the dungeon. One
            // extra LFG shout partway through so passers-by see them looking.
            if (!run.midAnnounced && run.stateMs > (StageSeconds / 2) * IN_MILLISECONDS)
            {
                AnnounceLfg(leader, run.name, run.guildId != 0, uint32(run.members.size()), 5);
                run.midAnnounced = true;
            }
            if (run.stateMs > StageSeconds * IN_MILLISECONDS)
            {
                run.state = RUN_ENTERING;
                run.stateMs = 0;
            }
            break;
        }
        case RUN_ENTERING:
        {
            // Only all-bot groups get force-teleported in. If a real player
            // joined, the party enters the normal way (player walks/summons,
            // bots follow) — we just wait for everyone to be inside.
            bool const botOnly = !RunHasRealPlayer(run);

            // Leader enters first so the instance exists for the rest.
            if (!OnInstanceMap(leader, run.mapId))
            {
                if (botOnly && !leader->IsBeingTeleported() && !leader->IsInCombat())
                    leader->TeleportTo(run.mapId, run.sx, run.sy, run.sz, run.so);
                break;
            }

            // Bring in any follower not yet in the leader's instance copy.
            if (botOnly)
            {
                for (ObjectGuid const& g : run.members)
                {
                    if (g == run.leaderGuid)
                        continue;
                    Player* m = ObjectAccessor::FindPlayer(g);
                    if (!m || !m->IsInWorld() || m->IsBeingTeleported())
                        continue;
                    if (OnInstanceMap(m, run.mapId) && SameInstance(m, leader))
                        continue;
                    float const ox = run.sx + frand(-EntranceSpread, EntranceSpread);
                    float const oy = run.sy + frand(-EntranceSpread, EntranceSpread);
                    m->TeleportTo(run.mapId, ox, oy, run.sz, run.so);
                }
            }

            uint32 insideAlive = 0;
            uint32 const present = CountLivingMembers(run, leader, insideAlive);

            bool const everyoneIn = present > 0 && insideAlive >= present;
            bool const timedOut = run.stateMs > EnterTimeoutSeconds * IN_MILLISECONDS;

            // The leader must be FULLY settled on the instance before we hand
            // off to the dungeon-clear driver. OnInstanceMap can already be
            // true during the final phase of a teleport (map assigned, player
            // not yet in-world), and StartAutonomousClear -> EnableClearOnLeader
            // computes NextDungeonBoss against that half-loaded instance, which
            // returns a garbage optional and crashes on the announce. Waiting a
            // tick for IsInWorld() && !IsBeingTeleported() closes the race.
            bool const leaderReady =
                leader && leader->IsInWorld() && !leader->IsBeingTeleported();

            if (leaderReady && (everyoneIn || (timedOut && insideAlive >= MinGroupSize)))
            {
                if (DungeonClearControl::StartAutonomousClear(leader))
                {
                    run.clearStarted = true;
                    run.state = RUN_CLEARING;
                    run.stateMs = 0;
                    LOG_INFO("module", "[DungeonSim] Run #{} {}: clear started ({} inside).",
                        run.id, run.name, insideAlive);
                }
                else if (timedOut)
                {
                    LOG_WARN("module", "[DungeonSim] Run #{} {}: could not start clear (no elected tank?). Recalling.",
                        run.id, run.name);
                    run.state = RUN_RETURNING;
                    run.stateMs = 0;
                }
            }
            else if (timedOut)
            {
                LOG_WARN("module", "[DungeonSim] Run #{} {}: only {} bot(s) made it inside; recalling.",
                    run.id, run.name, insideAlive);
                run.state = RUN_RETURNING;
                run.stateMs = 0;
            }
            break;
        }
        case RUN_CLEARING:
        {
            bool const active = ClearStillActive(run);
            bool const timedOut = run.stateMs > MaxRunMinutes * MINUTE * IN_MILLISECONDS;
            if (!active || timedOut)
            {
                // The clear actually ran (live combat happened); guarantee the
                // party's gearing/progression regardless of how live looting went.
                if (run.clearStarted)
                    AwardRun(run);
                LOG_INFO("module", "[DungeonSim] Run #{} {} finished ({}).",
                    run.id, run.name, timedOut ? "timeout" : "clear ended");
                run.state = RUN_RETURNING;
                run.stateMs = 0;
            }
            break;
        }
        case RUN_SIMMING:
        {
            // Offscreen raid progression: no teleport, no dungeon clear. After
            // the sim duration, award the raid's real boss loot + tier credit.
            if (run.stateMs > SimDurationMinutes * MINUTE * IN_MILLISECONDS)
            {
                AwardRun(run);
                LOG_INFO("module", "[DungeonSim] Sim raid #{} {} completed; awarded.",
                    run.id, run.name);
                run.state = RUN_RETURNING;
                run.stateMs = 0;
            }
            break;
        }
        case RUN_RETURNING:
        {
            ReturnAndDisband(run);
            run.id = 0;  // flag for removal by the caller
            break;
        }
        }
    }

    static void Tick(uint32 diff)
    {
        for (Run& run : g_runs)
            AdvanceRun(run, diff);

        g_runs.erase(std::remove_if(g_runs.begin(), g_runs.end(),
            [](Run const& r) { return r.id == 0; }), g_runs.end());
    }

    static void LoadConfig()
    {
        Enable = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.Enable", true);
        OnlyRandomBots = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.OnlyRandomBots", true);
        TickSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.TickSeconds", 30);
        MaxConcurrentRuns = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxConcurrentRuns", 3);
        MinBotLevel = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MinBotLevel", 15);
        LevelBandWidth = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.LevelBandWidth", 4);
        MinGroupSize = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MinGroupSize", 3);
        EntranceSpread = float(sConfigMgr->GetOption<float>("PlayerbotDungeonSim.EntranceSpread", 4.0f));
        EnterTimeoutSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.EnterTimeoutSeconds", 60);
        MaxRunMinutes = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.MaxRunMinutes", 45);
        StartupDelaySeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.StartupDelaySeconds", 120);
        ReturnToCityOnEnd = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.ReturnToCityOnEnd", true);

        AwardEnabled = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AwardEnabled", true);
        AwardItemsPerRun = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.AwardItemsPerRun", 1);
        AwardEquip = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AwardEquip", true);
        AwardGoldCopper = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.AwardGoldCopper", 50000);
        ClearsToAdvanceTier = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.ClearsToAdvanceTier", 8);
        LegendaryChance = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.LegendaryChance", 2000);
        LegendaryMinTier = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.LegendaryMinTier", 4);
        LegendaryMinItemLevel = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.LegendaryMinItemLevel", 70);

        SimDurationMinutes = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.SimDurationMinutes", 30);
        RequireRealAttunement = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.RequireRealAttunement", false);
        AnnounceLfgChat = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AnnounceLfgChat", true);
        AnnounceGeneralChat = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AnnounceGeneralChat", true);
        AnnounceTradeChat = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AnnounceTradeChat", false);
        AllowPlayerJoin = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.AllowPlayerJoin", true);
        StageInCity = sConfigMgr->GetOption<bool>("PlayerbotDungeonSim.StageInCity", true);
        StageSeconds = sConfigMgr->GetOption<uint32>("PlayerbotDungeonSim.StageSeconds", 40);

        if (ClearsToAdvanceTier < 1)
            ClearsToAdvanceTier = 1;

        if (MinGroupSize < 2)
            MinGroupSize = 2;
        if (MaxConcurrentRuns < 1)
            MaxConcurrentRuns = 1;
    }
}

// ============================================================== world script
class PlayerbotDungeonSimWorldScript : public WorldScript
{
public:
    PlayerbotDungeonSimWorldScript() : WorldScript("PlayerbotDungeonSimWorldScript") {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        PBDSim::LoadConfig();
        _startupMs = 0;
        _formMs = 0;
        if (PBDSim::Enable)
            LOG_INFO("module", "[DungeonSim] Autonomous mode enabled (handoff to mod-dungeon-clear). "
                "Requires DungeonClear.AllowAutonomousBotRuns = 1.");
    }

    void OnUpdate(uint32 diff) override
    {
        using namespace PBDSim;
        if (!Enable)
            return;

        if (_startupMs < StartupDelaySeconds * IN_MILLISECONDS)
        {
            _startupMs += diff;
            return;
        }

        // Drive in-flight runs every tick for responsive entry/teardown.
        Tick(diff);

        // Throttle new-run formation.
        _formMs += diff;
        if (_formMs >= TickSeconds * IN_MILLISECONDS)
        {
            _formMs = 0;
            uint32 guard = 0;
            while (g_runs.size() < MaxConcurrentRuns && guard++ < MaxConcurrentRuns)
            {
                if (!TryFormRun())
                    break;
            }
        }
    }

private:
    uint32 _startupMs = 0;
    uint32 _formMs = 0;
};

// ================================================================== commands
class PlayerbotDungeonSimCommandScript : public CommandScript
{
public:
    PlayerbotDungeonSimCommandScript() : CommandScript("PlayerbotDungeonSimCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable sub =
        {
            { "status", HandleStatus, SEC_GAMEMASTER, Console::Yes },
            { "start",  HandleStart,  SEC_GAMEMASTER, Console::Yes },
            { "stop",   HandleStop,   SEC_GAMEMASTER, Console::Yes },
            { "join",   HandleJoin,   SEC_PLAYER,     Console::No },
        };
        static ChatCommandTable root = { { "dngsim", sub } };
        return root;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        using namespace PBDSim;
        if (g_runs.empty())
        {
            handler->SendSysMessage("DungeonSim: no active runs.");
            return true;
        }
        for (Run const& r : g_runs)
        {
            char const* st = r.state == RUN_STAGING ? "staging"
                : r.state == RUN_ENTERING ? "entering"
                : r.state == RUN_CLEARING ? "clearing"
                : r.state == RUN_SIMMING ? "sim-raid" : "returning";
            handler->PSendSysMessage("#{} {} — {} — {} bots — {}s in state.",
                r.id, r.name, st, uint32(r.members.size()), r.stateMs / IN_MILLISECONDS);
        }
        return true;
    }

    static bool HandleStart(ChatHandler* handler)
    {
        using namespace PBDSim;
        if (TryFormRun())
            handler->SendSysMessage("DungeonSim: launched a run.");
        else
            handler->SendSysMessage("DungeonSim: could not form a run (need an online tank-spec bot in band).");
        return true;
    }

    static bool HandleStop(ChatHandler* handler, Optional<std::string> param)
    {
        using namespace PBDSim;
        uint64 runId = 0;
        if (param && !param->empty())
        {
            try { runId = std::stoull(*param); }
            catch (...) { runId = 0; }
        }
        if (runId == 0)
        {
            handler->SendSysMessage("Usage: .dngsim stop <runId>  (see .dngsim status)");
            return true;
        }
        for (Run& r : g_runs)
        {
            if (r.id == runId)
            {
                r.state = RUN_RETURNING;
                r.stateMs = 0;
                handler->PSendSysMessage("DungeonSim: recalling run #{}.", runId);
                return true;
            }
        }
        handler->SendSysMessage("DungeonSim: no such run.");
        return true;
    }

    // A real player rides along on a forming 5-man. Joins the run's group while it
    // is still assembling; the entry state machine then teleports the player in too.
    static bool HandleJoin(ChatHandler* handler, Optional<std::string> param)
    {
        using namespace PBDSim;
        if (!AllowPlayerJoin)
        {
            handler->SendSysMessage("DungeonSim: player join is disabled here.");
            return true;
        }
        Player* p = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!p)
            return true;
        if (p->GetGroup())
        {
            handler->SendSysMessage("DungeonSim: leave your current group first.");
            return true;
        }

        uint64 runId = 0;
        if (param && !param->empty())
        {
            try { runId = std::stoull(*param); }
            catch (...) { runId = 0; }
        }

        for (Run& r : g_runs)
        {
            if (r.mode != RUN_MODE_REAL || r.state != RUN_ENTERING)
                continue;
            if (runId && r.id != runId)
                continue;
            Player* leader = ObjectAccessor::FindPlayer(r.leaderGuid);
            Group* g = leader ? leader->GetGroup() : nullptr;
            if (!g || g->IsFull())
                continue;
            if (g->AddMember(p))
            {
                r.members.push_back(p->GetGUID());
                handler->PSendSysMessage("DungeonSim: joined run #{} ({}).", r.id, r.name);
                return true;
            }
        }
        handler->SendSysMessage("DungeonSim: no joinable run right now (see .dngsim status).");
        return true;
    }
};

// ==================================================================== loader
void AddSC_mod_playerbot_dungeon_sim()
{
    new PlayerbotDungeonSimWorldScript();
    new PlayerbotDungeonSimCommandScript();
}

// mod-playerbot-dungeon-sim -> Addmod_playerbot_dungeon_simScripts (called by the
// generated module script loader derived from the directory name).
void Addmod_playerbot_dungeon_simScripts()
{
    AddSC_mod_playerbot_dungeon_sim();
}
