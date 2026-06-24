CREATE TABLE IF NOT EXISTS `playerbot_dungeon_template` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `name` VARCHAR(100) NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `difficulty` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `min_level` TINYINT UNSIGNED NOT NULL,
  `max_level` TINYINT UNSIGNED NOT NULL,
  `target_level` TINYINT UNSIGNED NOT NULL,
  `min_phase` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `max_phase` TINYINT UNSIGNED NOT NULL DEFAULT 18,
  `group_size` TINYINT UNSIGNED NOT NULL DEFAULT 5,
  `is_raid` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `entrance_map` SMALLINT UNSIGNED NOT NULL,
  `entrance_x` FLOAT NOT NULL,
  `entrance_y` FLOAT NOT NULL,
  `entrance_z` FLOAT NOT NULL,
  `entrance_o` FLOAT NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_level` (`min_level`,`max_level`),
  KEY `idx_map` (`map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_boss_template` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `boss_order` TINYINT UNSIGNED NOT NULL,
  `boss_name` VARCHAR(100) NOT NULL,
  `creature_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_dungeon_order` (`dungeon_template_id`,`boss_order`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `playerbot_dungeon_template`
(`id`,`name`,`map_id`,`difficulty`,`min_level`,`max_level`,`target_level`,`min_phase`,`max_phase`,`group_size`,`is_raid`,`entrance_map`,`entrance_x`,`entrance_y`,`entrance_z`,`entrance_o`) VALUES
(1,'Ragefire Chasm',389,0,13,18,15,0,18,5,0,1,1811.78,-4410.50,-18.47,5.20),
(2,'Wailing Caverns',43,0,17,24,20,0,18,5,0,1,-740.00,-2214.00,16.00,2.70),
(3,'Deadmines',36,0,17,24,20,0,18,5,0,0,-11209.6,1666.54,24.70,1.42),
(4,'Shadowfang Keep',33,0,22,30,25,0,18,5,0,0,-234.15,1561.63,76.89,1.24),
(5,'Scarlet Monastery Graveyard',189,0,28,38,32,0,18,5,0,0,2870.90,-819.29,160.33,5.33),
(6,'Zul Farrak',209,0,44,54,48,0,18,5,0,1,-6796.49,-2890.77,9.00,3.10),
(7,'Blackrock Depths',230,0,52,60,56,0,18,5,0,0,-7179.34,-921.21,165.82,5.09),
(100,'Molten Core',409,0,60,60,60,0,18,40,1,0,-7515.0,-1045.0,182.3,0.0);

INSERT IGNORE INTO `playerbot_dungeon_boss_template` (`dungeon_template_id`,`boss_order`,`boss_name`,`creature_entry`) VALUES
(100,1,'Lucifron',12118),(100,2,'Magmadar',11982),(100,3,'Gehennas',12259),(100,4,'Garr',12057),(100,5,'Baron Geddon',12056),(100,6,'Shazzrah',12264),(100,7,'Sulfuron Harbinger',12098),(100,8,'Golemagg the Incinerator',11988),(100,9,'Majordomo Executus',12018),(100,10,'Ragnaros',11502);

-- Vanilla raid ladder. These are gated by min_phase/max_phase and by the realm MaxPlayerLevel cap.
-- Phase map used by this module: 0 MC/Onyxia, 4 BWL, 5 ZG, 6 AQ20/AQ40, 7 Naxx40.
INSERT IGNORE INTO `playerbot_dungeon_template`
(`id`,`name`,`map_id`,`difficulty`,`min_level`,`max_level`,`target_level`,`min_phase`,`max_phase`,`group_size`,`is_raid`,`entrance_map`,`entrance_x`,`entrance_y`,`entrance_z`,`entrance_o`) VALUES
(101,'Onyxia''s Lair',249,0,60,60,60,0,18,40,1,1,-4708.27,-3727.64,54.56,3.80),
(102,'Blackwing Lair',469,0,60,60,60,4,18,40,1,0,-7665.55,-1102.49,399.67,0.72),
(103,'Zul''Gurub',309,0,60,60,60,5,18,20,1,0,-11916.7,-1208.37,92.29,4.72),
(104,'Ruins of Ahn''Qiraj',509,0,60,60,60,6,18,20,1,1,-8409.82,1498.06,27.72,2.61),
(105,'Temple of Ahn''Qiraj',531,0,60,60,60,6,18,40,1,1,-8240.09,1991.32,129.07,0.94),
(106,'Naxxramas 40',533,2,60,60,60,7,18,40,1,0,3125.18,-3748.02,136.05,0.0);

INSERT IGNORE INTO `playerbot_dungeon_boss_template` (`dungeon_template_id`,`boss_order`,`boss_name`,`creature_entry`) VALUES
(101,1,'Onyxia',10184),

(102,1,'Razorgore the Untamed',12435),(102,2,'Vaelastrasz the Corrupt',13020),(102,3,'Broodlord Lashlayer',12017),(102,4,'Firemaw',11983),(102,5,'Ebonroc',14601),(102,6,'Flamegor',11981),(102,7,'Chromaggus',14020),(102,8,'Nefarian',11583),

(103,1,'High Priest Venoxis',14507),(103,2,'High Priestess Jeklik',14517),(103,3,'High Priestess Mar''li',14510),(103,4,'Bloodlord Mandokir',11382),(103,5,'Gahz''ranka',15114),(103,6,'High Priest Thekal',14509),(103,7,'High Priestess Arlokk',14515),(103,8,'Jin''do the Hexxer',11380),(103,9,'Hakkar',14834),

(104,1,'Kurinnaxx',15348),(104,2,'General Rajaxx',15341),(104,3,'Moam',15340),(104,4,'Buru the Gorger',15370),(104,5,'Ayamiss the Hunter',15369),(104,6,'Ossirian the Unscarred',15339),

(105,1,'The Prophet Skeram',15263),(105,2,'Bug Trio',15544),(105,3,'Battleguard Sartura',15516),(105,4,'Fankriss the Unyielding',15510),(105,5,'Viscidus',15299),(105,6,'Princess Huhuran',15509),(105,7,'Twin Emperors',15276),(105,8,'Ouro',15517),(105,9,'C''Thun',15727),

(106,1,'Anub''Rekhan',15956),(106,2,'Grand Widow Faerlina',15953),(106,3,'Maexxna',15952),(106,4,'Noth the Plaguebringer',15954),(106,5,'Heigan the Unclean',15936),(106,6,'Loatheb',16011),(106,7,'Instructor Razuvious',16061),(106,8,'Gothik the Harvester',16060),(106,9,'The Four Horsemen',16063),(106,10,'Patchwerk',16028),(106,11,'Grobbulus',15931),(106,12,'Gluth',15932),(106,13,'Thaddius',15928),(106,14,'Sapphiron',15989),(106,15,'Kel''Thuzad',15990);
