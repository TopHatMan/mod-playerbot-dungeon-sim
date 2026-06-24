ALTER TABLE `playerbot_dungeon_loot_award`
  ADD COLUMN IF NOT EXISTS `delivery_state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 pending, 1 failed, 2 delivered' AFTER `stored_online`,
  ADD COLUMN IF NOT EXISTS `delivered_at` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `delivery_state`,
  ADD COLUMN IF NOT EXISTS `upgrade_score` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `delivered_at`,
  ADD COLUMN IF NOT EXISTS `equip_slot` TINYINT UNSIGNED NOT NULL DEFAULT 255 AFTER `upgrade_score`,
  ADD KEY IF NOT EXISTS `idx_delivery` (`guid`, `delivery_state`);

UPDATE `playerbot_dungeon_loot_award`
SET `delivery_state` = CASE WHEN `stored_online` = 1 THEN 2 ELSE 0 END,
    `delivered_at` = CASE WHEN `stored_online` = 1 THEN `awarded_at` ELSE 0 END
WHERE `delivery_state` = 0;
