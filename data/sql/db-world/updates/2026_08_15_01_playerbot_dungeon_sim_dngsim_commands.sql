-- Registers the .dngsim commands on existing WORLD databases.
-- Security: 0 = player, 1 = moderator, 2 = gamemaster, 3 = administrator.

DELETE FROM `command` WHERE `name` IN
    ('dsim', 'dsim status', 'dsim start', 'dsim stop', 'dsim join',
     'dngsim', 'dngsim status', 'dngsim start', 'dngsim stop', 'dngsim join');

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('dngsim',        0, 'Syntax: .dngsim status|start|stop|join\nPlayerbot dungeon-sim commands.'),
('dngsim status', 2, 'Syntax: .dngsim status\nList active dungeon-sim runs and their state.'),
('dngsim start',  2, 'Syntax: .dngsim start\nForce the dungeon-sim to try to form a run now.'),
('dngsim stop',   2, 'Syntax: .dngsim stop <runId>\nRecall the given run (see .dngsim status).'),
('dngsim join',   0, 'Syntax: .dngsim join [runId]\nJoin a forming all-bot 5-man run.');
