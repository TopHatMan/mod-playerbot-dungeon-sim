-- Force characters install for mod-playerbot-dungeon-sim.
-- This is intentionally in updates/ so AzerothCore auto-updater creates/repairs the tables even if base SQL was skipped.

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_run` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `team` TINYINT UNSIGNED NOT NULL,
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 forming, 1 active, 2 completed, 3 failed, 4 real attempt',
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
  KEY `idx_team` (`team`),
  KEY `idx_ends` (`ends_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_run_member` (
  `run_id` BIGINT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `role` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 unknown, 1 tank, 2 healer, 3 dps',
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `level_at_start` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `joined_as_real_player` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`run_id`,`guid`),
  KEY `idx_guid` (`guid`),
  KEY `idx_guild` (`guild_id`)
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
  `delivery_state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 pending, 1 failed, 2 delivered',
  `delivered_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `upgrade_score` INT UNSIGNED NOT NULL DEFAULT 0,
  `equip_slot` TINYINT UNSIGNED NOT NULL DEFAULT 255,
  `note` VARCHAR(255) NOT NULL DEFAULT '',
  `awarded_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_run` (`run_id`),
  KEY `idx_guid` (`guid`),
  KEY `idx_delivery` (`guid`,`delivery_state`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_player_invite` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `run_id` BIGINT UNSIGNED NOT NULL,
  `player_guid` INT UNSIGNED NOT NULL,
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 pending, 1 accepted, 2 declined, 3 invalid, 4 expired',
  `invited_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `expires_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `responded_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `inviter_name` VARCHAR(32) NOT NULL DEFAULT '',
  `note` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_run_player` (`run_id`,`player_guid`),
  KEY `idx_player_state` (`player_guid`,`state`),
  KEY `idx_state_expires` (`state`,`expires_at`)
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

-- Repair older installs by adding any missing columns/indexes. MySQL 5.7 compatible.
DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_column`;
DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_index`;
DELIMITER $$
CREATE PROCEDURE `playerbot_dungeon_sim_add_column`(
    IN p_table VARCHAR(64),
    IN p_column VARCHAR(64),
    IN p_definition TEXT
)
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = p_table
          AND COLUMN_NAME = p_column
    ) THEN
        SET @s = CONCAT('ALTER TABLE `', p_table, '` ADD COLUMN `', p_column, '` ', p_definition);
        PREPARE stmt FROM @s;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;
    END IF;
END$$
CREATE PROCEDURE `playerbot_dungeon_sim_add_index`(
    IN p_table VARCHAR(64),
    IN p_index VARCHAR(64),
    IN p_definition TEXT
)
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM INFORMATION_SCHEMA.STATISTICS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = p_table
          AND INDEX_NAME = p_index
    ) THEN
        SET @s = CONCAT('ALTER TABLE `', p_table, '` ADD ', p_definition);
        PREPARE stmt FROM @s;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;
    END IF;
END$$
DELIMITER ;

CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'real_group_created', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `bosses_killed`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'online_members_moved', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `real_group_created`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_map', 'INT UNSIGNED NOT NULL DEFAULT 0 AFTER `online_members_moved`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_x', 'FLOAT NOT NULL DEFAULT 0 AFTER `entrance_map`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_y', 'FLOAT NOT NULL DEFAULT 0 AFTER `entrance_x`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_z', 'FLOAT NOT NULL DEFAULT 0 AFTER `entrance_y`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_o', 'FLOAT NOT NULL DEFAULT 0 AFTER `entrance_z`');
CALL `playerbot_dungeon_sim_add_index`('playerbot_dungeon_run', 'idx_ends', 'KEY `idx_ends` (`ends_at`)');

CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'source_boss_order', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `source_boss`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'stored_online', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `equipped`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'delivery_state', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''0 pending, 1 failed, 2 delivered'' AFTER `stored_online`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'delivered_at', 'INT UNSIGNED NOT NULL DEFAULT 0 AFTER `delivery_state`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'upgrade_score', 'INT UNSIGNED NOT NULL DEFAULT 0 AFTER `delivered_at`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'equip_slot', 'TINYINT UNSIGNED NOT NULL DEFAULT 255 AFTER `upgrade_score`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'note', 'VARCHAR(255) NOT NULL DEFAULT '''' AFTER `equip_slot`');
CALL `playerbot_dungeon_sim_add_index`('playerbot_dungeon_loot_award', 'idx_delivery', 'KEY `idx_delivery` (`guid`, `delivery_state`)');

DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_index`;
DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_column`;
