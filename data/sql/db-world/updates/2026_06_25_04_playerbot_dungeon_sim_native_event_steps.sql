-- Native DungeonSim route/event steps.
-- These are not a dependency on mod-dungeon-clear. They are DungeonSim's own recorded route/event anchors.
-- GM usage examples:
-- .dng-sim step add WC 10 move 15 first_pull
-- .dng-sim step add WC 20 teleport 20 serpentis_drop
-- .dng-sim step list WC

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_event_step` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `step_order` SMALLINT UNSIGNED NOT NULL,
  `step_type` VARCHAR(24) NOT NULL DEFAULT 'move',
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `position_x` FLOAT NOT NULL DEFAULT 0,
  `position_y` FLOAT NOT NULL DEFAULT 0,
  `position_z` FLOAT NOT NULL DEFAULT 0,
  `orientation` FLOAT NOT NULL DEFAULT 0,
  `wait_seconds` INT UNSIGNED NOT NULL DEFAULT 0,
  `label` VARCHAR(100) NOT NULL DEFAULT '',
  `data` VARCHAR(255) NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_dungeon_step` (`dungeon_template_id`,`step_order`),
  KEY `idx_dungeon_enabled` (`dungeon_template_id`,`enabled`),
  KEY `idx_step_type` (`step_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Seed a few starter event steps from the native waypoint research.
-- Deadmines. These are conservative anchors; tune in-game with .dng-sim step add DM ...
INSERT INTO `playerbot_dungeon_event_step`
(`dungeon_template_id`,`step_order`,`step_type`,`map_id`,`position_x`,`position_y`,`position_z`,`orientation`,`wait_seconds`,`label`,`enabled`) VALUES
(3,10,'move',36,-16.40,-383.07,61.78,0,20,'dm_first_pulls',1),
(3,20,'log',36,-107.56,-659.67,7.21,0,25,'dm_cannon_anchor_disabled_record_better_position',0),
(3,30,'log',36,-107.56,-659.67,7.21,0,10,'dm_cannon_wait_disabled_record_better_position',0)
ON DUPLICATE KEY UPDATE step_type=VALUES(step_type), map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), wait_seconds=VALUES(wait_seconds), label=VALUES(label), enabled=VALUES(enabled);

-- Wailing Caverns. WC needs anchors because bots often cannot find the next pull from the start/drop areas.
INSERT INTO `playerbot_dungeon_event_step`
(`dungeon_template_id`,`step_order`,`step_type`,`map_id`,`position_x`,`position_y`,`position_z`,`orientation`,`wait_seconds`,`label`,`enabled`) VALUES
(2,10,'move',43,-163.49,132.90,-74.62,0,20,'wc_first_cavern_anchor',1),
(2,20,'move',43,-290.65,-3.82,-58.30,0,25,'wc_serpentis_lip',1),
(2,30,'teleport',43,-285.45,4.02,-63.91,0,20,'wc_serpentis_landing',1),
(2,40,'move',43,-55.89,44.32,-29.01,0,30,'wc_verdan_drop_lip',1),
(2,50,'teleport',43,-49.50,47.60,-105.83,0,20,'wc_lower_caverns_landing',1),
(2,60,'move',43,-134.97,125.40,-78.09,0,30,'wc_naralex_anchor',1)
ON DUPLICATE KEY UPDATE step_type=VALUES(step_type), map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), wait_seconds=VALUES(wait_seconds), label=VALUES(label), enabled=VALUES(enabled);

-- Shadowfang Keep event anchors.
INSERT INTO `playerbot_dungeon_event_step`
(`dungeon_template_id`,`step_order`,`step_type`,`map_id`,`position_x`,`position_y`,`position_z`,`orientation`,`wait_seconds`,`label`,`enabled`) VALUES
(4,10,'move',33,-248.0,2122.0,81.3,0,20,'sfk_prisoner_anchor',1),
(4,20,'move',33,-242.58,2159.05,90.62,0,25,'sfk_courtyard_door',1),
(4,30,'move',33,-251.0,2115.0,81.3,0,20,'sfk_adamant_anchor',1)
ON DUPLICATE KEY UPDATE step_type=VALUES(step_type), map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), wait_seconds=VALUES(wait_seconds), label=VALUES(label), enabled=VALUES(enabled);
