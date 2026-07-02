# mod-playerbot-dungeon-sim

Native-only **no-waypoint test build**.

This build deliberately disables the old in-instance waypoint/event-step systems by default.

The goal of this version is to test the clean behavior:

```text
Group forms
↓
Leader enters dungeon / followers join the same instance copy
↓
No internal waypoint jumps
↓
No cannon piles / WC forced anchors / weird mid-dungeon teleports
↓
SIM/HYBRID still resolves loot + XP
```

Keep:

- dungeon templates
- entrance/start teleports
- SIM / REAL / HYBRID
- loot
- XP
- audit log
- observe / where / evac
- same-instance leader/follower entry fix
- return-on-end unload fix

Disabled by default:

- `playerbot_dungeon_waypoint` usage
- `.dng-sim wp ...` debug commands
- `playerbot_dungeon_event_step` usage
- `.dng-sim step ...` debug commands

Recommended config:

```ini
PlayerbotDungeonSim.RunMode = SIM
PlayerbotDungeonSim.HybridSimReal = 1
PlayerbotDungeonSim.HybridRealChance = 10
PlayerbotDungeonSim.HybridRealFallbackToSim = 1

PlayerbotDungeonSim.DisableRouteWaypoints = 1
PlayerbotDungeonSim.InstanceWaypointTeleport = 0
PlayerbotDungeonSim.NativeEventSteps = 0
PlayerbotDungeonSim.ProgressiveTeleport = 0

PlayerbotDungeonSim.TeleportOnlineMembers = 1
PlayerbotDungeonSim.TeleportInsideInstance = 1
PlayerbotDungeonSim.CreateRealGroups = 1
PlayerbotDungeonSim.ReturnOnlineMembersOnEnd = 1

PlayerbotDungeonSim.AwardLoot = 1
PlayerbotDungeonSim.AwardXp = 1
PlayerbotDungeonSim.XpPercentOfLevel = 25
```

The old waypoint tables can remain in the DB. This build ignores them while `DisableRouteWaypoints = 1`.

No CMake file is included.

---

# mod-playerbot-dungeon-sim

Hybrid SIM/REAL build.

Recommended design:

- **SIM** is the stable alive-world system: bots form groups socially, enter dungeons if online, finish by timer, get boss credit and loot.
- **REAL** is the experimental live-clearing system: online bots are grouped, teleported in, then native event/leader/leash movement tries to make them actually run the dungeon.
- **Hybrid** lets both happen: most runs use SIM for reliable gear/progression, while a percentage of runs try REAL when enough online/movable bots are available.

Important configs:

```ini
PlayerbotDungeonSim.RunMode = SIM
PlayerbotDungeonSim.HybridSimReal = 1
PlayerbotDungeonSim.HybridRealChance = 25
PlayerbotDungeonSim.HybridRealFallbackToSim = 1
PlayerbotDungeonSim.SimFallback = 1
PlayerbotDungeonSim.SimMinimumBossKillsForLoot = 1
PlayerbotDungeonSim.SimMinimumBossKillsApplyToRaids = 0
```

Recommended for visible bot society/gear testing:

```ini
PlayerbotDungeonSim.AllowOnlineCharacterTouch = 1
PlayerbotDungeonSim.TeleportOnlineMembers = 1
PlayerbotDungeonSim.TeleportOnlyIfOnline = 0
PlayerbotDungeonSim.RequireOnlineBotsForRun = 0
PlayerbotDungeonSim.MinOnlineMembersForLiveRun = 5
PlayerbotDungeonSim.AutoUseGmGroupSelection = 1
PlayerbotDungeonSim.PreferOnlineCandidatesForLiveRuns = 1
```

What changed in this build:

- Organic auto-runs can choose SIM or REAL per run through `HybridSimReal`.
- If a REAL group cannot be found, it can fall back to SIM instead of doing nothing.
- Online candidates are preferred whenever online teleporting is enabled, so SIM runs can still show in `/who` and receive direct loot.
- Timed resolution now has a non-raid minimum boss kill floor for loot. With `SimMinimumBossKillsForLoot = 1`, even a rough WC-style wipe still awards at least first-boss loot so bots gear over time.
- Dedicated audit log from the previous build remains: `Logs/playerbot_dungeon_sim.log`.

No CMake file is included.


## Observer/debug helpers

This build adds:

```text
.dng-sim where [RUN_ID]
.dng-sim observe [RUN_ID]
```

`where` prints exact live player-object coordinates for every bot in a run, plus DB fallback coordinates. This is useful because `/who` only proves they are in a zone; it does not prove you are in the same instance copy.

`observe` is GM-only and tries to join your GM character to the bot party before teleporting near an online run member. This helps when RFC/SFK/DM bots exist in a different instance copy than the one your GM entered manually.

The default Deadmines native cannon anchor is disabled in SQL now because it caused bot piles at the cannon. Re-record better DM post-cannon/ship route steps in-game with `.dng-sim step add DM ...` when ready.

## XP awards

This build adds dungeon XP awards for bot members.

Recommended hybrid progression config:

```ini
PlayerbotDungeonSim.AwardXp = 1
PlayerbotDungeonSim.XpPercentOfLevel = 25
PlayerbotDungeonSim.XpScaleByBossKills = 1
PlayerbotDungeonSim.XpMinimumPercentOnAnyKill = 5
PlayerbotDungeonSim.XpLevelCap = 0
PlayerbotDungeonSim.GiveXpToOnlinePlayers = 1
PlayerbotDungeonSim.DeliverPendingXpOnLogin = 1
```

Behavior:

- Full clear = up to 25% of the bot's current level.
- Partial clear = scaled by boss kills when `XpScaleByBossKills = 1`.
- Any run with at least one boss kill gets at least `XpMinimumPercentOnAnyKill` percent.
- Bots at the effective level cap receive no XP.
- Online bot XP is delivered immediately through `Player::GiveXP`.
- Offline bot XP is stored in `playerbot_dungeon_xp_award` and delivered when that bot logs in.
- XP events are written to `Logs/playerbot_dungeon_sim.log` as `XP` / `XP_DELIVERY` lines.

Manual SQL fallback for the new character table:

```sql
USE azc_characters_ashbringer;
SOURCE C:/Apps/WowServ/modules/mod-playerbot-dungeon-sim/data/sql/manual/characters_xp_awards.sql;
```


## Native-only REAL mode

This build does not depend on `mod-dungeon-clear`. DungeonSim owns both sides now:

```text
SIM/HYBRID = stable timed progression, loot, XP, logs
REAL = live group, instance-entry, leader/follower leash, native route/event steps
```

Recommended path is still HYBRID: let SIM carry bot gear/XP progression while REAL stays an experimental live attempt layer.
