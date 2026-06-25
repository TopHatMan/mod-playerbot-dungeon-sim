-- Register Playerbot Dungeon Sim GM commands in the world database.
-- AzerothCore command names are stored without the leading dot.

DELETE FROM `command` WHERE `name` IN (
    'dng-sim',
    'dng-sim help',
    'dng-sim create',
    'dng-sim inv',
    'dng-sim rmv',
    'dng-sim wipe'
);

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('dng-sim', 3, 'Syntax: .dng-sim help\nPlayerbot Dungeon Sim GM test commands.'),
('dng-sim help', 3, 'Syntax: .dng-sim help\nShows Playerbot Dungeon Sim GM test commands.'),
('dng-sim create', 3, 'Syntax: .dng-sim create <instance name>\nForce-create a live Playerbot Dungeon Sim run for the named instance using eligible online bots.'),
('dng-sim inv', 3, 'Syntax: .dng-sim inv <instance name> <bot name>\nStage a named online bot for a forced dungeon sim run. The run starts when enough staged bots are present.'),
('dng-sim rmv', 3, 'Syntax: .dng-sim rmv <instance name> <bot name>\nRemove a named bot from the staged forced dungeon sim group.'),
('dng-sim wipe', 3, 'Syntax: .dng-sim wipe <instance name>\nClear staged groups for the named instance and mark active runs for that instance abandoned.');
