-- Force world install for mod-playerbot-dungeon-sim.
-- This is intentionally in updates/ so AzerothCore auto-updater creates/repairs the tables even if base SQL was skipped.

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
  KEY `idx_map` (`map_id`),
  KEY `idx_phase` (`min_phase`,`max_phase`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_boss_template` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `boss_order` TINYINT UNSIGNED NOT NULL,
  `boss_name` VARCHAR(100) NOT NULL,
  `creature_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_dungeon_order` (`dungeon_template_id`,`boss_order`),
  KEY `idx_creature` (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `playerbot_dungeon_template`
(`id`,`name`,`map_id`,`difficulty`,`min_level`,`max_level`,`target_level`,`min_phase`,`max_phase`,`group_size`,`is_raid`,`entrance_map`,`entrance_x`,`entrance_y`,`entrance_z`,`entrance_o`) VALUES
(1,'Ragefire Chasm',389,0,13,18,15,0,18,5,0,1,1811.78,-4410.50,-18.47,5.20),
(2,'Wailing Caverns',43,0,17,24,20,0,18,5,0,1,-740.00,-2214.00,16.00,2.70),
(3,'Deadmines',36,0,15,24,20,0,18,5,0,0,-11209.6,1666.54,24.70,1.42),
(4,'Shadowfang Keep',33,0,20,30,25,0,18,5,0,0,-234.15,1561.63,76.89,1.24),
(5,'Blackfathom Deeps',48,0,22,30,26,0,18,5,0,1,4248.00,745.00,-24.50,1.34),
(6,'The Stockade',34,0,22,32,26,0,18,5,0,0,-8765.00,846.00,87.00,0.70),
(7,'Gnomeregan',90,0,28,38,32,0,18,5,0,0,-5163.54,925.42,257.18,1.57),
(8,'Razorfen Kraul',47,0,28,38,32,0,18,5,0,1,-4463.00,-1664.00,82.00,0.85),
(9,'Scarlet Monastery Graveyard',189,0,28,38,32,0,18,5,0,0,2870.90,-819.29,160.33,5.33),
(10,'Scarlet Monastery Library',189,0,32,40,36,0,18,5,0,0,2870.90,-819.29,160.33,5.33),
(11,'Scarlet Monastery Armory',189,0,35,42,38,0,18,5,0,0,2870.90,-819.29,160.33,5.33),
(12,'Scarlet Monastery Cathedral',189,0,38,45,42,0,18,5,0,0,2870.90,-819.29,160.33,5.33),
(13,'Razorfen Downs',129,0,37,46,42,0,18,5,0,1,-4657.00,-2519.00,81.00,4.00),
(14,'Uldaman',70,0,40,48,44,0,18,5,0,0,-6066.00,-2955.00,209.00,0.00),
(15,'Zul''Farrak',209,0,44,54,48,0,18,5,0,1,-6796.49,-2890.77,9.00,3.10),
(16,'Maraudon',349,0,46,55,50,1,18,5,0,1,-1419.00,2908.00,137.00,1.40),
(17,'Sunken Temple',109,0,50,56,54,0,18,5,0,0,-10177.90,-3995.87,-111.24,6.02),
(18,'Blackrock Depths',230,0,52,60,56,0,18,5,0,0,-7179.34,-921.21,165.82,5.09),
(19,'Lower Blackrock Spire',229,0,55,60,58,0,18,5,0,0,-7527.05,-1226.77,285.73,5.31),
(20,'Upper Blackrock Spire',229,0,58,60,60,0,18,10,0,0,-7527.05,-1226.77,285.73,5.31),
(21,'Dire Maul East',429,0,55,60,58,2,18,5,0,1,-3738.00,934.00,160.00,3.10),
(22,'Dire Maul West',429,0,56,60,58,2,18,5,0,1,-3828.00,1250.00,160.00,3.10),
(23,'Dire Maul North',429,0,57,60,60,2,18,5,0,1,-3520.00,1070.00,160.00,3.10),
(24,'Scholomance',289,0,58,60,60,0,18,5,0,0,1269.64,-2556.21,94.13,0.50),
(25,'Stratholme',329,0,58,60,60,0,18,5,0,0,3352.92,-3379.03,144.78,6.26),
(100,'Molten Core',409,0,60,60,60,0,18,40,1,0,-7515.0,-1045.0,182.3,0.0),
(101,'Onyxia''s Lair',249,0,60,60,60,0,18,40,1,1,-4708.27,-3727.64,54.56,3.80),
(102,'Blackwing Lair',469,0,60,60,60,4,18,40,1,0,-7665.55,-1102.49,399.67,0.72),
(103,'Zul''Gurub',309,0,60,60,60,5,18,20,1,0,-11916.7,-1208.37,92.29,4.72),
(104,'Ruins of Ahn''Qiraj',509,0,60,60,60,6,18,20,1,1,-8409.82,1498.06,27.72,2.61),
(105,'Temple of Ahn''Qiraj',531,0,60,60,60,6,18,40,1,1,-8240.09,1991.32,129.07,0.94),
(106,'Naxxramas 40',533,2,60,60,60,7,18,40,1,0,3125.18,-3748.02,136.05,0.0)
ON DUPLICATE KEY UPDATE
`name`=VALUES(`name`),`map_id`=VALUES(`map_id`),`difficulty`=VALUES(`difficulty`),`min_level`=VALUES(`min_level`),`max_level`=VALUES(`max_level`),`target_level`=VALUES(`target_level`),`min_phase`=VALUES(`min_phase`),`max_phase`=VALUES(`max_phase`),`group_size`=VALUES(`group_size`),`is_raid`=VALUES(`is_raid`),`entrance_map`=VALUES(`entrance_map`),`entrance_x`=VALUES(`entrance_x`),`entrance_y`=VALUES(`entrance_y`),`entrance_z`=VALUES(`entrance_z`),`entrance_o`=VALUES(`entrance_o`);

INSERT INTO `playerbot_dungeon_boss_template` (`dungeon_template_id`,`boss_order`,`boss_name`,`creature_entry`) VALUES
(1,1,'Oggleflint',11517),(1,2,'Jergosh the Invoker',11518),(1,3,'Bazzalan',11519),(1,4,'Taragaman the Hungerer',11520),
(2,1,'Kresh',3653),(2,2,'Lady Anacondra',3671),(2,3,'Lord Cobrahn',3669),(2,4,'Lord Pythas',3670),(2,5,'Skum',3674),(2,6,'Lord Serpentis',3673),(2,7,'Verdan the Everliving',5775),(2,8,'Mutanus the Devourer',3654),
(3,1,'Rhahk''Zor',644),(3,2,'Sneed',643),(3,3,'Gilnid',1763),(3,4,'Mr. Smite',646),(3,5,'Cookie',645),(3,6,'Edwin VanCleef',639),
(4,1,'Rethilgore',3914),(4,2,'Baron Silverlaine',3887),(4,3,'Commander Springvale',4278),(4,4,'Odo the Blindwatcher',4279),(4,5,'Fenrus the Devourer',4274),(4,6,'Archmage Arugal',4275),
(5,1,'Ghamoo-ra',4887),(5,2,'Lady Sarevess',4831),(5,3,'Gelihast',6243),(5,4,'Lorgus Jett',12902),(5,5,'Baron Aquanis',12876),(5,6,'Twilight Lord Kelris',4832),(5,7,'Aku''mai',4829),
(6,1,'Targorr the Dread',1696),(6,2,'Kam Deepfury',1666),(6,3,'Hamhock',1717),(6,4,'Bazil Thredd',1716),
(7,1,'Grubbis',7361),(7,2,'Viscous Fallout',7079),(7,3,'Electrocutioner 6000',6235),(7,4,'Crowd Pummeler 9-60',6229),(7,5,'Dark Iron Ambassador',6228),(7,6,'Mekgineer Thermaplugg',7800),
(8,1,'Roogug',6168),(8,2,'Aggem Thorncurse',4424),(8,3,'Death Speaker Jargba',4428),(8,4,'Overlord Ramtusk',4420),(8,5,'Agathelos the Raging',4422),(8,6,'Charlga Razorflank',4421),
(9,1,'Interrogator Vishas',3983),(9,2,'Bloodmage Thalnos',4543),
(10,1,'Houndmaster Loksey',3974),(10,2,'Arcanist Doan',6487),
(11,1,'Herod',3975),
(12,1,'High Inquisitor Fairbanks',4542),(12,2,'Scarlet Commander Mograine',3976),(12,3,'High Inquisitor Whitemane',3977),
(13,1,'Tuten''kash',7355),(13,2,'Mordresh Fire Eye',7357),(13,3,'Glutton',8567),(13,4,'Ragglesnout',7354),(13,5,'Amnennar the Coldbringer',7358),
(14,1,'Revelosh',6910),(14,2,'Ironaya',7228),(14,3,'Ancient Stone Keeper',7206),(14,4,'Galgann Firehammer',7291),(14,5,'Grimlok',4854),(14,6,'Archaedas',2748),
(15,1,'Antu''sul',8127),(15,2,'Theka the Martyr',7272),(15,3,'Witch Doctor Zum''rah',7271),(15,4,'Nekrum Gutchewer',7796),(15,5,'Shadowpriest Sezz''ziz',7275),(15,6,'Sergeant Bly',7604),(15,7,'Chief Ukorz Sandscalp',7267),(15,8,'Gahz''rilla',7273),
(16,1,'Noxxion',13282),(16,2,'Razorlash',12258),(16,3,'Lord Vyletongue',12236),(16,4,'Celebras the Cursed',12225),(16,5,'Landslide',12203),(16,6,'Tinkerer Gizlock',13601),(16,7,'Rotgrip',13596),(16,8,'Princess Theradras',12201),
(17,1,'Atal''alarion',8580),(17,2,'Dreamscythe',5721),(17,3,'Weaver',5720),(17,4,'Jammal''an the Prophet',5710),(17,5,'Ogom the Wretched',5711),(17,6,'Morphaz',5719),(17,7,'Hazzas',5722),(17,8,'Shade of Eranikus',5709),
(18,1,'Lord Roccor',9025),(18,2,'High Interrogator Gerstahn',9018),(18,3,'Houndmaster Grebmar',9319),(18,4,'Pyromancer Loregrain',9024),(18,5,'General Angerforge',9033),(18,6,'Golem Lord Argelmach',8983),(18,7,'Hurley Blackbreath',9537),(18,8,'Phalanx',9502),(18,9,'Ambassador Flamelash',9156),(18,10,'The Seven',9034),(18,11,'Magmus',9938),(18,12,'Emperor Dagran Thaurissan',9019),
(19,1,'Highlord Omokk',9196),(19,2,'Shadow Hunter Vosh''gajin',9236),(19,3,'War Master Voone',9237),(19,4,'Mother Smolderweb',10596),(19,5,'Urok Doomhowl',10584),(19,6,'Quartermaster Zigris',9736),(19,7,'Halycon',10220),(19,8,'Gizrul the Slavener',10268),(19,9,'Overlord Wyrmthalak',9568),
(20,1,'Pyroguard Emberseer',9816),(20,2,'Solakar Flamewreath',10264),(20,3,'Goraluk Anvilcrack',10899),(20,4,'Warchief Rend Blackhand',10429),(20,5,'The Beast',10430),(20,6,'General Drakkisath',10363),
(21,1,'Pusillin',14354),(21,2,'Zevrim Thornhoof',11490),(21,3,'Hydrospawn',13280),(21,4,'Lethtendris',14327),(21,5,'Alzzin the Wildshaper',11492),
(22,1,'Tendris Warpwood',11489),(22,2,'Illyanna Ravenoak',11488),(22,3,'Magister Kalendris',11487),(22,4,'Immol''thar',11496),(22,5,'Prince Tortheldrin',11486),
(23,1,'Guard Mol''dar',14326),(23,2,'Stomper Kreeg',14322),(23,3,'Guard Fengus',14321),(23,4,'Guard Slip''kik',14323),(23,5,'Captain Kromcrush',14325),(23,6,'Cho''Rush the Observer',14324),(23,7,'King Gordok',11501),
(24,1,'Kirtonos the Herald',10506),(24,2,'Jandice Barov',10503),(24,3,'Rattlegore',11622),(24,4,'Marduk Blackpool',10433),(24,5,'Vectus',10432),(24,6,'Ras Frostwhisper',10508),(24,7,'Instructor Malicia',10505),(24,8,'Doctor Theolen Krastinov',11261),(24,9,'Lorekeeper Polkelt',10901),(24,10,'The Ravenian',10507),(24,11,'Lord Alexei Barov',10504),(24,12,'Lady Illucia Barov',10502),(24,13,'Darkmaster Gandling',1853),
(25,1,'Hearthsinger Forresten',10558),(25,2,'The Unforgiven',10516),(25,3,'Timmy the Cruel',10808),(25,4,'Cannon Master Willey',10997),(25,5,'Archivist Galford',10811),(25,6,'Balnazzar',10813),(25,7,'Baroness Anastari',10436),(25,8,'Nerub''enkan',10437),(25,9,'Maleki the Pallid',10438),(25,10,'Magistrate Barthilas',10435),(25,11,'Ramstein the Gorger',10439),(25,12,'Baron Rivendare',10440),
(100,1,'Lucifron',12118),(100,2,'Magmadar',11982),(100,3,'Gehennas',12259),(100,4,'Garr',12057),(100,5,'Baron Geddon',12056),(100,6,'Shazzrah',12264),(100,7,'Sulfuron Harbinger',12098),(100,8,'Golemagg the Incinerator',11988),(100,9,'Majordomo Executus',12018),(100,10,'Ragnaros',11502),
(101,1,'Onyxia',10184),
(102,1,'Razorgore the Untamed',12435),(102,2,'Vaelastrasz the Corrupt',13020),(102,3,'Broodlord Lashlayer',12017),(102,4,'Firemaw',11983),(102,5,'Ebonroc',14601),(102,6,'Flamegor',11981),(102,7,'Chromaggus',14020),(102,8,'Nefarian',11583),
(103,1,'High Priest Venoxis',14507),(103,2,'High Priestess Jeklik',14517),(103,3,'High Priestess Mar''li',14510),(103,4,'Bloodlord Mandokir',11382),(103,5,'Gahz''ranka',15114),(103,6,'High Priest Thekal',14509),(103,7,'High Priestess Arlokk',14515),(103,8,'Jin''do the Hexxer',11380),(103,9,'Hakkar',14834),
(104,1,'Kurinnaxx',15348),(104,2,'General Rajaxx',15341),(104,3,'Moam',15340),(104,4,'Buru the Gorger',15370),(104,5,'Ayamiss the Hunter',15369),(104,6,'Ossirian the Unscarred',15339),
(105,1,'The Prophet Skeram',15263),(105,2,'Bug Trio',15544),(105,3,'Battleguard Sartura',15516),(105,4,'Fankriss the Unyielding',15510),(105,5,'Viscidus',15299),(105,6,'Princess Huhuran',15509),(105,7,'Twin Emperors',15276),(105,8,'Ouro',15517),(105,9,'C''Thun',15727),
(106,1,'Anub''Rekhan',15956),(106,2,'Grand Widow Faerlina',15953),(106,3,'Maexxna',15952),(106,4,'Noth the Plaguebringer',15954),(106,5,'Heigan the Unclean',15936),(106,6,'Loatheb',16011),(106,7,'Instructor Razuvious',16061),(106,8,'Gothik the Harvester',16060),(106,9,'The Four Horsemen',16063),(106,10,'Patchwerk',16028),(106,11,'Grobbulus',15931),(106,12,'Gluth',15932),(106,13,'Thaddius',15928),(106,14,'Sapphiron',15989),(106,15,'Kel''Thuzad',15990)
ON DUPLICATE KEY UPDATE `boss_name`=VALUES(`boss_name`), `creature_entry`=VALUES(`creature_entry`);

CREATE TABLE IF NOT EXISTS `playerbot_dungeon_waypoint` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `dungeon_template_id` INT UNSIGNED NOT NULL,
  `step_order` SMALLINT UNSIGNED NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `position_x` FLOAT NOT NULL,
  `position_y` FLOAT NOT NULL,
  `position_z` FLOAT NOT NULL,
  `orientation` FLOAT NOT NULL DEFAULT 0,
  `label` VARCHAR(100) NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_dungeon_step` (`dungeon_template_id`,`step_order`),
  KEY `idx_dungeon_enabled` (`dungeon_template_id`,`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
