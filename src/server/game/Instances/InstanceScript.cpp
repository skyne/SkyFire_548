/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "Group.h"
#include "InstanceScript.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"

void InstanceScript::SaveToDB()
{
    std::string data = GetSaveData();
    if (data.empty())
        return;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_INSTANCE_DATA);
    stmt->setUInt32(0, GetCompletedEncounterMask());
    stmt->setString(1, data);
    stmt->setUInt32(2, instance->GetInstanceId());
    CharacterDatabase.Execute(stmt);
}

void InstanceScript::HandleGameObject(uint64 GUID, bool open, GameObject* go)
{
    if (!go)
        go = instance->GetGameObject(GUID);
    if (go)
        go->SetGoState(open ? GOState::GO_STATE_ACTIVE : GOState::GO_STATE_READY);
    else
        SF_LOG_DEBUG("scripts", "InstanceScript: HandleGameObject failed");
}

bool InstanceScript::IsEncounterInProgress() const
{
    for (std::vector<BossInfo>::const_iterator itr = bosses.begin(); itr != bosses.end(); ++itr)
        if (itr->state == IN_PROGRESS)
            return true;

    return false;
}

void InstanceScript::LoadMinionData(const MinionData* data)
{
    while (data->entry)
    {
        if (data->bossId < bosses.size())
            minions.insert(std::make_pair(data->entry, MinionInfo(&bosses[data->bossId])));

        ++data;
    }
    SF_LOG_DEBUG("scripts", "InstanceScript::LoadMinionData: " UI64FMTD " minions loaded.", uint64(minions.size()));
}

void InstanceScript::LoadDoorData(const DoorData* data)
{
    while (data->entry)
    {
        if (data->bossId < bosses.size())
            doors.insert(std::make_pair(data->entry, DoorInfo(&bosses[data->bossId], data->type, BoundaryType(data->boundary))));

        ++data;
    }
    SF_LOG_DEBUG("scripts", "InstanceScript::LoadDoorData: " UI64FMTD " doors loaded.", uint64(doors.size()));
}

void InstanceScript::UpdateMinionState(Creature* minion, EncounterState state)
{
    switch (state)
    {
        case NOT_STARTED:
            if (!minion->IsAlive())
                minion->Respawn();
            else if (minion->IsInCombat())
                minion->AI()->EnterEvadeMode();
            break;
        case IN_PROGRESS:
            if (!minion->IsAlive())
                minion->Respawn();
            else if (!minion->GetVictim())
                minion->AI()->DoZoneInCombat();
            break;
        default:
            break;
    }
}

void InstanceScript::UpdateDoorState(GameObject* door)
{
    DoorInfoMapBounds range = doors.equal_range(door->GetEntry());
    if (range.first == range.second)
        return;

    bool open = true;
    for (; range.first != range.second && open; ++range.first)
    {
        DoorInfo const& info = range.first->second;
        switch (info.type)
        {
            case DOOR_TYPE_ROOM:
                open = (info.bossInfo->state != IN_PROGRESS);
                break;
            case DOOR_TYPE_PASSAGE:
                open = (info.bossInfo->state == DONE);
                break;
            case DOOR_TYPE_SPAWN_HOLE:
                open = (info.bossInfo->state == IN_PROGRESS);
                break;
            default:
                break;
        }
    }

    door->SetGoState(open ? GOState::GO_STATE_ACTIVE : GOState::GO_STATE_READY);
}

void InstanceScript::AddDoor(GameObject* door, bool add)
{
    DoorInfoMapBounds range = doors.equal_range(door->GetEntry());
    if (range.first == range.second)
        return;

    for (; range.first != range.second; ++range.first)
    {
        DoorInfo const& data = range.first->second;

        if (add)
        {
            data.bossInfo->door[data.type].insert(door);
            switch (data.boundary)
            {
                default:
                case BOUNDARY_NONE:
                    break;
                case BOUNDARY_N:
                case BOUNDARY_S:
                    data.bossInfo->boundary[data.boundary] = door->GetPositionX();
                    break;
                case BOUNDARY_E:
                case BOUNDARY_W:
                    data.bossInfo->boundary[data.boundary] = door->GetPositionY();
                    break;
                case BOUNDARY_NW:
                case BOUNDARY_SE:
                    data.bossInfo->boundary[data.boundary] = door->GetPositionX() + door->GetPositionY();
                    break;
                case BOUNDARY_NE:
                case BOUNDARY_SW:
                    data.bossInfo->boundary[data.boundary] = door->GetPositionX() - door->GetPositionY();
                    break;
            }
        }
        else
            data.bossInfo->door[data.type].erase(door);
    }

    if (add)
        UpdateDoorState(door);
}

void InstanceScript::AddMinion(Creature* minion, bool add)
{
    MinionInfoMap::iterator itr = minions.find(minion->GetEntry());
    if (itr == minions.end())
        return;

    if (add)
        itr->second.bossInfo->minion.insert(minion);
    else
        itr->second.bossInfo->minion.erase(minion);
}

bool InstanceScript::SetBossState(uint32 id, EncounterState state)
{
    if (id < bosses.size())
    {
        BossInfo* bossInfo = &bosses[id];
        if (bossInfo->state == TO_BE_DECIDED) // loading
        {
            bossInfo->state = state;
            //SF_LOG_ERROR("misc", "Inialize boss %u state as %u.", id, (uint32)state);
            return false;
        }
        else
        {
            if (bossInfo->state == state)
                return false;

            if (state == DONE)
                for (MinionSet::iterator i = bossInfo->minion.begin(); i != bossInfo->minion.end(); ++i)
                    if ((*i)->isWorldBoss() && (*i)->IsAlive())
                        return false;

            bossInfo->state = state;
            SaveToDB();
        }

        for (uint32 type = 0; type < MAX_DOOR_TYPES; ++type)
            for (DoorSet::iterator i = bossInfo->door[type].begin(); i != bossInfo->door[type].end(); ++i)
                UpdateDoorState(*i);

        for (MinionSet::iterator i = bossInfo->minion.begin(); i != bossInfo->minion.end(); ++i)
            UpdateMinionState(*i, state);

        if (state == DONE && id + 1 == bosses.size())
            CompleteChallengeMode();

        return true;
    }
    return false;
}

void InstanceScript::LoadBossState(const char* data)
{
    if (!data)
    {
        //OUT_LOAD_INST_DATA_FAIL
        return;
    }
    //OUT_LOAD_INST_DATA(data);
    std::istringstream loadStream(data);
    uint32 buff;
    uint32 bossId = 0;
    for (std::vector<BossInfo>::iterator i = bosses.begin(); i != bosses.end(); ++i, ++bossId)
    {
        loadStream >> buff;
        if (buff < TO_BE_DECIDED)
            SetBossState(bossId, (EncounterState)buff);
    }
    //OUT_LOAD_INST_DATA_COMPLETE
}

std::string InstanceScript::GetBossSaveData()
{
    std::ostringstream saveStream;
    for (std::vector<BossInfo>::iterator i = bosses.begin(); i != bosses.end(); ++i)
        saveStream << (uint32)i->state << ' ';
    return saveStream.str();
}

void InstanceScript::DoUseDoorOrButton(uint64 uiGuid, uint32 uiWithRestoreTime, bool bUseAlternativeState)
{
    if (!uiGuid)
        return;

    GameObject* go = instance->GetGameObject(uiGuid);

    if (go)
    {
        if (go->GetGoType() == GAMEOBJECT_TYPE_DOOR || go->GetGoType() == GAMEOBJECT_TYPE_BUTTON)
        {
            if (go->getLootState() == LootState::GO_READY)
                go->UseDoorOrButton(uiWithRestoreTime, bUseAlternativeState);
            else if (go->getLootState() == LootState::GO_ACTIVATED)
                go->ResetDoorOrButton();
        }
        else
            SF_LOG_ERROR("misc", "SD2: Script call DoUseDoorOrButton, but gameobject entry %u is type %u.", go->GetEntry(), go->GetGoType());
    }
}

void InstanceScript::DoRespawnGameObject(uint64 uiGuid, uint32 uiTimeToDespawn)
{
    if (GameObject* go = instance->GetGameObject(uiGuid))
    {
        //not expect any of these should ever be handled
        if (go->GetGoType() == GAMEOBJECT_TYPE_FISHINGNODE || go->GetGoType() == GAMEOBJECT_TYPE_DOOR ||
            go->GetGoType() == GAMEOBJECT_TYPE_BUTTON || go->GetGoType() == GAMEOBJECT_TYPE_TRAP)
            return;

        if (go->isSpawned())
            return;

        go->SetRespawnTime(uiTimeToDespawn);
    }
}

void InstanceScript::DoUpdateWorldState(uint32 uiStateId, uint32 uiStateData)
{
    Map::PlayerList const& lPlayers = instance->GetPlayers();

    if (!lPlayers.isEmpty())
    {
        for (Map::PlayerList::const_iterator itr = lPlayers.begin(); itr != lPlayers.end(); ++itr)
            if (Player* player = itr->GetSource())
                player->SendUpdateWorldState(uiStateId, uiStateData);
    }
    else
        SF_LOG_DEBUG("scripts", "DoUpdateWorldState attempt send data but no players in map.");
}

// Send Notify to all players in instance
void InstanceScript::DoSendNotifyToInstance(char const* format, ...)
{
    InstanceMap::PlayerList const& players = instance->GetPlayers();

    if (!players.isEmpty())
    {
        va_list ap;
        va_start(ap, format);
        char buff[1024];
        vsnprintf(buff, 1024, format, ap);
        va_end(ap);
        for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
            if (Player* player = i->GetSource())
                if (WorldSession* session = player->GetSession())
                    session->SendNotification("%s", buff);
    }
}

// Update Achievement Criteria for all players in instance
void InstanceScript::DoUpdateAchievementCriteria(AchievementCriteriaTypes type, uint32 miscValue1 /*= 0*/, uint32 miscValue2 /*= 0*/, Unit* unit /*= NULL*/)
{
    Map::PlayerList const& PlayerList = instance->GetPlayers();

    if (!PlayerList.isEmpty())
        for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
            if (Player* player = i->GetSource())
                player->UpdateAchievementCriteria(type, miscValue1, miscValue2, 0, unit);
}

// Reset Achievement Criteria for all players in instance
void InstanceScript::DoResetAchievementCriteria(AchievementCriteriaTypes type, uint64 miscValue1 /*= 0*/, uint64 miscValue2 /*= 0*/, bool evenIfCriteriaComplete /*= false*/)
{
    Map::PlayerList const& PlayerList = instance->GetPlayers();

    if (!PlayerList.isEmpty())
        for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
            if (Player* player = i->GetSource())
                player->ResetAchievementCriteria(type, miscValue1, miscValue2, evenIfCriteriaComplete);
}

// Start timed achievement for all players in instance
void InstanceScript::DoStartTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry)
{
    Map::PlayerList const& PlayerList = instance->GetPlayers();

    if (!PlayerList.isEmpty())
        for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
            if (Player* player = i->GetSource())
                player->StartTimedAchievement(type, entry);
}

// Stop timed achievement for all players in instance
void InstanceScript::DoStopTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry)
{
    Map::PlayerList const& PlayerList = instance->GetPlayers();

    if (!PlayerList.isEmpty())
        for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
            if (Player* player = i->GetSource())
                player->RemoveTimedAchievement(type, entry);
}

// Remove Auras due to Spell on all players in instance
void InstanceScript::DoRemoveAurasDueToSpellOnPlayers(uint32 spell)
{
    Map::PlayerList const& PlayerList = instance->GetPlayers();
    if (!PlayerList.isEmpty())
    {
        for (Map::PlayerList::const_iterator itr = PlayerList.begin(); itr != PlayerList.end(); ++itr)
        {
            if (Player* player = itr->GetSource())
            {
                player->RemoveAurasDueToSpell(spell);
                if (Pet* pet = player->GetPet())
                    pet->RemoveAurasDueToSpell(spell);
            }
        }
    }
}

// Cast spell on all players in instance
void InstanceScript::DoCastSpellOnPlayers(uint32 spell)
{
    Map::PlayerList const& PlayerList = instance->GetPlayers();

    if (!PlayerList.isEmpty())
        for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
            if (Player* player = i->GetSource())
                player->CastSpell(player, spell, true);
}

void InstanceScript::DoAddAuraOnPlayers(uint32 spell)
{
    Map::PlayerList const& PlayerList = instance->GetPlayers();

    if (!PlayerList.isEmpty())
        for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
            if (Player* player = i->GetSource())
                player->AddAura(spell, player);
}

bool InstanceScript::IsWipe(float range, Unit* source)
{
    Map::PlayerList const& PlayerList = instance->GetPlayers();

    for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
    {
        Player* player = i->GetSource();
        if (!player || !player->IsAlive() || player->IsGameMaster())
            continue;

        if (range > 0.0f && source && !player->IsWithinDistInMap(source, range))
            continue;

        return false;
    }

    return true;
}

bool InstanceScript::CheckAchievementCriteriaMeet(uint32 criteria_id, Player const* /*source*/, Unit const* /*target*/ /*= NULL*/, uint32 /*miscvalue1*/ /*= 0*/)
{
    SF_LOG_ERROR("misc", "Achievement system call InstanceScript::CheckAchievementCriteriaMeet but instance script for map %u not have implementation for achievement criteria %u",
        instance->GetId(), criteria_id);
    return false;
}

void InstanceScript::SendEncounterUnit(uint32 type, Unit* unit /*= NULL*/, uint8 param1 /*= 0*/, uint8 param2 /*= 0*/)
{
    // size of this packet is at most 15 (usually less)
    WorldPacket data(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, 15);
    data << uint32(type);

    switch (type)
    {
        case ENCOUNTER_FRAME_ENGAGE:
        case ENCOUNTER_FRAME_DISENGAGE:
        case ENCOUNTER_FRAME_UPDATE_PRIORITY:
            if (!unit)
                return;
            data.append(unit->GetPackGUID());
            data << uint8(param1);
            break;
        case ENCOUNTER_FRAME_ADD_TIMER:
        case ENCOUNTER_FRAME_ENABLE_OBJECTIVE:
        case ENCOUNTER_FRAME_DISABLE_OBJECTIVE:
        case ENCOUNTER_FRAME_SET_COMBAT_RES_LIMIT:
            data << uint8(param1);
            break;
        case ENCOUNTER_FRAME_UPDATE_OBJECTIVE:
            data << uint8(param1);
            data << uint8(param2);
            break;
        case ENCOUNTER_FRAME_UNK7:
        case ENCOUNTER_FRAME_ADD_COMBAT_RES_LIMIT:
        case ENCOUNTER_FRAME_RESET_COMBAT_RES_LIMIT:
        default:
            break;
    }

    instance->SendToPlayers(&data);
}

void InstanceScript::UpdateEncounterState(EncounterCreditType type, uint32 creditEntry, Unit* /*source*/)
{
    DungeonEncounterList const* encounters = sObjectMgr->GetDungeonEncounterList(instance->GetId(), instance->GetDifficulty());
    if (!encounters)
        return;

    uint32 dungeonId = 0;

    for (DungeonEncounterList::const_iterator itr = encounters->begin(); itr != encounters->end(); ++itr)
    {
        DungeonEncounter const* encounter = *itr;
        if (encounter->creditType == type && encounter->creditEntry == creditEntry)
        {
            completedEncounters |= 1 << encounter->dbcEntry->encounterIndex;
            if (encounter->lastEncounterDungeon)
            {
                dungeonId = encounter->lastEncounterDungeon;
                SF_LOG_DEBUG("lfg", "UpdateEncounterState: Instance %s (instanceId %u) completed encounter %s. Credit Dungeon: %u", instance->GetMapName(), instance->GetInstanceId(), encounter->dbcEntry->encounterName, dungeonId);
                break;
            }
        }
    }

    if (dungeonId)
    {
        Map::PlayerList const& players = instance->GetPlayers();
        for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
        {
            if (Player* player = i->GetSource())
                if (Group* grp = player->GetGroup())
                    if (grp->isLFGGroup())
                    {
                        sLFGMgr->FinishDungeon(grp->GetGUID(), dungeonId);
                        return;
                    }
        }
    }
}

void InstanceScript::UpdatePhasing()
{
    Map::PlayerList const& players = instance->GetPlayers();
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        if (Player* player = itr->GetSource())
            player->UpdatePhasing();
}

namespace
{
    enum
    {
        CHALLENGE_ELAPSED_TIMER = 1
    };
    uint32 const CHALLENGE_START_COUNTDOWN_SECONDS = 5;

    bool FindChallengeMedalThresholds(uint32 mapId, uint32& goldMs, uint32& silverMs, uint32& bronzeMs)
    {
        for (uint32 i = 0; i < sMapChallengeModeStore.GetNumRows(); ++i)
        {
            MapChallengeModeEntry const* entry = sMapChallengeModeStore.LookupEntry(i);
            if (!entry || entry->MapID != mapId)
                continue;

            if (!entry->GoldTime && !entry->SilverTime && !entry->BronzeTime)
                return false;

            goldMs = entry->GoldTime * IN_MILLISECONDS;
            silverMs = entry->SilverTime * IN_MILLISECONDS;
            bronzeMs = entry->BronzeTime * IN_MILLISECONDS;
            return true;
        }

        return false;
    }
}

void InstanceScript::SetChallengeModeStarted()
{
    if (_challengeStarted || _challengeCompleted || !instance->IsChallengeDungeon())
        return;

    _challengeStarted = true;
    _challengeMedal = CHALLENGE_MEDAL_GOLD;
    _challengeCountdown = CHALLENGE_START_COUNTDOWN_SECONDS * IN_MILLISECONDS;

    WorldPacket data(SMSG_START_TIMER, 12);
    data << uint32(CHALLENGE_START_COUNTDOWN_SECONDS);
    data << uint32(CHALLENGE_START_COUNTDOWN_SECONDS);
    data << uint32(CHALLENGE_ELAPSED_TIMER);
    instance->SendToPlayers(&data);
}

void InstanceScript::ScheduleChallengeStartup(uint32 diff)
{
    if (!_challengeStarted || _challengeCompleted || !_challengeCountdown)
        return;

    if (_challengeCountdown > diff)
    {
        _challengeCountdown -= diff;
        return;
    }

    _challengeCountdown = 0;
    for (std::vector<uint64>::const_iterator itr = _challengeDoorGuids.begin(); itr != _challengeDoorGuids.end(); ++itr)
        if (GameObject* door = instance->GetGameObject(*itr))
            door->SetGoState(GOState::GO_STATE_ACTIVE);

    WorldPacket data(SMSG_START_ELAPSED_TIMER, 8);
    data << uint32(0);
    data << uint32(CHALLENGE_ELAPSED_TIMER);
    instance->SendToPlayers(&data);
}

void InstanceScript::ScheduleChallengeTimeUpdate(uint32 diff)
{
    if (!_challengeStarted || _challengeCompleted || _challengeCountdown)
        return;

    _challengeTime += diff;

    uint32 goldMs = 0;
    uint32 silverMs = 0;
    uint32 bronzeMs = 0;
    if (!FindChallengeMedalThresholds(instance->GetId(), goldMs, silverMs, bronzeMs))
        return;

    uint32 thresholds[CHALLENGE_MEDAL_GOLD];
    thresholds[CHALLENGE_MEDAL_BRONZE - 1] = bronzeMs;
    thresholds[CHALLENGE_MEDAL_SILVER - 1] = silverMs;
    thresholds[CHALLENGE_MEDAL_GOLD - 1] = goldMs;

    while (_challengeMedal > CHALLENGE_MEDAL_NONE && _challengeTime > thresholds[_challengeMedal - 1])
        --_challengeMedal;
}

void InstanceScript::SendChallengeModeProgress(Player* player)
{
    if (!player || !_challengeStarted || _challengeCompleted || _challengeCountdown)
        return;

    WorldPacket data(SMSG_START_ELAPSED_TIMER, 8);
    data << uint32(_challengeTime / 100);
    data << uint32(CHALLENGE_ELAPSED_TIMER);
    player->GetSession()->SendPacket(&data);
}

void InstanceScript::CompleteChallengeMode()
{
    if (!_challengeStarted || _challengeCompleted || _challengeCountdown)
        return;

    _challengeStarted = false;
    _challengeCompleted = true;

    WorldPacket stop(SMSG_STOP_ELAPSED_TIMER, 5);
    stop.WriteBit(false);
    stop.FlushBits();
    stop << uint32(CHALLENGE_ELAPSED_TIMER);
    instance->SendToPlayers(&stop);

    uint32 const now = uint32(time(NULL));
    uint32 memberIndex = 0;
    RealmCompletedChallenge record;
    record.AttemptId = 0;
    record.CompletionTime = _challengeTime;
    record.CompletionDate = now;
    record.MedalEarned = _challengeMedal;

    uint32 guildId = 0;
    Map::PlayerList const& players = instance->GetPlayers();
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end() && memberIndex < 5; ++itr)
    {
        Player* player = itr->GetSource();
        if (!player)
            continue;

        record.Members[memberIndex].Guid = player->GetGUIDLow();
        record.Members[memberIndex].SpecId = player->GetTalentSpecialization(player->GetActiveSpec());
        ++memberIndex;

        if (!guildId)
            if (Group* group = player->GetGroup())
                if (group->IsGuildGroup(player->GetGuildId(), player))
                    guildId = player->GetGuildId();
    }
    record.MembersCount = uint8(memberIndex);

    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (!player || !player->GetSession())
            continue;

        CompletedChallenge stored;
        bool const newBestTime = sObjectMgr->RecordCharacterChallenge(player->GetGUIDLow(), instance->GetId(), _challengeTime, _challengeMedal, stored);
        if (newBestTime)
        {
            player->GetSession()->SendChallengeModeMapStatsUpdate(instance->GetId());

            WorldPacket recordPacket(SMSG_CHALLENGE_MODE_NEW_PLAYER_RECORD, 12);
            recordPacket << uint32(instance->GetId());
            recordPacket << uint32(_challengeMedal);
            recordPacket << uint32(_challengeTime);
            player->GetSession()->SendPacket(&recordPacket);
        }
    }

    ChallengeReward reward;
    uint32 money = 0;
    uint32 valor = 0;
    if (_challengeMedal < 4 && sObjectMgr->GetChallengeReward(instance->GetId(), reward))
    {
        money = reward.MoneyReward[_challengeMedal];
        valor = reward.ValorReward[_challengeMedal];
        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            if (Player* player = itr->GetSource())
            {
                if (money)
                    player->ModifyMoney(int64(money));
                if (valor)
                    player->ModifyCurrency(CURRENCY_TYPE_VALOR_POINTS, int32(valor), true);
            }
        }
    }

    uint32 const currencyRewards = valor != 0 ? 1 : 0;
    WorldPacket complete(SMSG_CHALLENGE_MODE_COMPLETE, 16);
    complete << uint32(money);
    complete << uint32(instance->GetId());
    complete << uint32(_challengeMedal);
    complete << uint32(_challengeTime);
    complete.WriteBits(currencyRewards, 21);
    complete.WriteBits(0, 20);
    if (currencyRewards)
    {
        complete << uint32(CURRENCY_TYPE_VALOR_POINTS);
        complete << uint32(valor);
    }
    complete.FlushBits();
    instance->SendToPlayers(&complete);

    sObjectMgr->RecordRealmChallenge(instance->GetId(), record);
    if (guildId)
    {
        record.GuildId = guildId;
        sObjectMgr->RecordGuildChallenge(instance->GetId(), record);
    }
}
