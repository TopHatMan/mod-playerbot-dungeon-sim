# mod-playerbot-dungeon-sim

AzerothCore module experiment for making playerbots form faction/guild-aware dungeon groups, teleport to dungeons, attempt real runs when possible, and fall back to simulated results/rewards.

## Current pass

Implemented so far:

- Dungeon template and boss template SQL.
- Character DB run/member/boss/loot tables.
- Faction-aware bot selection.
- Same-guild group preference with PUG fallback.
- Dungeon availability check through AzerothCore `DisableMgr` map checks.
- Timed run creation and result simulation.
- Optional live group creation for online characters.
- Optional teleport of online members to dungeon start coordinates.
- Boss depth simulation.
- Boss loot pull from `creature_loot_template`.
- Usability checks for class/race/level.
- Basic class/armor/role scoring.
- Need-roll style winner selection.
- Loot award logging.
- Pending loot delivery for offline winners on login.
- Smarter online delivery/equip helper with basic item-level/quality upgrade comparison.

## SQL install order

Run the base SQL files first:

- `data/sql/db-world/base/playerbot_dungeon_sim_world.sql`
- `data/sql/db-characters/base/playerbot_dungeon_sim_characters.sql`

Then run character updates in order:

- `2026_06_24_01_playerbot_dungeon_sim_live_runs.sql`
- `2026_06_24_02_playerbot_dungeon_sim_loot.sql`
- `2026_06_24_03_playerbot_dungeon_sim_pending_loot.sql`

## Important safety note

This module still intentionally avoids hard-coding a specific Playerbots fork's internal bot-account tables. Right now, eligibility reads from `characters`. Before running on a real populated realm, add your exact Playerbots filter so it does not select human player characters unless the invite system is explicitly enabled.

## Suggested safe config while testing

```ini
PlayerbotDungeonSim.Enable = 1
PlayerbotDungeonSim.Debug = 1
PlayerbotDungeonSim.MaxGroupsPerTick = 1
PlayerbotDungeonSim.TeleportOnlyIfOnline = 0
PlayerbotDungeonSim.GiveLootToOnlinePlayers = 1
PlayerbotDungeonSim.EquipUsableLootOnline = 0
PlayerbotDungeonSim.DeliverPendingLootOnLogin = 1
PlayerbotDungeonSim.EquipUpgradesOnDelivery = 0
```

Turn equip on only after item delivery is proven stable on your branch.

## Next planned chunk

- Real player invite option.
- Bot-only eligibility filter for your Playerbots DB layout.
- Raid lockout/progress memory.
- Dungeon run announcements.

## 2026-06-24 chunk 5: real-player invites + bot-only eligibility

Added optional real-player invite support:

- Finds online same-faction, partyless, level-eligible real players.
- Sends a dungeon invite prompt through system chat.
- Player can answer `yes`/`y` or `no`/`n` in say chat while the invite is active.
- Accepting adds the player to the run member table with `joined_as_real_player = 1` and teleports them to the dungeon entrance if teleporting is enabled.
- Declines and expired invites are tracked in `playerbot_dungeon_player_invite`.

Added bot-only eligibility config:

```ini
PlayerbotDungeonSim.BotOnlyEligibilityFilter = 0
PlayerbotDungeonSim.BotAccountMin = 0
PlayerbotDungeonSim.BotAccountMax = 0
```

Leave this disabled until you confirm the account ID range used by your Playerbots. Once enabled, dungeon sim will only pick characters in that account range as bot candidates, which prevents normal player alts from being silently used as simulated bots.

Important: the chat hook uses a simple `yes/no` prototype. If your AzerothCore branch has a different `PlayerScript::OnChat` signature, adjust that one method only; the invite DB/state logic is independent.

## Raid mode / level-cap gate

Raid templates are now dormant until the realm cap supports them. The module reads `MaxPlayerLevel` from the core config through `CONFIG_MAX_PLAYER_LEVEL` and refuses raid templates unless:

- `PlayerbotDungeonSim.RaidMode = 1`
- server level cap is at least `PlayerbotDungeonSim.RaidMinServerLevelCap`
- server level cap is at least the raid template `min_level`

So if the realm is currently capped at 40, Molten Core remains ignored even though the SQL template exists.

Bot-sim raids waive attunement checks by design. That waiver only affects simulated bot raid selection/progression; it does not grant real players quest flags, keys, or attunement credit.

Raid progression is tracked in `playerbot_dungeon_raid_progress` by dungeon template, faction, and guild. A guild/PUG raid remembers its best boss depth, so Molten Core can naturally look like: Lucifron/Magmadar attempts first, then Gehennas/Garr, then deeper progression later.

## Vanilla raid ladder and lockouts

This build treats Vanilla 5-man dungeons as repeatable content with no saved lockout by default:

```ini
PlayerbotDungeonSim.NormalDungeonLockouts = 0
```

Raid simulation is gated by the current realm level cap and the module progression phase. If `MaxPlayerLevel` is 40, no level-60 raid template is selected. Once the cap is 60, the Vanilla raid ladder becomes available by phase:

```text
Phase 0: Molten Core, Onyxia
Phase 4: Blackwing Lair
Phase 5: Zul'Gurub
Phase 6: AQ20, AQ40
Phase 7: Naxxramas 40
```

Relevant config:

```ini
PlayerbotDungeonSim.RaidMode = 1
PlayerbotDungeonSim.RaidMinServerLevelCap = 60
PlayerbotDungeonSim.RaidLockouts = 1
PlayerbotDungeonSim.RaidLockoutDays = 7
PlayerbotDungeonSim.RespectProgressionPhase = 1
PlayerbotDungeonSim.ProgressionPhase = 0
PlayerbotDungeonSim.RaidWaiveBotAttunement = 1
```

Attunement is waived only for bot-sim raid selection. This module does not grant real players attunement or bypass normal player entry requirements.

## Console status and guild simulation

This build prints run activity to the worldserver console:

- `START run #...` when a dungeon/raid group is created
- `STATUS active dungeon/raid runs:` every configured interval
- `END run #...` when the timed run resolves
- `[GuildSim guild=...]` messages when guild groups talk about runs

New config keys:

```ini
PlayerbotDungeonSim.ConsoleStatus = 1
PlayerbotDungeonSim.ConsoleStatusSeconds = 120
PlayerbotDungeonSim.GuildChatSimulation = 1
PlayerbotDungeonSim.GuildChatChance = 75
PlayerbotDungeonSim.GuildChatToOnlineMembers = 1
```

`GuildChatToOnlineMembers` sends the simulated guild lines to online guild members as green system messages. It is intentionally not a raw guild-chat packet yet, because different AzerothCore/playerbot branches expose different chat APIs. The console output is always present when guild simulation fires.

### 2026-06-24 loader symbol fix

Added the exact AzerothCore generated module loader export:

```cpp
void Addmod_playerbot_dungeon_simScripts()
```

This fixes linker error `LNK2019 unresolved external symbol Addmod_playerbot_dungeon_simScripts`.

## Social / LFG simulation

The module now simulates social friction and public LFG chatter:

- `PlayerbotDungeonSim.SimulatedBotDeclineChance` makes some candidate bots say no before a group is formed.
- Guild groups can still announce inside the guild sim.
- `GeneralChatSimulation` prints city-style LFM messages such as `[GeneralSim:Stormwind] Botname: LFM Deadmines...`.
- `LocalZoneChatSimulation` prints dungeon-local chatter such as `[LocalSim:Scarlet Monastery] Botname: Anyone near Scarlet Monastery?`.

These public messages are intentionally branch-safe console/system simulation messages for now. They do not inject raw chat packets into live General channel state, which differs between AzerothCore branches.

## Organic LFG acting layer

The dungeon group is still formed internally first, but the module can now make the bots act out the social flow before the run starts.

Example console lines:

```text
[GeneralSim:Ironforge] Brokkar: LFM Deadmines need tank/heals/dps
[GeneralSim:Ironforge] Miralyn: I can heal
[GeneralSim:Ironforge] Thandor: tank here prot warrior
[GeneralSim:Ironforge] Kelri: frost mage dps
[LocalSim:Deadmines] Kelri: summons? at stone? heading to Deadmines
```

Classic-capital restriction is on by default, so simulated public chat uses only:

- Alliance: Stormwind, Ironforge, Darnassus
- Horde: Orgrimmar, Thunder Bluff, Undercity

No Exodar or Silvermoon while the realm is progression-centered in Vanilla.

## Trade BOE / COD flavor

The module can also print occasional TradeSim spam:

```text
[TradeSim:Stormwind] Arlena: WTS blue 2h axe 14g can COD
```

This is currently safe console-visible flavor only. The next safe implementation step would be a real `playerbot_dungeon_trade_offer` table plus a player chat response such as `cod yes`, then server-side COD mail creation once we wire it to your branch's mail API.

## Mixed real-player runs

When `PlayerbotDungeonSim.ManualTravelForPlayerJoinedRuns = 1`, any run that may invite a real player will not auto-teleport the player or the bot group. The player accepts the invite, then travels with the bots normally or uses existing bot summon/party commands. Bot-only runs can still auto-teleport if `TeleportOnlineMembers = 1`.


## Zero-member run fix / safety

This build allocates `playerbot_dungeon_run.id` before insert instead of using `SELECT LAST_INSERT_ID()` after async `Execute()`. On AzerothCore, async SQL can use a different DB connection, which could create active runs with `members=0`.

It also refuses to run unless bot-only filtering is enabled with either `PlayerbotDungeonSim.BotAccountPrefix` or a numeric account range. For Nick's server this should be:

```ini
PlayerbotDungeonSim.BotOnlyEligibilityFilter = 1
PlayerbotDungeonSim.BotAccountPrefix = ashrandbot
```

Old orphan active runs can be cleaned with:

```sql
UPDATE playerbot_dungeon_run r
LEFT JOIN playerbot_dungeon_run_member rm ON rm.run_id = r.id
SET r.state = 3, r.result = 4
WHERE r.state IN (0,1,4) AND rm.run_id IS NULL;
```

## Live bot dungeon test mode

This build supports the controlled live test flow:

1. Build a bot-only group from configured bot accounts only.
2. Create a real live group for online bot members if possible.
3. Teleport online bot members into the instance start when `TeleportInsideInstance = 1`.
4. Let them remain there for the run timer.
5. Resolve the run by simulation fallback.
6. Award loot from killed boss loot tables.
7. Deliver loot immediately to online bot winners if `GiveLootToOnlinePlayers = 1`.
8. Optionally equip usable upgrades if `EquipUsableLootOnline = 1`.

The instance-start teleport is resolved from AzerothCore's `areatrigger_teleport` table by matching `target_map = dungeon.map_id`. If no matching areatrigger row exists, the module logs a debug message and falls back to the dungeon template's outdoor entrance coordinates.

For a first live test, use short durations and store-only loot:

```ini
PlayerbotDungeonSim.AllowOnlineCharacterTouch = 1
PlayerbotDungeonSim.CreateRealGroups = 1
PlayerbotDungeonSim.TeleportOnlineMembers = 1
PlayerbotDungeonSim.TeleportInsideInstance = 1
PlayerbotDungeonSim.MinOnlineMembersForLiveRun = 1
PlayerbotDungeonSim.InviteRealPlayers = 0
PlayerbotDungeonSim.GiveLootToOnlinePlayers = 1
PlayerbotDungeonSim.EquipUsableLootOnline = 0
PlayerbotDungeonSim.DeliverPendingLootOnLogin = 0
PlayerbotDungeonSim.MinDurationMinutes = 2
PlayerbotDungeonSim.MaxDurationMinutes = 4
```

After `StoreNewItem` delivery is confirmed stable, test equipment replacement with:

```ini
PlayerbotDungeonSim.EquipUsableLootOnline = 1
PlayerbotDungeonSim.OnlyEquipBetterItems = 1
```

Hard safety remains: online movement and direct loot delivery call the bot account filter again before touching any `Player*`. On Nick's server, only accounts whose username starts with `ashrandbot` should be eligible.


## Playerbot config import

The module can now read the important Playerbots account settings directly from `playerbots.conf` through `sConfigMgr`.

Recommended:

```ini
PlayerbotDungeonSim.UsePlayerbotConfig = 1
PlayerbotDungeonSim.BotOnlyEligibilityFilter = 1
PlayerbotDungeonSim.BotAccountPrefix = auto
```

With `auto`, DungeonSim uses `AiPlayerbot.RandomBotAccountPrefix` from Playerbots, for example `ashrandbot`.
This keeps DungeonSim from touching real player accounts when the Playerbot prefix changes.

DungeonSim also logs the imported Playerbot values at startup: prefix, account count, random bot autologin flags, min/max random bots, and disabled-without-real-player state.


## Live-only testing

If you want to verify that real online Playerbot characters are actually grouped, moved, and rewarded, enable:

```ini
PlayerbotDungeonSim.AllowOnlineCharacterTouch = 1
PlayerbotDungeonSim.RequireOnlineBotsForRun = 1
PlayerbotDungeonSim.MinOnlineMembersForLiveRun = 1
PlayerbotDungeonSim.CreateRealGroups = 1
PlayerbotDungeonSim.TeleportOnlineMembers = 1
PlayerbotDungeonSim.GiveLootToOnlinePlayers = 1
```

With `RequireOnlineBotsForRun = 1`, a dungeon run will not start as offline simulation when zero online/movable bots are available. Console lines should skip the run instead of creating `members=5 moved=0` fake-only runs.

SQL note: `playerbot_dungeon_run` and `playerbot_dungeon_run_member` are character-database tables. If cleaning old test runs manually, select/use the characters database, not world.


## Live visibility tuning

For a real-bot test, set:

```ini
PlayerbotDungeonSim.RequireOnlineBotsForRun = 1
PlayerbotDungeonSim.CleanupOfflineRunsWhenLiveOnly = 1
PlayerbotDungeonSim.StatusShowOnlineMembers = 1
```

Console status will show `members`, `online`, `movable`, and `moved`. If `moved=0`, no online bot was teleported, and `/who` will not show a bot inside the dungeon.

## Live dungeon test checklist

This build includes forced SQL installer updates:

- `data/sql/db-world/updates/2026_06_24_99_playerbot_dungeon_sim_force_world_install.sql`
- `data/sql/db-characters/updates/2026_06_24_99_playerbot_dungeon_sim_force_characters_install.sql`

The runtime tables such as `playerbot_dungeon_run`, `playerbot_dungeon_run_member`, and `playerbot_dungeon_loot_award` belong in the **characters** database, not world.

For a live visual test, use:

```ini
PlayerbotDungeonSim.UsePlayerbotConfig = 1
PlayerbotDungeonSim.BotOnlyEligibilityFilter = 1
PlayerbotDungeonSim.BotAccountPrefix = auto
PlayerbotDungeonSim.RequireOnlineBotsForRun = 1
PlayerbotDungeonSim.MinOnlineMembersForLiveRun = 1
PlayerbotDungeonSim.AllowOnlineCharacterTouch = 1
PlayerbotDungeonSim.CreateRealGroups = 1
PlayerbotDungeonSim.TeleportOnlineMembers = 1
PlayerbotDungeonSim.TeleportInsideInstance = 1
PlayerbotDungeonSim.InviteRealPlayers = 0
PlayerbotDungeonSim.GiveLootToOnlinePlayers = 1
PlayerbotDungeonSim.EquipUsableLootOnline = 0
PlayerbotDungeonSim.MinDurationMinutes = 2
PlayerbotDungeonSim.MaxDurationMinutes = 4
```

Expected console status for a real live run:

```text
members=5 online=1 movable=1 moved=1
```

If it says `moved=0`, the run is not visually inside the dungeon.
