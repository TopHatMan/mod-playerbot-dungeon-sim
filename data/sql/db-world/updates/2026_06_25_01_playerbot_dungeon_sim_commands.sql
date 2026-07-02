USE azc_world_ashbringer;

DELETE FROM `command` WHERE `name` IN (
    'dng-sim',
    'dng-sim help',
    'dng-sim create',
    'dng-sim inv',
    'dng-sim rmv',
    'dng-sim wipe',
    'dng-sim mode',
    'dng-sim where',
    'dng-sim observe',
    'dng-sim evac',
    'dng-sim wp',
    'dng-sim waypoint',
    'dng-sim step',
    'dng-sim event',
    'dng-sim native'
);

REPLACE INTO `command` (`name`, `security`, `help`) VALUES
('dng-sim', 3, 'Syntax: .dng-sim help\nPlayerbot Dungeon Sim GM test commands.'),
('dng-sim help', 3, 'Syntax: .dng-sim help\nShows Playerbot Dungeon Sim GM test commands.'),
('dng-sim create', 3, 'Syntax: .dng-sim create [sim|real] <instance name>\nForce-create a SIM or REAL Playerbot Dungeon Sim run for the named instance.'),
('dng-sim inv', 3, 'Syntax: .dng-sim inv <instance name> <bot name>\nStage a named bot for a forced dungeon sim run. In REAL mode the bot must be online/movable when the group starts.'),
('dng-sim rmv', 3, 'Syntax: .dng-sim rmv <instance name> <bot name>\nRemove a named bot from the staged forced dungeon sim group.'),
('dng-sim wipe', 3, 'Syntax: .dng-sim wipe <instance name>\nClear staged groups for the named instance, mark active runs abandoned, and evac online bots.'),
('dng-sim mode', 3, 'Syntax: .dng-sim mode\nShows SIM/REAL mode explanation.'),
('dng-sim where', 3, 'Syntax: .dng-sim where [RUN_ID]\nShows exact online/offline member positions for a DungeonSim run.'),
('dng-sim observe', 3, 'Syntax: .dng-sim observe [RUN_ID]\nGM observer helper: teleport near an online run member in their instance copy when possible.'),
('dng-sim evac', 3, 'Syntax: .dng-sim evac [RUN_ID|all]\nEmergency teleport online DungeonSim bots out of instance maps to the dungeon outdoor entrance.'),
('dng-sim wp', 3, 'Syntax: .dng-sim wp add|list|del|clear <INSTANCE> [ORDER] [LABEL]'),
('dng-sim waypoint', 3, 'Syntax: .dng-sim waypoint add|list|del|clear <INSTANCE> [ORDER] [LABEL]'),
('dng-sim step', 3, 'Syntax: .dng-sim step add|list|del|clear <INSTANCE> [ORDER] [TYPE] [WAIT] [LABEL]'),
('dng-sim event', 3, 'Syntax: .dng-sim event add|list|del|clear <INSTANCE> [ORDER] [TYPE] [WAIT] [LABEL]'),
('dng-sim native', 3, 'Syntax: .dng-sim native add|list|del|clear <INSTANCE> [ORDER] [TYPE] [WAIT] [LABEL]');
