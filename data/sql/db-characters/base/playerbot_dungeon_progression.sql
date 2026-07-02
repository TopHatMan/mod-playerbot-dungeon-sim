-- mod-playerbot-dungeon-sim — per-bot progression ladder.
--
-- One row per bot character. level_cap is the current cap tier (60/70/80) and
-- `tier` is the content step within that cap (0 = the level-60 5-man dungeons;
-- higher tiers are the raid ladder, added next). clears_in_tier counts completed
-- runs toward the next tier (ClearsToAdvanceTier in the conf).
--
-- Apply to the CHARACTERS database (same DB as `characters`).

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_progression` (
  `guid`           INT UNSIGNED    NOT NULL,
  `level_cap`      TINYINT UNSIGNED NOT NULL DEFAULT 60,
  `tier`           INT UNSIGNED    NOT NULL DEFAULT 0,
  `clears_in_tier` INT UNSIGNED    NOT NULL DEFAULT 0,
  `updated`        INT UNSIGNED    NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;