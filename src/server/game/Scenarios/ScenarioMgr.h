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

// SkyFire adaptation: scenario progress is tracked per InstanceScript, Map, and Group.

#ifndef ScenarioMgr_h__
#define ScenarioMgr_h__

#include "Common.h"
#include "DBCEnums.h"
#include "ByteBuffer.h"

class WorldPacket;

#include <map>
#include <unordered_map>
#include <vector>

#define MAX_ALLOWED_SCENARIO_POI_QUERY_SIZE 35

class Map;
class Player;
class InstanceScript;
struct ScenarioEntry;
struct ScenarioStepEntry;

enum ScenarioType
{
    SCENARIO_TYPE_SCENARIO       = 0,
    SCENARIO_TYPE_CHALLENGE_MODE = 1,
    SCENARIO_TYPE_SOLO           = 2,
    SCENARIO_TYPE_DUNGEON        = 10,
};

enum ChallengeMedals
{
    MedalTypeNone   = 0,
    MedalTypeBronze = 1,
    MedalTypeSilver = 2,
    MedalTypeGold   = 3,
    MaxMedalType
};

struct ScenarioDataObs
{
    ScenarioEntry const* Entry;
    std::map<uint8, ScenarioStepEntry const*> Steps;
};

struct ScenarioPOIPoint
{
    int32 X;
    int32 Y;

    ScenarioPOIPoint() : X(0), Y(0) { }
    ScenarioPOIPoint(int32 x, int32 y) : X(x), Y(y) { }
};

struct ScenarioPOI
{
    int32 BlobIndex;
    int32 MapID;
    int32 WorldMapAreaID;
    int32 Floor;
    int32 Priority;
    int32 Flags;
    int32 WorldEffectID;
    int32 PlayerConditionID;
    std::vector<ScenarioPOIPoint> Points;

    ScenarioPOI() : BlobIndex(0), MapID(0), WorldMapAreaID(0), Floor(0), Priority(0), Flags(0), WorldEffectID(0), PlayerConditionID(0) { }

    ScenarioPOI(int32 blobIndex, int32 mapId, int32 worldMapAreaId, int32 floor, int32 priority, int32 flags, int32 worldEffectId,
        int32 playerConditionId, std::vector<ScenarioPOIPoint> points) :
        BlobIndex(blobIndex), MapID(mapId), WorldMapAreaID(worldMapAreaId), Floor(floor), Priority(priority), Flags(flags),
        WorldEffectID(worldEffectId), PlayerConditionID(playerConditionId), Points(std::move(points)) { }
};

typedef std::vector<ScenarioPOI> ScenarioPOIVector;
typedef std::unordered_map<uint32, ScenarioPOIVector> ScenarioPOIContainer;
typedef std::map<uint32, ScenarioDataObs> ScenarioDataContainer;

struct ScenarioChallenge
{
    uint32 CurWave;
    uint32 MaxWave;
    uint32 Medal;
    uint32 Timer;

    ScenarioChallenge(uint32 curWave, uint32 maxWave, uint32 medal, uint32 timer)
        : CurWave(curWave), MaxWave(maxWave), Medal(medal), Timer(timer) { }
};

// One criteria counter inside SMSG_SCENARIO_STATE / SMSG_SCENARIO_PROGRESS_UPDATE.
struct CriteriaProgressData
{
    CriteriaProgressData(uint32 id, uint64 quantity, ObjectGuid instanceGuid, uint32 date, uint32 startTime, uint8 flags)
        : InstanceGuid(instanceGuid), Quantity(quantity), Id(id), Date(date), TimeFromStart(startTime), TimeFromCreate(startTime), Flags(flags) { }

    CriteriaProgressData()
        : InstanceGuid(0), Quantity(0), Id(0), Date(0), TimeFromStart(0), TimeFromCreate(0), Flags(0) { }

    ObjectGuid InstanceGuid;
    uint64 Quantity;
    uint32 Id;
    uint32 Date;
    uint32 TimeFromStart;
    uint32 TimeFromCreate;
    uint8 Flags;
};

// Full scenario UI state for the players inside one instance.
struct ScenarioData
{
    ScenarioData(uint32 scenarioId, uint32 stepId, uint32 currWave, uint32 maxWave, uint32 timer, uint32 criteriaCount, uint32 bonusAvailable, bool complete)
        : ScenarioId(scenarioId), StepId(stepId), WaveCurrent(currWave), WaveMax(maxWave), TimerDuration(timer),
          CriteriaCount(criteriaCount), BonusObjectiveDataCount(bonusAvailable), ScenarioComplete(complete)
    {
        criteriaProgress.resize(criteriaCount);
    }

    ScenarioData(uint32 scenarioId, uint32 stepId)
        : ScenarioId(scenarioId), StepId(stepId), WaveCurrent(0), WaveMax(0), TimerDuration(0),
          CriteriaCount(0), BonusObjectiveDataCount(0), ScenarioComplete(false) { }

    ScenarioData()
        : ScenarioId(0), StepId(0), WaveCurrent(0), WaveMax(0), TimerDuration(0),
          CriteriaCount(0), BonusObjectiveDataCount(0), ScenarioComplete(false) { }

    void AddCriteriaProgress(CriteriaProgressData const& data)
    {
        criteriaProgress.push_back(data);
        CriteriaCount = uint32(criteriaProgress.size());
    }

    uint32 ScenarioId;
    uint32 StepId;
    uint32 WaveCurrent;
    uint32 WaveMax;
    uint32 TimerDuration;
    uint32 CriteriaCount;
    uint32 BonusObjectiveDataCount;
    bool ScenarioComplete;
    std::vector<CriteriaProgressData> criteriaProgress;
};

class ScenarioMgr
{
    private:
        ScenarioMgr() { }
        ~ScenarioMgr() { }

    public:
        static ScenarioMgr* Instance()
        {
            static ScenarioMgr instance;
            return &instance;
        }

        void LoadDB2Data();
        void LoadScenarioPOI();

        uint32 GetMaxStepsForScenario(uint32 mapId) const;
        uint32 GetScenarioIdForMap(uint32 mapId) const;
        uint32 GetScenarioIdForMap(Map const* map) const;
        uint32 GetScenarioIdForMap(uint32 mapId, DifficultyID difficulty, TeamId team) const;
        uint32 GetCriteriaIdForScenarioStep(uint32 mapId, uint32 step) const;
        uint32 GetCriteriaIdForScenarioStep(Map const* map, uint32 step) const;

        ScenarioPOIVector const* GetScenarioPOIs(int32 criteriaTreeId) const;

        void SendScenarioState(Player* player, uint32 mapId, int32 step, uint32 startTime, uint32 miscCriteriaTreeId = 0, uint32 scenarioId = 0, uint32 bonusData = 0, ScenarioChallenge const* challengeData = NULL);
        void SendScenarioState(Map const* map, ScenarioData const& scenarioData, Player* player = NULL);
        void SendScenarioProgressUpdate(Map const* map, CriteriaProgressData const& progress, Player* player = NULL);

    private:
        void WriteScenarioState(WorldPacket& data, uint32 scenarioId, int32 step, uint32 bonusData, uint32 waveMax, uint32 timer, uint32 medal, uint32 waveCurrent, std::vector<CriteriaProgressData> const& progress) const;

        ScenarioDataContainer _scenarioData;
        ScenarioPOIContainer _scenarioPOIStore;
};

#define sScenarioMgr ScenarioMgr::Instance()

#endif
