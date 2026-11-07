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

#include "VignetteMgr.h"
#include "AreaTrigger.h"
#include "Corpse.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"

namespace
{
    Vignette::Type TypeForWorldObject(WorldObject const* target, uint32 trackingQuestId)
    {
        switch (target->GetTypeId())
        {
            case TypeID::TYPEID_UNIT:
                return trackingQuestId ? Vignette::Type::SourceRare : Vignette::Type::SourceCreature;
            case TypeID::TYPEID_GAMEOBJECT:
                return trackingQuestId ? Vignette::Type::SourceTreasure : Vignette::Type::SourceGameObject;
            default:
                return Vignette::Type::SourceCreature;
        }
    }
}

namespace Vignette
{
    Manager::Manager(Player const* player) : m_Owner(player)
    {
    }

    Manager::~Manager()
    {
        for (VignetteContainer::iterator itr = m_Vignettes.begin(); itr != m_Vignettes.end(); ++itr)
            delete itr->second;

        m_Vignettes.clear();
        m_Owner = NULL;
    }

    Entity* Manager::CreateAndAddVignette(uint32 vignetteId, uint32 mapId, Type vignetteType, float x, float y, float z, uint64 sourceGuid, uint32 trackingQuestId)
    {
        if (!HasVignetteEntry(vignetteId))
            return NULL;

        for (VignetteContainer::const_iterator itr = m_Vignettes.begin(); itr != m_Vignettes.end(); ++itr)
        {
            Entity const* existing = itr->second;

            // Several rares and chests share one client vignette id (the icon). Dedupe the source, not the id.
            if (sourceGuid && existing->GetSourceGuid() == sourceGuid)
                return NULL;

            if (!sourceGuid && !existing->GetSourceGuid() && existing->GetVignetteId() == vignetteId)
                return NULL;
        }

        Entity* vignette = new Entity(vignetteId, mapId);
        vignette->Create(vignetteType, x, y, z, sourceGuid, trackingQuestId);

        m_Vignettes.insert(std::make_pair(vignette->GetGuid(), vignette));
        m_AddedVignette.insert(vignette->GetGuid());
        return vignette;
    }

    void Manager::Untrack(VignetteContainer::iterator itr)
    {
        uint64 guid = itr->first;
        delete itr->second;
        m_Vignettes.erase(itr);

        // Never queued to the client, so a remove would refer to a guid the client does not have.
        if (m_AddedVignette.erase(guid))
        {
            m_UpdatedVignette.erase(guid);
            return;
        }

        m_UpdatedVignette.erase(guid);
        m_RemovedVignette.insert(guid);
    }

    void Manager::DestroyAndRemoveVignetteById(uint32 vignetteId)
    {
        if (!vignetteId)
            return;

        for (VignetteContainer::iterator itr = m_Vignettes.begin(); itr != m_Vignettes.end();)
        {
            if (itr->second->GetVignetteId() == vignetteId)
            {
                VignetteContainer::iterator next = itr;
                ++next;
                Untrack(itr);
                itr = next;
                continue;
            }

            ++itr;
        }
    }

    void Manager::DestroyAndRemoveVignetteBySource(uint64 sourceGuid)
    {
        if (!sourceGuid)
            return;

        for (VignetteContainer::iterator itr = m_Vignettes.begin(); itr != m_Vignettes.end();)
        {
            if (itr->second->GetSourceGuid() == sourceGuid && itr->second->GetVignetteType() != Type::SourceScript)
            {
                VignetteContainer::iterator next = itr;
                ++next;
                Untrack(itr);
                itr = next;
                continue;
            }

            ++itr;
        }
    }

    void Manager::DestroyAndRemoveVignettes(std::function<bool(Entity const*)> const& predicate)
    {
        for (VignetteContainer::iterator itr = m_Vignettes.begin(); itr != m_Vignettes.end();)
        {
            if (predicate(itr->second))
            {
                VignetteContainer::iterator next = itr;
                ++next;
                Untrack(itr);
                itr = next;
                continue;
            }

            ++itr;
        }
    }

    void Manager::SendVignetteUpdateToClient()
    {
        if (!m_Owner->GetSession())
            return;

        uint32 addedVignetteCount = 0;
        uint32 updatedVignetteCount = 0;
        ByteBuffer playersAddBuffer, playersUpdateBuffer, playersRemoveBuffer, objectAddBuffer, objectUpdateBuffer;
        WorldPacket data(SMSG_VIGNETTE_UPDATE);

        // Removed guid count.
        data.WriteBits(m_RemovedVignette.size(), 24);
        data.WriteBit(false);                                 // ForceUpdate
        // Updated guid count. Mask order is the 5.4.8 client order.
        data.WriteBits(m_UpdatedVignette.size(), 24);
        for (std::set<uint64>::const_iterator itr = m_UpdatedVignette.begin(); itr != m_UpdatedVignette.end(); ++itr)
        {
            ObjectGuid vignetteGuid(*itr);
            data.WriteGuidMask(vignetteGuid, 4, 7, 3, 2, 6, 0, 5, 1);
            playersUpdateBuffer.WriteGuidBytes(vignetteGuid, 6, 2, 5, 0, 3, 4, 1, 7);
        }

        for (std::set<uint64>::const_iterator itr = m_RemovedVignette.begin(); itr != m_RemovedVignette.end(); ++itr)
        {
            ObjectGuid vignetteGuid(*itr);
            data.WriteGuidMask(vignetteGuid, 5, 4, 1, 7, 0, 6, 2, 3);
            playersRemoveBuffer.WriteGuidBytes(vignetteGuid, 4, 0, 6, 7, 5, 3, 1, 2);
        }
        m_RemovedVignette.clear();

        // Added guid count.
        data.WriteBits(m_AddedVignette.size(), 24);
        for (std::set<uint64>::const_iterator itr = m_AddedVignette.begin(); itr != m_AddedVignette.end(); ++itr)
        {
            ObjectGuid vignetteGuid(*itr);
            data.WriteGuidMask(vignetteGuid, 1, 4, 3, 6, 2, 0, 7, 5);
            playersAddBuffer.WriteGuidBytes(vignetteGuid, 7, 1, 0, 6, 2, 3, 4, 5);
        }

        // Added vignette record count. Filled in after the loop once skipped guids are known.
        size_t addedVignetteCountPos = data.bitwpos();
        data.WriteBits(addedVignetteCount, 20);

        for (std::set<uint64>::const_iterator itr = m_AddedVignette.begin(); itr != m_AddedVignette.end(); ++itr)
        {
            VignetteContainer::iterator findResult = m_Vignettes.find(*itr);
            if (findResult == m_Vignettes.end())
                continue;

            ++addedVignetteCount;

            Entity* vignette = findResult->second;
            ObjectGuid objectGuid(vignette->GetGuid());

            data.WriteGuidMask(objectGuid, 5, 3, 7, 4, 2, 0, 6, 1);
            objectAddBuffer.WriteGuidBytes(objectGuid, 2, 5);
            objectAddBuffer << float(vignette->GetPositionZ());
            objectAddBuffer << uint32(vignette->GetVignetteId());   // Vignette.db2 id
            objectAddBuffer << float(vignette->GetPositionY());
            objectAddBuffer.WriteGuidBytes(objectGuid, 1);
            objectAddBuffer << float(vignette->GetPositionX());
            objectAddBuffer.WriteGuidBytes(objectGuid, 6, 7, 4, 3, 0);
        }
        m_AddedVignette.clear();

        size_t updatedVignetteCountPos = data.bitwpos();
        data.WriteBits(updatedVignetteCount, 20);

        for (std::set<uint64>::const_iterator itr = m_UpdatedVignette.begin(); itr != m_UpdatedVignette.end(); ++itr)
        {
            VignetteContainer::iterator findResult = m_Vignettes.find(*itr);
            if (findResult == m_Vignettes.end())
                continue;

            ++updatedVignetteCount;

            Entity* vignette = findResult->second;
            ObjectGuid objectGuid(vignette->GetGuid());

            data.WriteGuidMask(objectGuid, 3, 5, 2, 6, 4, 0, 1, 7);
            objectUpdateBuffer.WriteGuidBytes(objectGuid, 5, 2);
            objectUpdateBuffer << float(vignette->GetPositionZ());
            objectUpdateBuffer << float(vignette->GetPositionX());
            objectUpdateBuffer.WriteGuidBytes(objectGuid, 1, 4, 6, 0);
            objectUpdateBuffer << uint32(vignette->GetVignetteId()); // Vignette.db2 id
            objectUpdateBuffer << float(vignette->GetPositionY());
            objectUpdateBuffer.WriteGuidBytes(objectGuid, 3, 7);
        }
        m_UpdatedVignette.clear();

        data.FlushBits();

        data.PutBits(addedVignetteCountPos, addedVignetteCount, 20);
        data.PutBits(updatedVignetteCountPos, updatedVignetteCount, 20);

        // Byte blocks follow the bit block in this order. Empty blocks are omitted.
        if (objectUpdateBuffer.size())
            data.append(objectUpdateBuffer);
        if (playersRemoveBuffer.size())
            data.append(playersRemoveBuffer);
        if (objectAddBuffer.size())
            data.append(objectAddBuffer);
        if (playersAddBuffer.size())
            data.append(playersAddBuffer);
        if (playersUpdateBuffer.size())
            data.append(playersUpdateBuffer);

        m_Owner->GetSession()->SendPacket(&data);
    }

    void Manager::Update()
    {
        if (!m_Owner->IsInWorld())
            return;

        uint32 const mapId = m_Owner->GetMapId();
        DestroyAndRemoveVignettes([this, mapId](Entity const* vignette) -> bool
        {
            if (vignette->GetMapId() != mapId)
                return true;

            uint32 trackingQuestId = vignette->GetTrackingQuestId();
            return trackingQuestId && m_Owner->GetQuestRewardStatus(trackingQuestId);
        });

        for (VignetteContainer::iterator itr = m_Vignettes.begin(); itr != m_Vignettes.end(); ++itr)
        {
            Entity* vignette = itr->second;
            uint64 sourceGuid = vignette->GetSourceGuid();

            if (IS_UNIT_GUID(sourceGuid))
            {
                if (Unit* source = ObjectAccessor::GetUnit(*m_Owner, sourceGuid))
                    vignette->UpdatePosition(source->GetPositionX(), source->GetPositionY(), source->GetPositionZ());
            }
            else if (IS_GAMEOBJECT_GUID(sourceGuid))
            {
                if (GameObject* source = ObjectAccessor::GetGameObject(*m_Owner, sourceGuid))
                    vignette->UpdatePosition(source->GetPositionX(), source->GetPositionY(), source->GetPositionZ());
            }

            if (vignette->NeedClientUpdate())
            {
                m_UpdatedVignette.insert(vignette->GetGuid());
                vignette->ResetNeedClientUpdate();
            }
        }

        if (!m_AddedVignette.empty() || !m_UpdatedVignette.empty() || !m_RemovedVignette.empty())
            SendVignetteUpdateToClient();
    }

    template <class T>
    void Manager::OnWorldObjectAppear(T const* target)
    {
        uint32 vignetteId = target->GetVignetteId();
        if (!vignetteId)
            return;

        uint32 trackingQuestId = target->GetVignetteTrackingQuest();
        if (trackingQuestId && m_Owner->GetQuestRewardStatus(trackingQuestId))
            return;

        CreateAndAddVignette(vignetteId, target->GetMapId(), TypeForWorldObject(target, trackingQuestId),
            target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), target->GetGUID(), trackingQuestId);
    }

    template <class T>
    void Manager::OnWorldObjectDisappear(T const* target)
    {
        if (!target->GetVignetteId() && !target->GetGUID())
            return;

        DestroyAndRemoveVignetteBySource(target->GetGUID());
    }

    template void Manager::OnWorldObjectDisappear(Corpse const*);
    template void Manager::OnWorldObjectDisappear(Creature const*);
    template void Manager::OnWorldObjectDisappear(GameObject const*);
    template void Manager::OnWorldObjectDisappear(DynamicObject const*);
    template void Manager::OnWorldObjectDisappear(AreaTrigger const*);
    template void Manager::OnWorldObjectDisappear(WorldObject const*);
    template void Manager::OnWorldObjectDisappear(Player const*);

    template void Manager::OnWorldObjectAppear(Corpse const*);
    template void Manager::OnWorldObjectAppear(Creature const*);
    template void Manager::OnWorldObjectAppear(GameObject const*);
    template void Manager::OnWorldObjectAppear(DynamicObject const*);
    template void Manager::OnWorldObjectAppear(AreaTrigger const*);
    template void Manager::OnWorldObjectAppear(WorldObject const*);
    template void Manager::OnWorldObjectAppear(Player const*);
}
