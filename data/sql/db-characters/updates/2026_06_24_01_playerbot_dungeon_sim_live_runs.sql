-- MySQL 5.7 / AzerothCore updater safe version.
-- Adds live-run columns only when missing.

DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_column`;
DELIMITER $$
CREATE PROCEDURE `playerbot_dungeon_sim_add_column`(
    IN p_table VARCHAR(64),
    IN p_column VARCHAR(64),
    IN p_definition TEXT
)
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM INFORMATION_SCHEMA.COLUMNS
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
DELIMITER ;

CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'real_group_created', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `bosses_killed`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'online_members_moved', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `real_group_created`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_map', 'INT UNSIGNED NOT NULL DEFAULT 0 AFTER `online_members_moved`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_x', 'FLOAT NOT NULL DEFAULT 0 AFTER `entrance_map`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_y', 'FLOAT NOT NULL DEFAULT 0 AFTER `entrance_x`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_z', 'FLOAT NOT NULL DEFAULT 0 AFTER `entrance_y`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_run', 'entrance_o', 'FLOAT NOT NULL DEFAULT 0 AFTER `entrance_z`');

DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_column`;
