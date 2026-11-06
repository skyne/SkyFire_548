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

// SkyFire adaptation: answers CMSG_QUERY_SCENARIO_POI from the scenario POI tables.

#include "ScenarioMgr.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <unordered_set>

void WorldSession::HandleQueryScenarioPOI(WorldPacket& recvData)
{
    std::unordered_set<int32> criteriaTreeIds;
    uint32 missingScenarioPOIsCount = recvData.ReadBits(22);
    for (uint32 i = 0; i < missingScenarioPOIsCount; ++i)
        criteriaTreeIds.insert(recvData.read<uint32>());

    uint32 count = 0;
    ByteBuffer dataBuff;
    WorldPacket data(SMSG_SCENARIO_POIS);
    size_t writePos = data.bitwpos();
    data.WriteBits(count, 21);

    for (std::unordered_set<int32>::const_iterator itr = criteriaTreeIds.begin(); itr != criteriaTreeIds.end(); ++itr)
    {
        ScenarioPOIVector const* scenarioPOIs = sScenarioMgr->GetScenarioPOIs(*itr);
        if (!scenarioPOIs)
            continue;

        data.WriteBits(scenarioPOIs->size(), 19);
        for (ScenarioPOIVector::const_iterator poi = scenarioPOIs->begin(); poi != scenarioPOIs->end(); ++poi)
        {
            data.WriteBits(poi->Points.size(), 21);

            dataBuff << int32(poi->BlobIndex);
            dataBuff << int32(poi->Floor);
            dataBuff << int32(poi->WorldEffectID);

            for (std::vector<ScenarioPOIPoint>::const_iterator point = poi->Points.begin(); point != poi->Points.end(); ++point)
            {
                dataBuff << int32(point->X);
                dataBuff << int32(point->Y);
            }

            dataBuff << int32(0);
            dataBuff << int32(poi->PlayerConditionID);
            dataBuff << int32(poi->MapID);
            dataBuff << int32(0);
            dataBuff << int32(poi->WorldMapAreaID);
        }

        dataBuff << int32(*itr);
        ++count;
    }

    data.FlushBits();
    data.PutBits(writePos, count, 21);
    data.append(dataBuff);
    SendPacket(&data);
}
