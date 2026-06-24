ALTER TABLE `playerbot_dungeon_run`
  ADD COLUMN IF NOT EXISTS `real_group_created` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `bosses_killed`,
  ADD COLUMN IF NOT EXISTS `online_members_moved` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `real_group_created`,
  ADD COLUMN IF NOT EXISTS `entrance_map` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `online_members_moved`,
  ADD COLUMN IF NOT EXISTS `entrance_x` FLOAT NOT NULL DEFAULT 0 AFTER `entrance_map`,
  ADD COLUMN IF NOT EXISTS `entrance_y` FLOAT NOT NULL DEFAULT 0 AFTER `entrance_x`,
  ADD COLUMN IF NOT EXISTS `entrance_z` FLOAT NOT NULL DEFAULT 0 AFTER `entrance_y`,
  ADD COLUMN IF NOT EXISTS `entrance_o` FLOAT NOT NULL DEFAULT 0 AFTER `entrance_z`;
