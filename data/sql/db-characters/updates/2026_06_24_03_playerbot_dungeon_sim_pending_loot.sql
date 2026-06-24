-- MySQL 5.7 / AzerothCore updater safe version.
-- Adds pending-loot delivery fields and index only when missing.

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

CREATE PROCEDURE `playerbot_dungeon_sim_add_index`(
    IN p_table VARCHAR(64),
    IN p_index VARCHAR(64),
    IN p_definition TEXT
)
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM INFORMATION_SCHEMA.STATISTICS
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

CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'delivery_state', 'TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''0 pending, 1 failed, 2 delivered'' AFTER `stored_online`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'delivered_at', 'INT UNSIGNED NOT NULL DEFAULT 0 AFTER `delivery_state`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'upgrade_score', 'INT UNSIGNED NOT NULL DEFAULT 0 AFTER `delivered_at`');
CALL `playerbot_dungeon_sim_add_column`('playerbot_dungeon_loot_award', 'equip_slot', 'TINYINT UNSIGNED NOT NULL DEFAULT 255 AFTER `upgrade_score`');
CALL `playerbot_dungeon_sim_add_index`('playerbot_dungeon_loot_award', 'idx_delivery', 'KEY `idx_delivery` (`guid`, `delivery_state`)');

DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_index`;
DROP PROCEDURE IF EXISTS `playerbot_dungeon_sim_add_column`;
