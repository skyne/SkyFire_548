-- Upstream issue 1522. Each Outland dungeon has an in-instance graveyard in WorldSafeLocs.dbc,
-- but game_graveyard_zone never links it to the dungeon's root zone, so spirit release uses an outdoor graveyard.
-- Faction 0 is both teams. Coordinates were checked against WorldSafeLocs.dbc; only Hellfire Ramparts was confirmed in play.

DELETE FROM `game_graveyard_zone` WHERE `id` IN (3718, 3719, 3717, 3739, 3738, 3740, 3766, 3765, 3767, 3752, 3751, 3750, 3749);
INSERT INTO `game_graveyard_zone` (`id`, `ghost_zone`, `faction`) VALUES
(3718, 3714, 0), -- Shattered Halls
(3719, 3713, 0), -- Blood Furnace
(3717, 3562, 0), -- Hellfire Ramparts
(3739, 3715, 0), -- Steamvault
(3738, 3716, 0), -- Underbog
(3740, 3717, 0), -- Slave Pens
(3766, 3848, 0), -- The Mechanar
(3765, 3847, 0), -- The Botanica
(3767, 3849, 0), -- The Arcatraz
(3752, 3789, 0), -- Shadow Labyrinth
(3751, 3791, 0), -- Sethekk Halls
(3750, 3792, 0), -- Mana-Tombs
(3749, 3790, 0); -- Auchenai Crypts
