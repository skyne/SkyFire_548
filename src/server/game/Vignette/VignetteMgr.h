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

#ifndef SF_VIGNETTE_MGR_H
#define SF_VIGNETTE_MGR_H

#include "Vignette.h"

#include <functional>
#include <map>
#include <set>

class WorldObject;
class Player;

namespace Vignette
{
    typedef std::map<uint64, Entity*> VignetteContainer;

    class Manager
    {
        public:
            explicit Manager(Player const* player);
            ~Manager();

            /**
             * Create a minimap marker for this player.
             * vignetteId is the client Vignette.db2 id (icon and name live on the client).
             * sourceGuid, when set, is followed during Player::Update and removed when that object leaves visibility,
             * unless type is SourceScript.
             * Returns nullptr when vignetteId is 0 or this source (or this id, for a marker with no source) is already tracked.
             */
            Entity* CreateAndAddVignette(uint32 vignetteId, uint32 mapId, Type vignetteType, float x, float y, float z, uint64 sourceGuid = 0, uint32 trackingQuestId = 0);

            // Remove every marker with this client vignette id.
            void DestroyAndRemoveVignetteById(uint32 vignetteId);

            // Remove every marker that follows this creature or gameobject.
            void DestroyAndRemoveVignetteBySource(uint64 sourceGuid);

            void DestroyAndRemoveVignettes(std::function<bool(Entity const*)> const& predicate);

            // Called from Player::Update, which Map::Update drives for every in-world player.
            void Update();

            // Called from Player::UpdateVisibilityOf when an object enters the client's set.
            template<class T>
            void OnWorldObjectAppear(T const* target);

            // Called from Player::UpdateVisibilityOf and WorldObject::DestroyForNearbyPlayers.
            template<class T>
            void OnWorldObjectDisappear(T const* target);

        private:
            void SendVignetteUpdateToClient();
            void Untrack(VignetteContainer::iterator itr);

            Player const*     m_Owner;
            VignetteContainer m_Vignettes;
            std::set<uint64>  m_RemovedVignette;
            std::set<uint64>  m_AddedVignette;
            std::set<uint64>  m_UpdatedVignette;
    };
}

#endif
