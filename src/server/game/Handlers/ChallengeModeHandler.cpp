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
* SkyFire adaptation: reward and leaderboard replies read stored challenge times.
*/

#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <ctime>

namespace
{
    void WriteGuildLeader(ByteBuffer& data, ByteBuffer& buffer, RealmCompletedChallenge const& challenge)
    {
        data.WriteBits(challenge.MembersCount, 20);
        for (uint8 i = 0; i < challenge.MembersCount && i < 5; ++i)
        {
            ObjectGuid guid = MAKE_NEW_GUID(challenge.Members[i].Guid, 0, HIGHGUID_PLAYER);
            data.WriteGuidMask(guid, 5, 2, 3, 0, 7, 1, 4, 6);

            buffer.WriteGuidBytes(guid, 3);
            buffer << uint32(challenge.Members[i].SpecId);
            buffer << uint32(realmID);
            buffer.WriteGuidBytes(guid, 1, 5, 0);
            buffer << uint32(realmID);
            buffer.WriteGuidBytes(guid, 4, 7, 6, 2);
        }

        buffer << uint32(challenge.AttemptId);
        buffer << uint32(challenge.MedalEarned);
        buffer << uint32(realmID);
        buffer << uint32(challenge.CompletionTime);
        buffer << uint32(challenge.CompletionDate);
    }

    void WriteRealmLeader(ByteBuffer& data, ByteBuffer& buffer, RealmCompletedChallenge const& challenge)
    {
        data.WriteBits(challenge.MembersCount, 20);
        for (uint8 i = 0; i < challenge.MembersCount && i < 5; ++i)
        {
            ObjectGuid guid = MAKE_NEW_GUID(challenge.Members[i].Guid, 0, HIGHGUID_PLAYER);
            data.WriteGuidMask(guid, 5, 3, 0, 1, 2, 4, 6, 7);

            buffer.WriteGuidBytes(guid, 4, 3, 6, 7, 2, 1);
            buffer << uint32(realmID);
            buffer << uint32(challenge.Members[i].SpecId);
            buffer << uint32(realmID);
            buffer.WriteGuidBytes(guid, 0, 5);
        }

        buffer << uint32(challenge.CompletionDate);
        buffer << uint32(realmID);
        buffer << uint32(challenge.AttemptId);
        buffer << uint32(challenge.CompletionTime);
        buffer << uint32(challenge.MedalEarned);
    }
}

void WorldSession::HandleGetChallengeModeRewards(WorldPacket& /*recvData*/)
{
    SF_LOG_DEBUG("network", "CMSG_GET_CHALLENGE_MODE_REWARDS %s", GetPlayerInfo().c_str());

    std::vector<ChallengeReward> rewards = sObjectMgr->GetChallengeRewards();
    ByteBuffer buffer;
    WorldPacket data(SMSG_CHALLENGE_MODE_REWARDS);
    data.WriteBits(rewards.size(), 21);

    for (std::vector<ChallengeReward>::const_iterator reward = rewards.begin(); reward != rewards.end(); ++reward)
    {
        data.WriteBits(4, 20);
        for (uint32 i = 0; i < 4; ++i)
        {
            data.WriteBits(0, 21);
            data.WriteBits(0, 20);
            buffer << uint32(reward->MoneyReward[i]);
        }
        buffer << uint32(reward->MapId);
    }

    data.WriteBits(0, 20);
    data.FlushBits();
    if (!buffer.empty())
        data.append(buffer);

    SendPacket(&data);
}

void WorldSession::HandleChallengeModeRequestLeaders(WorldPacket& recvData)
{
    uint32 mapId = recvData.read<uint32>();
    recvData.read_skip<uint32>(); // Last guild update
    recvData.read_skip<uint32>(); // Last realm update

    SF_LOG_DEBUG("network", "CMSG_CHALLENGE_MODE_REQUEST_LEADERS %s map: %u", GetPlayerInfo().c_str(), mapId);

    RealmCompletedChallenge realmChallenge;
    RealmCompletedChallenge guildChallenge;
    bool const hasRealm = sObjectMgr->GetRealmCompletedChallenge(mapId, realmChallenge);
    bool const hasGuild = _player && sObjectMgr->GetGuildCompletedChallenge(mapId, _player->GetGuildId(), guildChallenge);

    ByteBuffer realmBuffer;
    ByteBuffer guildBuffer;
    WorldPacket data(SMSG_CHALLENGE_MODE_REQUEST_LEADERS_RESULT);
    data << uint32(time(nullptr));
    data << uint32(time(nullptr));
    data << uint32(mapId);

    data.WriteBits(hasGuild ? 1 : 0, 19);
    if (hasGuild)
        WriteGuildLeader(data, guildBuffer, guildChallenge);

    data.WriteBits(hasRealm ? 1 : 0, 19);
    if (hasRealm)
        WriteRealmLeader(data, realmBuffer, realmChallenge);

    data.FlushBits();
    if (!realmBuffer.empty())
        data.append(realmBuffer);
    if (!guildBuffer.empty())
        data.append(guildBuffer);

    SendPacket(&data);
}

void WorldSession::HandleChallengeModeRequestMapStats(WorldPacket& /*recvData*/)
{
    SF_LOG_DEBUG("network", "CMSG_CHALLENGE_MODE_REQUEST_MAP_STATS %s", GetPlayerInfo().c_str());

    std::map<uint32, CompletedChallenge> challenges;
    if (_player)
        sObjectMgr->GetCharacterCompletedChallenges(_player->GetGUIDLow(), challenges);

    ByteBuffer dataBuffer;
    WorldPacket data(SMSG_CHALLENGE_MODE_ALL_MAP_STATS);
    data.WriteBits(challenges.size(), 19);

    for (std::map<uint32, CompletedChallenge>::const_iterator itr = challenges.begin(); itr != challenges.end(); ++itr)
    {
        data.WriteBits(0, 23);
        dataBuffer.AppendPackedTime(itr->second.BestMedalDate);
        dataBuffer << int32(itr->second.BestMedal);
        dataBuffer << int32(itr->first);
        dataBuffer << int32(itr->second.LastTime);
        dataBuffer << int32(itr->second.BestTime);
    }

    data.FlushBits();
    if (!dataBuffer.empty())
        data.append(dataBuffer);

    SendPacket(&data);
}

void WorldSession::SendChallengeModeMapStatsUpdate(uint32 mapId)
{
    if (!_player)
        return;

    std::map<uint32, CompletedChallenge> challenges;
    sObjectMgr->GetCharacterCompletedChallenges(_player->GetGUIDLow(), challenges);
    std::map<uint32, CompletedChallenge>::const_iterator itr = challenges.find(mapId);
    if (itr == challenges.end())
        return;

    WorldPacket data(SMSG_CHALLENGE_MODE_MAP_STATS_UPDATE);
    data.WriteBits(0, 23);
    data << int32(itr->second.LastTime);
    data.AppendPackedTime(itr->second.BestMedalDate);
    data << int32(mapId);
    data << int32(itr->second.BestTime);
    data << int32(itr->second.BestMedal);
    SendPacket(&data);
}
