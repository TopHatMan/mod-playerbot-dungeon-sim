CREATE TABLE IF NOT EXISTS `playerbot_dungeon_run` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `team` TINYINT UNSIGNED NOT NULL,
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 forming, 1 active, 2 completed, 3 failed',
  `started_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `ends_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `quality_score` SMALLINT NOT NULL DEFAULT 0,
  `planned_duration_sec` INT UNSIGNED NOT NULL DEFAULT 0,
  `result` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 pending, 1 full, 2 partial, 3 wipe, 4 abandoned',
  `bosses_killed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `real_group_created` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `online_members_moved` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `entrance_map` INT UNSIGNED NOT NULL DEFAULT 0,
  `entrance_x` FLOAT NOT NULL DEFAULT 0,
  `entrance_y` FLOAT NOT NULL DEFAULT 0,
  `entrance_z` FLOAT NOT NULL DEFAULT 0,
  `entrance_o` FLOAT NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_state` (`state`),
  KEY `idx_template` (`dungeon_template_id`),
  KEY `idx_team` (`team`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_run_member` (
  `run_id` BIGINT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `role` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 unknown, 1 tank, 2 healer, 3 dps',
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `level_at_start` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `joined_as_real_player` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`run_id`,`guid`),
  KEY `idx_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_run_boss` (
  `run_id` BIGINT UNSIGNED NOT NULL,
  `boss_order` TINYINT UNSIGNED NOT NULL,
  `boss_name` VARCHAR(100) NOT NULL,
  `killed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`run_id`,`boss_order`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_loot_award` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `run_id` BIGINT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `item_entry` INT UNSIGNED NOT NULL,
  `source_boss` VARCHAR(100) NOT NULL DEFAULT '',
  `source_boss_order` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `equipped` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `stored_online` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `note` VARCHAR(255) NOT NULL DEFAULT '',
  `awarded_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_run` (`run_id`),
  KEY `idx_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

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
