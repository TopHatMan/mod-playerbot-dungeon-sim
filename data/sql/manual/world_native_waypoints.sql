-- NativeClear seed waypoints imported from DungeonClear research.
-- This does not create a dependency on mod-dungeon-clear.

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_waypoint` (
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `step_order` INT UNSIGNED NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `position_x` FLOAT NOT NULL,
  `position_y` FLOAT NOT NULL,
  `position_z` FLOAT NOT NULL,
  `orientation` FLOAT NOT NULL DEFAULT 0,
  `label` VARCHAR(64) NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`dungeon_template_id`, `step_order`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Deadmines, template id 3, map 36
-- Cannon anchor from DungeonClear's Deadmines event notes.
INSERT INTO `playerbot_dungeon_waypoint`
(`dungeon_template_id`, `step_order`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `label`, `enabled`) VALUES
(3, 30, 36, -107.56, -659.67, 7.21, 0, 'dm_cannon_anchor', 1)
ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=VALUES(enabled);

-- Wailing Caverns, template id 2, map 43
-- The important WC problem is not the entrance; it is the disconnected/drop portions.
INSERT INTO `playerbot_dungeon_waypoint`
(`dungeon_template_id`, `step_order`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `label`, `enabled`) VALUES
(2, 50, 43, -290.65567, -3.8297224, -58.30473, 0, 'wc_serpentis_lip', 1),
(2, 51, 43, -285.45773, 4.021016, -63.919395, 0, 'wc_serpentis_landing', 1),
(2, 70, 43, -55.89, 44.32, -29.01, 0, 'wc_verdan_drop_lip', 1),
(2, 71, 43, -49.5, 47.6, -105.83, 0, 'wc_lower_caverns_landing', 1),
(2, 80, 43, -134.97, 125.40, -78.09, 0, 'wc_disciple_naralex_anchor', 1)
ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=VALUES(enabled);

-- Shadowfang Keep, template id 4, map 33
INSERT INTO `playerbot_dungeon_waypoint`
(`dungeon_template_id`, `step_order`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `label`, `enabled`) VALUES
(4, 20, 33, -248.0, 2122.0, 81.3, 0, 'sfk_ashcrombe_anchor', 1),
(4, 21, 33, -242.58, 2159.05, 90.62, 0, 'sfk_courtyard_door', 1),
(4, 22, 33, -251.0, 2115.0, 81.3, 0, 'sfk_adamant_anchor', 1)
ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=VALUES(enabled);

-- Razorfen Downs, template id 14 in current template ladder, map 129
INSERT INTO `playerbot_dungeon_waypoint`
(`dungeon_template_id`, `step_order`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `label`, `enabled`) VALUES
(14, 40, 129, 2515.71, 854.81, 47.68, 0, 'rfd_mordresh_combat_spot', 1)
ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=VALUES(enabled);

-- Blackrock Depths, template id 18 in current template ladder, map 230
INSERT INTO `playerbot_dungeon_waypoint`
(`dungeon_template_id`, `step_order`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `label`, `enabled`) VALUES
(18, 40, 230, 596.432, -188.498, -53.9, 0, 'brd_ring_of_law_arena', 1)
ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=VALUES(enabled);

-- Uldaman, template id 16 in current template ladder, map 70
INSERT INTO `playerbot_dungeon_waypoint`
(`dungeon_template_id`, `step_order`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `label`, `enabled`) VALUES
(16, 40, 70, -234.69, 239.62, -50.91, 0, 'uld_ironaya_room_anchor', 1),
(16, 60, 70, 104.85, 272.45, -26.53, 0, 'uld_keeper_altar_anchor', 1),
(16, 70, 70, 96.48, 269.05, -52.15, 0, 'uld_archaedas_altar_anchor', 1)
ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=VALUES(enabled);

-- Zul'Farrak, template id 17 in current template ladder, map 209
INSERT INTO `playerbot_dungeon_waypoint`
(`dungeon_template_id`, `step_order`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `label`, `enabled`) VALUES
(17, 60, 209, 1886.0, 1250.0, 30.0, 0, 'zf_pyramid_ramp_anchor', 1)
ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=VALUES(enabled);

-- Sunken Temple, template id 23 in current template ladder, map 109
INSERT INTO `playerbot_dungeon_waypoint`
(`dungeon_template_id`, `step_order`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `label`, `enabled`) VALUES
(23, 60, 109, -466.8, 272.9, -90.4, 0, 'st_hakkar_sanctum_anchor', 1)
ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), label=VALUES(label), enabled=VALUES(enabled);
