/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "RaidScale.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "Util.h"
#include "World.h"

namespace
{
    uint32 MapCap(InstanceMap const* instance)
    {
        uint32 cap = instance->GetMaxPlayers();
        if (!cap)
            cap = instance->Is25ManRaid() ? 25 : 10;
        return cap;
    }

    uint32 LivePlayers(InstanceMap const* instance)
    {
        uint32 cap = MapCap(instance);
        uint32 floor = std::min<uint32>(5, cap);
        uint32 players = instance->GetPlayersCountExceptGMs();
        if (players < floor)
            players = floor;
        if (players > cap)
            players = cap;
        return players;
    }

    bool IsPlayerOwned(Creature const* creature)
    {
        return creature->IsPet() || creature->GetCharmerOrOwnerPlayerOrPlayerItself() != NULL;
    }

    bool IsEncounterBoss(Creature const* creature)
    {
        if (IsPlayerOwned(creature))
            return false;

        if (creature->IsDungeonBoss() || creature->isWorldBoss())
            return true;

        CreatureTemplate const* info = creature->GetCreatureTemplate();
        return info && info->rank == CREATURE_ELITE_WORLDBOSS;
    }

    InstanceMap const* RaidInstance(Map const* map)
    {
        if (!map || !map->IsRaid() || !sWorld->GetBoolConfig(WorldBoolConfigs::CONFIG_RAID_FLEX_SCALE))
            return NULL;
        return map->ToInstanceMap();
    }

    uint32 PlayersFor(InstanceMap const* instance)
    {
        if (instance->IsRaidScaleLocked())
            return std::max<uint32>(1, instance->GetRaidScalePlayers());
        return LivePlayers(instance);
    }

    uint32 ScaleAmount(uint32 amount, uint32 players, uint32 cap)
    {
        if (!amount || !cap || players >= cap)
            return amount;
        return std::max<uint32>(1, uint32((uint64(amount) * players + cap / 2) / cap));
    }
}

namespace RaidScale
{
    float Multiplier(Map const* map)
    {
        InstanceMap const* instance = RaidInstance(map);
        if (!instance)
            return 1.0f;
        return float(PlayersFor(instance)) / float(MapCap(instance));
    }

    uint32 ScaleTargetCap(Unit const* caster, uint32 cap)
    {
        if (!cap || !caster)
            return cap;

        Creature const* creature = caster->ToCreature();
        if (!creature || IsPlayerOwned(creature))
            return cap;

        InstanceMap const* instance = RaidInstance(creature->GetMap());
        if (!instance)
            return cap;

        uint32 players = PlayersFor(instance);
        uint32 mapCap = MapCap(instance);
        if (!mapCap || players >= mapCap)
            return cap;

        float expected = float(cap) * float(players) / float(mapCap);
        uint32 whole = uint32(expected);
        float remain = expected - float(whole);
        if (remain > 0.0f && roll_chance_f(remain * 100.0f))
            ++whole;
        return whole;
    }

    void ApplyHealth(Creature* creature)
    {
        if (!creature || !creature->IsAlive() || IsPlayerOwned(creature))
            return;

        InstanceMap const* instance = RaidInstance(creature->GetMap());
        if (!instance)
            return;

        uint32 base = creature->GetCreateHealth();
        if (!base)
            return;

        uint32 scaled = ScaleAmount(base, PlayersFor(instance), MapCap(instance));
        if (creature->GetMaxHealth() == scaled)
            return;

        // Guardian::UpdateMaxHealth adds CreateHealth on top of BASE_VALUE.
        float baseValue = float(scaled);
        if (creature->IsGuardian())
            baseValue = scaled > base ? float(scaled - base) : 0.0f;

        creature->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, baseValue);
        creature->UpdateMaxHealth();
        if (!creature->IsInCombat())
            creature->SetHealth(creature->GetMaxHealth());
    }

    void ApplyHealthThunk(Creature* creature, void* /*data*/)
    {
        ApplyHealth(creature);
    }

    void NoteBossStillFighting(Creature* creature, void* data)
    {
        if (*static_cast<bool*>(data))
            return;
        if (creature && creature->IsInCombat() && IsEncounterBoss(creature))
            *static_cast<bool*>(data) = true;
    }

    void OnPlayersChanged(Map* map)
    {
        if (!RaidInstance(map))
            return;
        if (map->ToInstanceMap()->IsRaidScaleLocked())
            return;

        map->ForEachCreature(ApplyHealthThunk, NULL);
    }

    void OnCreatureEnterCombat(Creature* creature)
    {
        if (!creature || !IsEncounterBoss(creature))
            return;

        InstanceMap* instance = creature->GetMap() ? creature->GetMap()->ToInstanceMap() : NULL;
        if (!instance || !RaidInstance(instance) || instance->IsRaidScaleLocked())
            return;

        instance->LockRaidScale(LivePlayers(instance));
    }

    void OnCreatureLeaveCombat(Creature* creature)
    {
        if (!creature || !IsEncounterBoss(creature))
            return;

        Map* map = creature->GetMap();
        InstanceMap* instance = map ? map->ToInstanceMap() : NULL;
        if (!instance || !instance->IsRaidScaleLocked())
            return;

        bool bossStillFighting = false;
        map->ForEachCreature(NoteBossStillFighting, &bossStillFighting);
        if (bossStillFighting)
            return;

        instance->UnlockRaidScale();
        map->ForEachCreature(ApplyHealthThunk, NULL);
    }

    void ScaleDamage(Unit const* attacker, uint32& damage)
    {
        Creature const* creature = attacker ? attacker->ToCreature() : NULL;
        if (!creature || !damage || IsPlayerOwned(creature))
            return;

        InstanceMap const* instance = RaidInstance(creature->GetMap());
        if (!instance)
            return;

        damage = ScaleAmount(damage, PlayersFor(instance), MapCap(instance));
    }

    void ScaleDamage(Unit const* attacker, int32& damage)
    {
        if (damage <= 0)
            return;
        uint32 scaled = uint32(damage);
        ScaleDamage(attacker, scaled);
        damage = int32(scaled);
    }
}
