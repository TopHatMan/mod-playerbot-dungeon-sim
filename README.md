# mod-playerbot-dungeon-sim

An **active-world progression engine** for [AzerothCore](https://www.azerothcore.org/)
+ [Playerbots](https://github.com/liyunfan1223/mod-playerbots). It drives random
bots up a level-appropriate content ladder — real 5-man dungeon clears while
levelling, then offscreen "sim" raid progression at cap — and **guarantees their
gearing** from the real boss loot tables, so the world fills with bots that are
actually geared and progressing like players.

It is not "every bot runs dungeons." Only a slice run content at any time; the
point is a living server where guilds visibly progress and geared bots spill out
into the world.

---

## ⚠️ Requires a forked mod-dungeon-clear

This module hands real 5-man runs to **mod-dungeon-clear** by calling
`DungeonClearControl::StartAutonomousClear(...)`. That entry point does **not**
exist in upstream mod-dungeon-clear — it is added by **my fork**:

> **You must use my fork of mod-dungeon-clear:**
> `https://github.com/<your-user>/mod-dungeon-clear`  ← *(update this link)*

If you build against upstream mod-dungeon-clear you will get a **linker error**:

```
unresolved external symbol "bool DungeonClearControl::StartAutonomousClear(class Player *)"
```

That is the signal you're on the wrong dungeon-clear. Install the fork and set
`DungeonClear.AllowAutonomousBotRuns = 1` in its config.

---

## Features

- **Level 1–60 dungeon ladder.** Bots run level-appropriate 5-mans (selection
  follows their level; leveling itself is handled by the Playerbots factory).
- **Faction-routed dungeons.** RFC is Horde-only, the Stockade is Alliance-only,
  Wailing Caverns / Shadowfang Keep lean Horde, Deadmines leans Alliance, the rest
  are 50/50 — zero-weight maps are never offered to the wrong faction.
- **Raid progression (sim).** At cap, bots climb MC → Onyxia → BWL → ZG/AQ20 →
  AQ40 → Naxxramas. Raids run offscreen ("sim mode") since dungeon-clear can't
  drive 20/40-mans; bots keep roaming the world while their raid progress is
  credited.
- **Guaranteed gearing.** On a completed run, each bot is granted its best slot
  upgrade(s) pulled from the dungeon/raid bosses' real `creature_loot_template`
  drops, filtered by class/race/level and only when it beats the current item.
  Plus gold and a tier credit.
- **Slow legendaries.** Gated behind max level **and** deep raid tier **and**
  raid-level average gear, then a tiny per-run roll — the long tail of guild
  progression, never a levelling drop.
- **Alive-world LFG chat.** Forming groups post "LFG/LFM \<dungeon\>" in the LFG
  channel (and optionally Trade/General), flavored for guild runs vs pugs.
- **Players can join.** `.dngsim join [runId]` drops a real player into a forming
  5-man; all-bot groups are teleported in, player-led groups enter naturally.
- **Disabled-content aware.** Every candidate map is checked against the `disables`
  table, so disabled instances (e.g. Maraudon, Dire Maul) are never used.
- **Coordinated.** Shares `BotActivityRegistry.h` with the world-PvP and artisans
  mods so a bot is never sent to two activities at once.

## Requirements

- AzerothCore (with the Eluna/module system) built on your platform.
- mod-playerbots.
- **My fork of mod-dungeon-clear** (see the notice above), with
  `DungeonClear.AllowAutonomousBotRuns = 1`.

## Installation

```bash
cd azerothcore/modules
git clone https://github.com/<your-user>/mod-playerbot-dungeon-sim
git clone https://github.com/<your-user>/mod-dungeon-clear    # the fork
# re-run CMake configure, then rebuild
```

1. Apply the SQL in `sql/` (the `playerbot_dungeon_progression` table goes on the
   **characters** database).
2. Ensure `src/BotActivityRegistry.h` is byte-identical across this, the world-PvP,
   and the artisans modules.
3. Copy the `.conf.dist` to your config and tune it.

## Commands

| Command | Access | Description |
|---|---|---|
| `.dngsim status` | GM | List active runs and their state |
| `.dngsim start` | GM | Force a run to form now |
| `.dngsim stop <runId>` | GM | Recall a run |
| `.dngsim join [runId]` | Player | Join a forming 5-man |

## Configuration

See `conf/mod_playerbot_dungeon_sim.conf.dist` — run cadence, group rules, award
amounts, legendary gates, the raid-sim duration, attunement, and the LFG-chat
channels are all tunable.

## Credits

Built on AzerothCore and mod-playerbots. Requires a forked mod-dungeon-clear.