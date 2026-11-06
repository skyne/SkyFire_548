#
# This file is part of Project SkyFire https://www.projectskyfire.org. 
# See COPYRIGHT file for Copyright information
#

set(scripts_STAT_SRCS
  ${scripts_STAT_SRCS}
  Pandaria/StormstoutBrewery/boss_hoptallus.cpp
  Pandaria/StormstoutBrewery/boss_ook_ook.cpp
  Pandaria/StormstoutBrewery/stormstout_brewery.cpp
  Pandaria/StormstoutBrewery/instance_stormstout_brewery.cpp
  Pandaria/StormstoutBrewery/stormstout_brewery.h
  Pandaria/TempleOfJadeSerpent/temple_of_jade_serpent.h
  Pandaria/TempleOfJadeSerpent/instance_temple_of_jade_serpent.cpp
  Pandaria/zone_valley_of_the_four_winds.cpp
  Pandaria/zone_wandering_island.cpp
  Pandaria/MoguShanVault/boss_elegon.cpp
  Pandaria/MoguShanVault/boss_feng.cpp
  Pandaria/MoguShanVault/boss_garajal.cpp
  Pandaria/MoguShanVault/boss_spirit_kings.cpp
  Pandaria/MoguShanVault/boss_stone_guard.cpp
  Pandaria/MoguShanVault/boss_will_of_emperor.cpp
  Pandaria/MoguShanVault/instance_mogu_shan_vault.cpp
  Pandaria/MoguShanVault/mogu_shan_vault.cpp
  Pandaria/MoguShanVault/mogu_shan_vault.h
  Pandaria/ScriptCompat/TaskScheduler.cpp
  Pandaria/ScriptCompat/pandaria_script_compat.cpp
  Pandaria/ScriptCompat/TaskScheduler.h
  Pandaria/ScriptCompat/Duration.h
  Pandaria/ScriptCompat/pandaria_script_compat.h
  Pandaria/HeartOfFear/boss_garalon.cpp
  Pandaria/HeartOfFear/boss_meljarak.cpp
  Pandaria/HeartOfFear/boss_shekzeer.cpp
  Pandaria/HeartOfFear/boss_tayak.cpp
  Pandaria/HeartOfFear/boss_unsok.cpp
  Pandaria/HeartOfFear/boss_zorlok.cpp
  Pandaria/HeartOfFear/heart_of_fear.cpp
  Pandaria/HeartOfFear/heart_of_fear.h
  Pandaria/HeartOfFear/instance_heart_of_fear.cpp
  Pandaria/TerraceOfEndlessSpring/boss_lei_shi.cpp
  Pandaria/TerraceOfEndlessSpring/boss_protectors_of_the_endless.cpp
  Pandaria/TerraceOfEndlessSpring/boss_sha_of_fear.cpp
  Pandaria/TerraceOfEndlessSpring/boss_tsulong.cpp
  Pandaria/TerraceOfEndlessSpring/instance_terrace_of_endless_spring.cpp
  Pandaria/TerraceOfEndlessSpring/terrace_of_endless_spring.cpp
  Pandaria/TerraceOfEndlessSpring/terrace_of_endless_spring.h
)

message("  -> Prepared: Pandaria")
