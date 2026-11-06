-- Mogu'shan Vaults (map 1008). Encounter scripts ported from MoPCore.
-- Boss and trash rows that already had the matching scriptname are left alone.
-- These are the templates that were still empty, plus the Spirit Kings controller,
-- which was bound to the flanking adds instead of the controller.

UPDATE `instance_template`
SET `script` = 'instance_mogu_shan_vault'
WHERE `map` = 1008;

UPDATE `creature_template` SET `scriptname` = 'boss_jin_qin_xi' WHERE `entry` IN (60399, 60400);
UPDATE `creature_template` SET `scriptname` = 'mob_living_crystal' WHERE `entry` IN (60304, 60306, 60307, 60308);
UPDATE `creature_template` SET `scriptname` = 'mob_woe_add_generic' WHERE `entry` IN (60396, 60397, 60398);
UPDATE `creature_template` SET `scriptname` = 'mob_woe_titan_spark' WHERE `entry` = 60480;
UPDATE `creature_template` SET `scriptname` = 'mob_siphon_shield' WHERE `entry` = 60627;
UPDATE `creature_template` SET `scriptname` = 'mob_ancient_mogu_machine' WHERE `entry` = 60648;
UPDATE `creature_template` SET `scriptname` = 'mob_soul_fragment' WHERE `entry` = 60781;
UPDATE `creature_template` SET `scriptname` = 'boss_spirit_kings_controler' WHERE `entry` = 60984;
UPDATE `creature_template` SET `scriptname` = '' WHERE `entry` = 60847 AND `scriptname` = 'boss_spirit_kings_controler';
UPDATE `creature_template` SET `scriptname` = 'mob_stone_quilen' WHERE `entry` IN (60576, 60584);
UPDATE `creature_template` SET `scriptname` = 'mob_zandalari_skullcharger' WHERE `entry` IN (60375, 60377);
UPDATE `creature_template` SET `scriptname` = 'mob_mogu_secret_keeper' WHERE `entry` IN (61131, 61135);
UPDATE `creature_template` SET `scriptname` = 'mob_mogu_engine_keeper' WHERE `entry` = 64068;
UPDATE `creature_template` SET `scriptname` = 'mob_mogu_warden' WHERE `entry` IN (64061, 64947, 64963);
UPDATE `creature_template` SET `scriptname` = 'mob_tiling_creature' WHERE `entry` = 62026;
UPDATE `creature_template` SET `scriptname` = 'mob_general_purpose_bunnyJMF' WHERE `entry` = 55091;
