-- Challenge-mode money, valor, title, and achievement ids.
-- CREATE and INSERT from alexkulya/pandaria_5.4.8 sql/base/world_04_03_2023.zip
-- table `challenge_mode_rewards`. Legends of Azeroth has no copy of this table
-- in sql/updates or sql/base. No table is named leaderboard.
-- SkyFire adaptation: CREATE IF NOT EXISTS, and the insert ignores a row that already exists.

CREATE TABLE IF NOT EXISTS `challenge_mode_rewards` (
  `map` int(10) unsigned NOT NULL DEFAULT '0',
  `none_money` int(10) unsigned NOT NULL DEFAULT '0',
  `bronze_money` int(10) unsigned NOT NULL DEFAULT '0',
  `silver_money` int(10) unsigned NOT NULL DEFAULT '0',
  `gold_money` int(10) unsigned NOT NULL DEFAULT '0',
  `none_valor` int(10) unsigned NOT NULL DEFAULT '0',
  `bronze_valor` int(10) unsigned NOT NULL DEFAULT '0',
  `silver_valor` int(10) unsigned NOT NULL DEFAULT '0',
  `gold_valor` int(10) unsigned NOT NULL DEFAULT '0',
  `title` int(10) unsigned NOT NULL DEFAULT '0',
  `platinum_achievement` int(10) unsigned NOT NULL DEFAULT '0',
  `comment` text NOT NULL,
  PRIMARY KEY (`map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

INSERT IGNORE INTO `challenge_mode_rewards` (`map`, `none_money`, `bronze_money`, `silver_money`, `gold_money`, `none_valor`, `bronze_valor`, `silver_valor`, `gold_valor`, `title`, `platinum_achievement`, `comment`) VALUES
(959, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 371, 8432, 'Shado-Pan Monastery'),
(960, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 374, 8430, 'Temple of the Jade Serpent'),
(961, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 373, 8431, 'Stormstout Brewery'),
(962, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 366, 8434, 'Gate of the Setting Sun'),
(994, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 367, 8433, 'Mogu''shan Palace'),
(1001, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 368, 8436, 'Scarlet Halls'),
(1004, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 369, 8437, 'Scarlet Monastery'),
(1007, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 370, 8438, 'Scholomance'),
(1011, 114000, 228000, 342000, 456000, 0, 12500, 15000, 16500, 372, 8439, 'Siege of Niuzao Temple');
