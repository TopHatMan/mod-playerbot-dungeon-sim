USE azc_world_ashbringer;

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_waypoint` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `step_order` SMALLINT UNSIGNED NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `position_x` FLOAT NOT NULL,
  `position_y` FLOAT NOT NULL,
  `position_z` FLOAT NOT NULL,
  `orientation` FLOAT NOT NULL DEFAULT 0,
  `label` VARCHAR(100) NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_dungeon_step` (`dungeon_template_id`,`step_order`),
  KEY `idx_dungeon_enabled` (`dungeon_template_id`,`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
