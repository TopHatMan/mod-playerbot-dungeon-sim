CREATE TABLE IF NOT EXISTS `playerbot_dungeon_lockout` (
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `team` TINYINT UNSIGNED NOT NULL,
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `locked_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `reset_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`dungeon_template_id`,`team`,`guild_id`),
  KEY `idx_reset` (`reset_at`),
  KEY `idx_team_guild` (`team`,`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
