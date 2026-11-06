/*
 * This file is part of Project SkyFire https://www.projectskyfire.org.
 * See LICENSE.md file for Copyright information
 */

// SkyFire adaptation: loaders and names the 5.4.8 raid scripts expect.
#ifndef PANDARIA_SCRIPT_COMPAT_H_
#define PANDARIA_SCRIPT_COMPAT_H_

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Containers.h"
#include "TaskScheduler.h"

enum PandariaLfrSpells
{
    SPELL_DETERMINATION_LFR = 139068
};

template<class S>
struct spell_script : public SpellScriptLoader
{
    explicit spell_script(char const* name) : SpellScriptLoader(name) { }
    SpellScript* GetSpellScript() const override { return new S(); }
};

template<class A>
struct aura_script : public SpellScriptLoader
{
    explicit aura_script(char const* name) : SpellScriptLoader(name) { }
    AuraScript* GetAuraScript() const override { return new A(); }
};


#include "InstanceScript.h"
#include "Player.h"

enum PandariaRole
{
    PANDARIA_ROLE_DPS = 0,
    PANDARIA_ROLE_TANK = 1,
    PANDARIA_ROLE_HEALER = 2
};

uint32 PandariaActiveSpec(Player const* player);
bool PandariaIsTank(Player const* player);
PandariaRole PandariaRoleFor(Player const* player);
void PandariaRemoveBloodlust(InstanceScript* instance);

Player* GetFarthestPlayerInArea(Unit* source, float range);
Player* PandariaFirstPlayer(Map* map);

struct CasterSpecTargetSelector
{
    explicit CasterSpecTargetSelector(uint32 spellId = 0) : _spellId(spellId) { }
    bool operator()(WorldObject* target) const;
private:
    uint32 _spellId;
};

struct MeeleSpecTargetSelector
{
    explicit MeeleSpecTargetSelector(uint32 spellId = 0) : _spellId(spellId) { }
    bool operator()(WorldObject* target) const;
private:
    uint32 _spellId;
};

struct TankSpecTargetSelector
{
    explicit TankSpecTargetSelector(uint32 spellId = 0) : _spellId(spellId) { }
    bool operator()(WorldObject* target) const;
private:
    uint32 _spellId;
};

struct NonTankSpecTargetSelector
{
    explicit NonTankSpecTargetSelector(uint32 spellId = 0) : _spellId(spellId) { }
    bool operator()(WorldObject* target) const;
private:
    uint32 _spellId;
};

struct HealerSpecTargetSelector
{
    explicit HealerSpecTargetSelector(uint32 spellId = 0) : _spellId(spellId) { }
    bool operator()(WorldObject* target) const;
private:
    uint32 _spellId;
};

void PandariaFixate(Creature* creature, uint64 targetGuid);
void PandariaRewardEncounterLoot(Player* player, Creature* creature, uint32 lootId = 0);

#endif
