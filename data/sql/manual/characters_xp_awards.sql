-- Manual fallback. Run this against the CHARACTERS database.

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_xp_award` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `run_id` BIGINT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `level_at_award` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `xp_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `percent_of_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `killed_bosses` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `total_bosses` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `delivered` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 pending, 1 failed, 2 delivered',
  `delivered_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `awarded_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `note` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `idx_run` (`run_id`),
  KEY `idx_guid_delivered` (`guid`,`delivered`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
