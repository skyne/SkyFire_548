/*
 * This file is part of Project SkyFire https://www.projectskyfire.org.
 * See LICENSE.md file for Copyright information
 */

// SkyFire adaptation: role selectors and bloodlust cleanup used by the 5.4.8 raids.
#include "pandaria_script_compat.h"
#include "Pet.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "LootMgr.h"

uint32 PandariaActiveSpec(Player const* player)
{
    return player ? player->GetTalentSpecialization(player->GetActiveSpec()) : 0;
}

bool PandariaIsTank(Player const* player)
{
    switch (PandariaActiveSpec(player))
    {
        case SPEC_WARRIOR_PROTECTION:
        case SPEC_PALADIN_PROTECTION:
        case SPEC_DRUID_GUARDIAN:
        case SPEC_DEATH_KNIGHT_BLOOD:
        case SPEC_MONK_BREWMASTER:
            return true;
        default:
            return false;
    }
}

PandariaRole PandariaRoleFor(Player const* player)
{
    if (PandariaIsTank(player))
        return PANDARIA_ROLE_TANK;

    switch (PandariaActiveSpec(player))
    {
        case SPEC_PALADIN_HOLY:
        case SPEC_PRIEST_DISCIPLINE:
        case SPEC_PRIEST_HOLY:
        case SPEC_SHAMAN_RESTORATION:
        case SPEC_MONK_MISTWEAVER:
        case SPEC_DRUID_RESTORATION:
            return PANDARIA_ROLE_HEALER;
        default:
            return PANDARIA_ROLE_DPS;
    }
}

static bool PandariaAcceptSpecTarget(Player* player, uint32 spellId)
{
    return player && !player->IsGameMaster() && (!spellId || !player->HasAura(spellId));
}

bool CasterSpecTargetSelector::operator()(WorldObject* target) const
{
    Player* player = target ? target->ToPlayer() : nullptr;
    if (!PandariaAcceptSpecTarget(player, _spellId))
        return false;

    switch (player->getClass())
    {
        case CLASS_MAGE:
        case CLASS_PRIEST:
        case CLASS_WARLOCK:
        case CLASS_HUNTER:
            return true;
        case CLASS_MONK:
            return PandariaActiveSpec(player) == SPEC_MONK_MISTWEAVER;
        case CLASS_DRUID:
            return PandariaActiveSpec(player) == SPEC_DRUID_BALANCE || PandariaActiveSpec(player) == SPEC_DRUID_RESTORATION;
        case CLASS_SHAMAN:
            return PandariaActiveSpec(player) == SPEC_SHAMAN_ELEMENTAL || PandariaActiveSpec(player) == SPEC_SHAMAN_RESTORATION;
        case CLASS_PALADIN:
            return PandariaActiveSpec(player) == SPEC_PALADIN_HOLY;
        default:
            return false;
    }
}

bool MeeleSpecTargetSelector::operator()(WorldObject* target) const
{
    Player* player = target ? target->ToPlayer() : nullptr;
    if (!PandariaAcceptSpecTarget(player, _spellId))
        return false;

    switch (player->getClass())
    {
        case CLASS_ROGUE:
            return true;
        case CLASS_WARRIOR:
            return PandariaActiveSpec(player) != SPEC_WARRIOR_PROTECTION;
        case CLASS_PALADIN:
            return PandariaActiveSpec(player) == SPEC_PALADIN_RETRIBUTION;
        case CLASS_MONK:
            return PandariaActiveSpec(player) == SPEC_MONK_WINDWALKER;
        case CLASS_DRUID:
            return PandariaActiveSpec(player) == SPEC_DRUID_FERAL;
        case CLASS_SHAMAN:
            return PandariaActiveSpec(player) == SPEC_SHAMAN_ENHANCEMENT;
        case CLASS_DEATH_KNIGHT:
            return PandariaActiveSpec(player) != SPEC_DEATH_KNIGHT_BLOOD;
        default:
            return false;
    }
}

bool TankSpecTargetSelector::operator()(WorldObject* target) const
{
    Player* player = target ? target->ToPlayer() : nullptr;
    return PandariaAcceptSpecTarget(player, _spellId) && PandariaIsTank(player);
}

bool NonTankSpecTargetSelector::operator()(WorldObject* target) const
{
    Player* player = target ? target->ToPlayer() : nullptr;
    return PandariaAcceptSpecTarget(player, _spellId) && !PandariaIsTank(player);
}

bool HealerSpecTargetSelector::operator()(WorldObject* target) const
{
    Player* player = target ? target->ToPlayer() : nullptr;
    return PandariaAcceptSpecTarget(player, _spellId) && PandariaRoleFor(player) == PANDARIA_ROLE_HEALER;
}

void PandariaFixate(Creature* creature, uint64 targetGuid)
{
    if (!creature)
        return;

    if (Unit* target = ObjectAccessor::GetUnit(*creature, targetGuid))
    {
        creature->AddThreat(target, 5000000.0f);
        if (creature->AI())
            creature->AI()->AttackStart(target);
    }
}

void PandariaRemoveBloodlust(InstanceScript* instance)
{
    if (!instance || !instance->instance)
        return;

    static uint32 const spells[] = { 95809, 80354, 57723, 57724 };
    Map::PlayerList const& players = instance->instance->GetPlayers();
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (!player)
            continue;

        for (uint32 spell : spells)
        {
            player->RemoveAurasDueToSpell(spell);
            if (Pet* pet = player->GetPet())
                pet->RemoveAurasDueToSpell(spell);
        }
    }
}

Player* PandariaFirstPlayer(Map* map)
{
    if (!map)
        return nullptr;

    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        if (Player* player = itr->GetSource())
            return player;

    return nullptr;
}

Player* GetFarthestPlayerInArea(Unit* source, float range)
{
    if (!source || !source->GetMap())
        return nullptr;

    Player* farthest = nullptr;
    float best = 0.0f;
    Map::PlayerList const& players = source->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (!player || !player->IsAlive() || player->IsGameMaster() || !source->IsWithinDist(player, range))
            continue;

        float distance = source->GetDistance(player);
        if (!farthest || distance > best)
        {
            farthest = player;
            best = distance;
        }
    }

    return farthest;
}

void PandariaRewardEncounterLoot(Player* player, Creature* creature, uint32 lootId)
{
    if (!player || !creature)
        return;

    if (!lootId && creature->GetCreatureTemplate())
        lootId = creature->GetCreatureTemplate()->lootid;
    if (!lootId)
        return;

    creature->loot.clear();
    if (!creature->loot.FillLoot(lootId, LootTemplates_Creature, player, true))
        return;

    creature->SetLootRecipient(player);
    creature->SetFlag(OBJECT_FIELD_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
}
