-- Upstream issues verified against the shipped world data.
-- 1553: Disengage (781) also casts linked spell 56446, which leaps forward and cancels the backward leap.
-- 1508: Billibub Cogspinner (menu 698) shows a vendor line that has no vendor action.
-- 1483: Nipsy (menu 4845) has the same missing vendor action.
-- OptionType 3 is GOSSIP_OPTION_VENDOR, OptionNpcflag 128 is UNIT_NPC_FLAG_VENDOR.

DELETE FROM `spell_linked_spell`
WHERE `spell_trigger` = 781
  AND `spell_effect` = 56446
  AND `type` = 0;

UPDATE `gossip_menu_option`
SET `OptionType` = 3,
    `OptionNpcflag` = 128
WHERE (`MenuID` = 698 AND `OptionID` = 0)
   OR (`MenuID` = 4845 AND `OptionID` = 0);
