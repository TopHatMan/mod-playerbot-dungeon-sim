-- Raid progression memory for simulated bot raid teams.
-- This is keyed by dungeon template + faction + guild. guild_id 0 represents a PUG raid pool.
CREATE TABLE IF NOT EXISTS `playerbot_dungeon_raid_progress` (
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `team` TINYINT UNSIGNED NOT NULL,
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `best_bosses_killed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `last_bosses_killed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `attempts` INT UNSIGNED NOT NULL DEFAULT 0,
  `kills_total` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_run_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`dungeon_template_id`,`team`,`guild_id`),
  KEY `idx_guild` (`guild_id`),
  KEY `idx_team` (`team`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
