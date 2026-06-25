# NativeClear import notes

This module does **not** depend on `mod-dungeon-clear`.

The uploaded DungeonClear module was reviewed as a reference implementation. The pieces worth absorbing into PlayerbotDungeonSim are architectural ideas and dungeon-specific event knowledge, not the original command model.

## Useful DungeonClear ideas to absorb

- Long-distance dungeon navigation must not be one raw `MoveTo` call. Routes need anchor points, event points, and recovery logic.
- Some dungeons require special one-way movement events, such as Wailing Caverns drops.
- Some dungeons require scripted objective events, such as the Deadmines cannon, Shadowfang prisoner door, Zul'Farrak pyramid/cage, Uldaman altar events, and Sunken Temple avatar event.
- Tank/leader should drive the route; followers should follow/cluster around the leader.
- If a dungeon has a known broken navmesh gap, use an explicit route event/anchor instead of trusting stock pathfinding.

## What was imported in this pass

This pass imports the known event/anchor coordinates into PlayerbotDungeonSim's own route table, using the existing native waypoint system:

- Deadmines cannon anchor
- Wailing Caverns Serpentis drop anchor/landing
- Wailing Caverns Verdan return-fall/drop anchor/landing
- Wailing Caverns Disciple of Naralex escort anchor
- Shadowfang Keep prisoner/courtyard anchors
- Razorfen Downs Mordresh combat anchor
- Blackrock Depths Ring of Law arena anchor
- Uldaman Ironaya/Keepers/Archaedas anchors
- Zul'Farrak pyramid ramp anchor
- Sunken Temple sanctum anchor

## Next native phases

1. Keep DungeonSim as the owner of social LFG, group creation, progression gating, loot, and run lifecycle.
2. Add native per-dungeon event execution using `playerbot_dungeon_event_step` instead of only teleport waypoints.
3. Port only the minimum pathing helpers needed for long routes into a `PBDS::NativeClear` namespace if waypoint teleport is not enough.
4. Keep the module self-sufficient: no DungeonClear dependency, no `.dc on`, no external player authorization model.
