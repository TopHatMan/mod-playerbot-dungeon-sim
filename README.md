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
