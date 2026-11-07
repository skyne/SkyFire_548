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
*
* SkyFire adaptation: per-player vignette markers without Vignette.db2 or creature_template columns.
*/

#include "Vignette.h"
#include "DB2Stores.h"
#include "Log.h"
#include "ObjectDefines.h"

#include <atomic>
#include <set>

namespace
{
    uint32 GenerateVignetteLowGuid()
    {
        static std::atomic<uint32> nextLowGuid(1);
        uint32 lowGuid = nextLowGuid.fetch_add(1, std::memory_order_relaxed);
        ASSERT(lowGuid < 0xFFFFFFFE && "Vignette guid overflow");
        return lowGuid;
    }
}

namespace Vignette
{
    bool HasVignetteEntry(uint32 vignetteId)
    {
        if (!vignetteId)
            return false;

        if (sVignetteStore.LookupEntry(vignetteId))
            return true;

        static std::set<uint32> loggedIds;
        if (loggedIds.insert(vignetteId).second)
            SF_LOG_ERROR("misc", "Vignette id %u is not in Vignette.db2; marker skipped.", vignetteId);

        return false;
    }

    Entity::Entity(uint32 vignetteId, uint32 mapId) : m_Map(mapId), m_VignetteId(vignetteId)
    {
        ASSERT(vignetteId);

        m_Guid = 0;
        m_SourceGuid = 0;
        m_TrackingQuestId = 0;
        m_PositionX = 0.0f;
        m_PositionY = 0.0f;
        m_PositionZ = 0.0f;
        m_Type = Type::SourceScript;
        m_NeedClientUpdate = false;
    }

    Entity::~Entity() { }

    void Entity::Create(Type type, float x, float y, float z, uint64 sourceGuid, uint32 trackingQuestId)
    {
        // Low guid, vignette id in the entry part, high guid 0x200. The 5.4.8 client treats that high part as a vignette.
        m_Guid = MAKE_NEW_GUID(GenerateVignetteLowGuid(), m_VignetteId, HIGHGUID_VIGNETTE);
        m_Type = type;
        m_PositionX = x;
        m_PositionY = y;
        m_PositionZ = z;
        m_SourceGuid = sourceGuid;
        m_TrackingQuestId = trackingQuestId;
    }

    void Entity::UpdatePosition(float x, float y, float z)
    {
        if ((int32)m_PositionX == (int32)x && (int32)m_PositionY == (int32)y)
            return;

        m_PositionX = x;
        m_PositionY = y;
        m_PositionZ = z;
        m_NeedClientUpdate = true;
    }
}
