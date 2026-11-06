/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SKYFIRE_RAIDSCALE_H
#define SKYFIRE_RAIDSCALE_H

#include "Define.h"

class Creature;
class Map;
class Unit;

namespace RaidScale
{
    // players / map cap, floored at 5 and capped at the map maximum.
    // A boss pull freezes the fraction until every boss has left combat.
    float Multiplier(Map const* map);
    uint32 ScaleTargetCap(Unit const* caster, uint32 cap);
    void ApplyHealth(Creature* creature);
    void OnPlayersChanged(Map* map);
    void OnCreatureEnterCombat(Creature* creature);
    void OnCreatureLeaveCombat(Creature* creature);
    void ScaleDamage(Unit const* attacker, uint32& damage);
    void ScaleDamage(Unit const* attacker, int32& damage);
}

#endif
