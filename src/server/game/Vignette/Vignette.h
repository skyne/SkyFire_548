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

#ifndef SF_VIGNETTE_H
#define SF_VIGNETTE_H

#include "Common.h"

namespace Vignette
{
    // True when vignetteId is a row in Vignette.db2. Id 0 is not a row.
    // An unknown non-zero id is logged once and returns false.
    bool HasVignetteEntry(uint32 vignetteId);

    // Internal only. The client never sees this; it decides how a marker is added and removed.
    enum class Type : uint8
    {
        SourceCreature,           // Linked to a creature. Shown while that creature is visible.
        SourceGameObject,         // Linked to a gameobject. Shown while that gameobject is visible.
        SourceRare,               // Linked to a rare. Hidden once the tracking quest is rewarded.
        SourceTreasure,           // Linked to a treasure. Hidden once the tracking quest is rewarded.
        SourceScript              // Created by a script. Never removed by visibility.
    };

    // One minimap marker. The client looks the vignette id up in its own Vignette.db2.
    class Entity
    {
        friend class Manager;

        public:
            void UpdatePosition(float x, float y, float z);

            void ResetNeedClientUpdate() { m_NeedClientUpdate = false; }

            uint64 GetGuid() const { return m_Guid; }
            uint64 GetSourceGuid() const { return m_SourceGuid; }
            uint32 GetMapId() const { return m_Map; }
            uint32 GetVignetteId() const { return m_VignetteId; }
            uint32 GetTrackingQuestId() const { return m_TrackingQuestId; }
            Type GetVignetteType() const { return m_Type; }
            bool NeedClientUpdate() const { return m_NeedClientUpdate; }

            float GetPositionX() const { return m_PositionX; }
            float GetPositionY() const { return m_PositionY; }
            float GetPositionZ() const { return m_PositionZ; }

        private:
            Entity(uint32 vignetteId, uint32 mapId);
            ~Entity();

            void Create(Type type, float x, float y, float z, uint64 sourceGuid, uint32 trackingQuestId);

            uint64       m_Guid;                 // GUID_TYPE_VIGNETTE / HIGHGUID_VIGNETTE
            uint64       m_SourceGuid;           // Creature or gameobject this marker follows. 0 for a script marker.
            uint32 const m_Map;
            uint32       m_VignetteId;           // Client Vignette.db2 id
            uint32       m_TrackingQuestId;      // Rewarded quest hides the marker. 0 means none.
            float        m_PositionX;
            float        m_PositionY;
            float        m_PositionZ;
            Type         m_Type;

        protected:
            bool         m_NeedClientUpdate;
    };
}

#endif
