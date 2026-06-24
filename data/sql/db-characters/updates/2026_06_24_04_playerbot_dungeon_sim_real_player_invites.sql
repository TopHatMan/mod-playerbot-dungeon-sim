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
