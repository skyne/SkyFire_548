/*
* This file is part of the Pandaria 5.4.8 Project. See THANKS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

// SkyFire adaptation: lookups use LFGDungeonEntry, packets use SkyFire ByteBuffer, and step state is stored on InstanceScript.

#include "ScenarioMgr.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "ObjectDefines.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"

#include <ctime>

namespace
{
    bool FactionMatches(int32 dungeonFaction, TeamId team)
    {
        if (dungeonFaction < 0 || team == TEAM_NEUTRAL)
            return true;
        return dungeonFaction == int32(team);
    }
}

uint32 ScenarioMgr::GetScenarioIdForMap(uint32 mapId, DifficultyID difficulty, TeamId team) const
{
    uint32 anyMatch = 0;
    uint32 difficultyMatch = 0;

    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
    {
        LFGDungeonEntry const* dungeon = sLFGDungeonStore.LookupEntry(i);
        if (!dungeon || dungeon->m_ContinentID != int32(mapId) || !dungeon->m_ScenarioID)
            continue;

        bool const difficultyOk = difficulty == DIFFICULTY_NONE || DifficultyID(dungeon->m_DifficultyID) == difficulty;
        bool const factionOk = FactionMatches(dungeon->m_Faction, team);

        if (difficultyOk && factionOk)
            return dungeon->m_ScenarioID;

        if (difficultyOk && !difficultyMatch)
            difficultyMatch = dungeon->m_ScenarioID;
        if (!anyMatch)
            anyMatch = dungeon->m_ScenarioID;
    }

    return difficultyMatch ? difficultyMatch : anyMatch;
}

uint32 ScenarioMgr::GetScenarioIdForMap(uint32 mapId) const
{
    return GetScenarioIdForMap(mapId, DIFFICULTY_NONE, TEAM_NEUTRAL);
}

uint32 ScenarioMgr::GetScenarioIdForMap(Map const* map) const
{
    if (!map)
        return 0;

    TeamId team = TEAM_NEUTRAL;
    Map::PlayerList const& players = map->GetPlayers();
    if (!players.isEmpty())
        if (Player* player = players.begin()->GetSource())
            team = player->GetTeamId();

    return GetScenarioIdForMap(map->GetId(), map->GetDifficulty(), team);
}

uint32 ScenarioMgr::GetCriteriaIdForScenarioStep(uint32 mapId, uint32 step) const
{
    ScenarioDataContainer::const_iterator itr = _scenarioData.find(GetScenarioIdForMap(mapId));
    if (itr == _scenarioData.end())
        return 0;

    std::map<uint8, ScenarioStepEntry const*>::const_iterator stepItr = itr->second.Steps.find(uint8(step));
    if (stepItr == itr->second.Steps.end() || !stepItr->second)
        return 0;

    return stepItr->second->CriteriaTreeID;
}

uint32 ScenarioMgr::GetCriteriaIdForScenarioStep(Map const* map, uint32 step) const
{
    if (!map)
        return 0;

    ScenarioDataContainer::const_iterator itr = _scenarioData.find(GetScenarioIdForMap(map));
    if (itr == _scenarioData.end())
        return 0;

    std::map<uint8, ScenarioStepEntry const*>::const_iterator stepItr = itr->second.Steps.find(uint8(step));
    if (stepItr == itr->second.Steps.end() || !stepItr->second)
        return 0;

    return stepItr->second->CriteriaTreeID;
}

uint32 ScenarioMgr::GetMaxStepsForScenario(uint32 mapId) const
{
    uint32 const scenarioId = GetScenarioIdForMap(mapId);
    ScenarioDataContainer::const_iterator itr = _scenarioData.find(scenarioId);
    if (itr == _scenarioData.end() || itr->second.Steps.empty())
        return 0;

    return itr->second.Steps.rbegin()->second->Step;
}

void ScenarioMgr::WriteScenarioState(WorldPacket& data, uint32 scenarioId, int32 step, uint32 bonusData, uint32 waveMax, uint32 timer, uint32 medal, uint32 waveCurrent, std::vector<CriteriaProgressData> const& progress) const
{
    data << uint32(waveMax);
    data << uint32(timer);
    data << uint32(medal);
    data << uint32(scenarioId);
    data << uint32(waveCurrent);
    data << uint32(bonusData);
    data << uint32(step);
    data.WriteBits(progress.size(), 19);
    data.WriteBit(false);

    ByteBuffer bytesData;
    for (std::vector<CriteriaProgressData>::const_iterator itr = progress.begin(); itr != progress.end(); ++itr)
    {
        ObjectGuid counter(itr->Quantity);
        ObjectGuid instanceGuid = itr->InstanceGuid;

        data.WriteGuidMask(instanceGuid, 3, 5);
        data.WriteGuidMask(counter, 2);
        data.WriteGuidMask(instanceGuid, 6);
        data.WriteGuidMask(counter, 7);
        data.WriteGuidMask(instanceGuid, 1, 7, 0);
        data.WriteGuidMask(counter, 3);
        data.WriteGuidMask(instanceGuid, 4);
        data.WriteGuidMask(counter, 0, 5, 1, 4);
        data.WriteGuidMask(instanceGuid, 2);
        data.WriteGuidMask(counter, 6);
        data.WriteBits(itr->Flags, 4);

        bytesData.WriteGuidBytes(counter, 3, 7, 5);
        bytesData << uint32(itr->TimeFromStart);
        bytesData.WriteGuidBytes(counter, 2);
        bytesData << uint32(itr->TimeFromCreate);
        bytesData.WriteGuidBytes(counter, 6, 4);
        bytesData.AppendPackedTime(itr->Date);
        bytesData.WriteGuidBytes(counter, 1);
        bytesData.WriteGuidBytes(instanceGuid, 3, 2, 1, 5, 4, 7);
        bytesData.WriteGuidBytes(counter, 0);
        bytesData.WriteGuidBytes(instanceGuid, 6, 0);
        bytesData << uint32(itr->Id);
    }

    data.WriteBit(false);
    data.FlushBits();
    if (bytesData.size())
        data.append(bytesData);
}

void ScenarioMgr::SendScenarioState(Map const* map, ScenarioData const& scenarioData, Player* player)
{
    WorldPacket data(SMSG_SCENARIO_STATE);
    WriteScenarioState(data, scenarioData.ScenarioId, int32(scenarioData.StepId), scenarioData.BonusObjectiveDataCount,
        scenarioData.WaveMax, scenarioData.TimerDuration, MedalTypeNone, scenarioData.WaveCurrent, scenarioData.criteriaProgress);

    if (player)
        player->SendDirectMessage(&data);
    else if (map)
        map->SendToPlayers(&data);
}

void ScenarioMgr::SendScenarioProgressUpdate(Map const* map, CriteriaProgressData const& progress, Player* player)
{
    ObjectGuid counter(progress.Quantity);
    ObjectGuid instanceGuid = progress.InstanceGuid;
    WorldPacket data(SMSG_SCENARIO_PROGRESS_UPDATE);

    data.WriteGuidMask(instanceGuid, 6, 4, 5);
    data.WriteGuidMask(counter, 1, 5, 6, 7, 4);
    data.WriteGuidMask(instanceGuid, 1);
    data.WriteGuidMask(counter, 0);
    data.WriteGuidMask(instanceGuid, 2, 3);
    data.WriteGuidMask(counter, 2, 3);
    data.WriteGuidMask(instanceGuid, 0);
    data.WriteBits(progress.Flags, 4);
    data.WriteGuidMask(instanceGuid, 7);

    data.WriteGuidBytes(counter, 5, 2);
    data.WriteGuidBytes(instanceGuid, 7, 4);
    data.WriteGuidBytes(counter, 4, 0);
    data.WriteGuidBytes(instanceGuid, 1, 2);
    data << uint32(progress.TimeFromStart);
    data.WriteGuidBytes(counter, 3);
    data << uint32(progress.Id);
    data.WriteGuidBytes(counter, 1);
    data.AppendPackedTime(progress.Date);
    data << uint32(progress.TimeFromCreate);
    data.WriteGuidBytes(instanceGuid, 0, 3, 6);
    data.WriteGuidBytes(counter, 7, 6);
    data.WriteGuidBytes(instanceGuid, 5);

    if (player)
        player->SendDirectMessage(&data);
    else if (map)
        map->SendToPlayers(&data);
}

void ScenarioMgr::SendScenarioState(Player* player, uint32 mapId, int32 step, uint32 startTime, uint32 miscCriteriaTreeId, uint32 scenarioId, uint32 bonusData, ScenarioChallenge const* challengeData)
{
    if (!player)
        return;

    // The client wants an empty state before the first real step.
    if (step == 0)
        SendScenarioState(player, mapId, -1, 0, miscCriteriaTreeId, scenarioId);

    Map const* map = player->GetMap();
    uint32 resolvedScenarioId = scenarioId ? scenarioId : (map && map->GetId() == mapId ? GetScenarioIdForMap(map) : GetScenarioIdForMap(mapId));
    uint32 criteriaTreeId = miscCriteriaTreeId;
    if (!criteriaTreeId && step >= 0)
    {
        ScenarioDataContainer::const_iterator dataItr = _scenarioData.find(resolvedScenarioId);
        if (dataItr != _scenarioData.end())
        {
            std::map<uint8, ScenarioStepEntry const*>::const_iterator stepItr = dataItr->second.Steps.find(uint8(step));
            if (stepItr != dataItr->second.Steps.end() && stepItr->second)
                criteriaTreeId = stepItr->second->CriteriaTreeID;
        }
    }

    if (InstanceScript* instance = player->GetInstanceScript())
        if (step >= 0)
            instance->RememberScenarioStep(step, resolvedScenarioId, startTime);

    std::vector<CriteriaProgressData> progress;
    if (criteriaTreeId)
    {
        ObjectGuid counterGuid = 0;
        if (InstanceScript* instance = player->GetInstanceScript())
            counterGuid = instance->GetScenarioGUID();

        progress.push_back(CriteriaProgressData(criteriaTreeId, 0, counterGuid ? counterGuid : ObjectGuid(player->GetGUID()), uint32(time(NULL)), startTime, 0));
    }

    uint32 waveMax = challengeData ? challengeData->MaxWave : 0;
    uint32 timer = challengeData ? challengeData->Timer : 0;
    uint32 medal = challengeData ? challengeData->Medal : uint32(MedalTypeNone);
    uint32 waveCurrent = challengeData ? challengeData->CurWave : 0;

    WorldPacket data(SMSG_SCENARIO_STATE);
    WriteScenarioState(data, resolvedScenarioId, step, bonusData, waveMax, timer, medal, waveCurrent, progress);
    player->SendDirectMessage(&data);

    if (step > 0 && criteriaTreeId && !progress.empty())
        SendScenarioProgressUpdate(map, progress.front(), player);
}

void ScenarioMgr::LoadDB2Data()
{
    _scenarioData.clear();

    std::unordered_map<uint32, std::map<uint8, ScenarioStepEntry const*>> scenarioSteps;
    for (uint32 i = 0; i < sScenarioStepStore.GetNumRows(); ++i)
        if (ScenarioStepEntry const* step = sScenarioStepStore.LookupEntry(i))
            scenarioSteps[step->ScenarioID][step->Step] = step;

    for (uint32 i = 0; i < sScenarioStore.GetNumRows(); ++i)
    {
        ScenarioEntry const* scenario = sScenarioStore.LookupEntry(i);
        if (!scenario)
            continue;

        ScenarioDataObs& data = _scenarioData[scenario->ID];
        data.Entry = scenario;
        data.Steps = std::move(scenarioSteps[scenario->ID]);
    }
}

void ScenarioMgr::LoadScenarioPOI()
{
    uint32 oldMSTime = getMSTime();
    _scenarioPOIStore.clear();

    QueryResult result = WorldDatabase.Query("SELECT CriteriaTreeID, BlobIndex, Idx1, MapID, WorldMapAreaId, Floor, Priority, Flags, WorldEffectID, PlayerConditionID FROM scenario_poi ORDER BY CriteriaTreeID, Idx1");
    if (!result)
    {
        SF_LOG_ERROR("server.loading", ">> Loaded 0 scenario POI definitions. DB table `scenario_poi` is empty.");
        return;
    }

    QueryResult points = WorldDatabase.Query("SELECT CriteriaTreeID, Idx1, X, Y FROM scenario_poi_points ORDER BY CriteriaTreeID DESC, Idx1, Idx2");

    std::vector<std::vector<std::vector<ScenarioPOIPoint>>> pois;
    if (points)
    {
        Field* fields = points->Fetch();
        uint32 criteriaTreeIdMax = fields[0].GetUInt32();
        pois.resize(criteriaTreeIdMax + 1);

        do
        {
            fields = points->Fetch();
            int32 criteriaTreeId = fields[0].GetInt32();
            int32 idx1 = fields[1].GetInt32();
            int32 x = fields[2].GetInt32();
            int32 y = fields[3].GetInt32();

            if (criteriaTreeId < 0 || size_t(criteriaTreeId) >= pois.size())
                continue;

            if (int32(pois[criteriaTreeId].size()) <= idx1)
                pois[criteriaTreeId].resize(idx1 + 10);

            pois[criteriaTreeId][idx1].push_back(ScenarioPOIPoint(x, y));
        } while (points->NextRow());
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        int32 criteriaTreeId = fields[0].GetInt32();
        int32 blobIndex = fields[1].GetInt32();
        int32 idx1 = fields[2].GetInt32();
        int32 mapId = fields[3].GetInt32();
        int32 worldMapAreaId = fields[4].GetInt32();
        int32 floor = fields[5].GetInt32();
        int32 priority = fields[6].GetInt32();
        int32 flags = fields[7].GetInt32();
        int32 worldEffectId = fields[8].GetInt32();
        int32 playerConditionId = fields[9].GetInt32();

        if (criteriaTreeId > 0 && !sCriteriaTreeStore.LookupEntry(uint32(criteriaTreeId)))
            SF_LOG_ERROR("sql.sql", "`scenario_poi` CriteriaTreeID (%u) Idx1 (%u) does not correspond to a valid criteria tree", criteriaTreeId, idx1);

        if (criteriaTreeId >= 0 && size_t(criteriaTreeId) < pois.size() && idx1 >= 0 && size_t(idx1) < pois[criteriaTreeId].size())
            _scenarioPOIStore[uint32(criteriaTreeId)].push_back(ScenarioPOI(blobIndex, mapId, worldMapAreaId, floor, priority, flags, worldEffectId, playerConditionId, pois[criteriaTreeId][idx1]));
        else
            SF_LOG_ERROR("server.loading", "Table scenario_poi references unknown scenario poi points for criteria tree id %i POI id %i", criteriaTreeId, blobIndex);

        ++count;
    } while (result->NextRow());

    SF_LOG_INFO("server.loading", ">> Loaded %u scenario POI definitions in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

ScenarioPOIVector const* ScenarioMgr::GetScenarioPOIs(int32 criteriaTreeId) const
{
    if (criteriaTreeId < 0)
        return NULL;

    ScenarioPOIContainer::const_iterator itr = _scenarioPOIStore.find(uint32(criteriaTreeId));
    if (itr != _scenarioPOIStore.end())
        return &itr->second;

    return NULL;
}
