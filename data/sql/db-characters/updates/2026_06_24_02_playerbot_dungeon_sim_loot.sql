-- MySQL 5.7 / AzerothCore updater safe version.
-- Adds loot-award columns only when missing.

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

CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'source_boss_order', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `source_boss`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'stored_online', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `equipped`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'note', 'VARCHAR(255) NOT NULL DEFAULT '''' AFTER `stored_online`');

DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_column`;
