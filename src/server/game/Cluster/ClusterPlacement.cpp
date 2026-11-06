/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "ClusterPlacement.h"
#include "ClusterMgr.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "SocialMgr.h"
#include <unordered_map>
#include <vector>

namespace
{
    struct LiveLayer
    {
        uint32 nodeId;
        uint32 population;
    };

    struct GroupChoice
    {
        bool set;
        uint32 transfers;
        uint32 friends;
        uint32 guild;
        bool leader;
        uint32 population;
        uint32 nodeId;
        size_t index;
    };

    void CollectGuids(PreparedQueryResult result, std::vector<uint32>& guids)
    {
        if (!result)
            return;

        do
        {
            guids.push_back(result->Fetch()[0].GetUInt32());
        } while (result->NextRow());
    }

    void CountOnLayers(std::vector<uint32> const& guids, std::unordered_map<uint32, uint32> const& routeByGuid, std::unordered_map<uint32, size_t> const& nodeToIndex, std::vector<uint32>& counts)
    {
        for (uint32 guid : guids)
        {
            std::unordered_map<uint32, uint32>::const_iterator route = routeByGuid.find(guid);
            if (route == routeByGuid.end())
                continue;

            std::unordered_map<uint32, size_t>::const_iterator layer = nodeToIndex.find(route->second);
            if (layer == nodeToIndex.end())
                continue;

            ++counts[layer->second];
        }
    }

    bool LoadLiveLayers(uint32 realmId, uint32 mapId, uint32 staleSeconds, std::vector<LiveLayer>& layers, std::unordered_map<uint32, size_t>& nodeToIndex)
    {
        QueryResult result = LoginDatabase.PQuery(
            "SELECT a.layer_id, a.node_id, a.population FROM cluster_map_assignment a "
            "JOIN cluster_node n ON n.realm_id = a.realm_id AND n.node_id = a.node_id "
            "WHERE a.realm_id = %u AND a.map_id = %u AND n.state = 2 AND n.heartbeat + %u >= UNIX_TIMESTAMP()",
            realmId, mapId, staleSeconds);
        if (!result)
            return false;

        do
        {
            Field* fields = result->Fetch();
            uint32 nodeId = fields[1].GetUInt32();
            if (nodeToIndex.find(nodeId) != nodeToIndex.end())
                continue;

            nodeToIndex[nodeId] = layers.size();
            LiveLayer layer;
            layer.nodeId = nodeId;
            layer.population = fields[2].GetUInt32();
            layers.push_back(layer);
        } while (result->NextRow());

        return !layers.empty();
    }

    void LoadMapRoutes(uint32 realmId, uint32 mapId, std::unordered_map<uint32, uint32>& routeByGuid)
    {
        QueryResult result = LoginDatabase.PQuery(
            "SELECT character_guid, node_id FROM cluster_account_route WHERE realm_id = %u AND map_id = %u",
            realmId, mapId);
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            routeByGuid[fields[0].GetUInt32()] = fields[1].GetUInt32();
        } while (result->NextRow());
    }

    bool LoadOwnRoute(uint32 accountId, uint32 characterGuid, uint32 realmId, uint32& nodeId)
    {
        QueryResult result = LoginDatabase.PQuery(
            "SELECT node_id FROM cluster_account_route WHERE account_id = %u AND character_guid = %u AND realm_id = %u",
            accountId, characterGuid, realmId);
        if (!result)
            return false;

        nodeId = result->Fetch()[0].GetUInt32();
        return true;
    }

    void FillResult(LiveLayer const& layer, uint32 transfers, ClusterPlaceResult& out)
    {
        out.nodeId = layer.nodeId;
        out.layerId = layer.nodeId;
        out.population = layer.population;
        out.transfers = transfers;
    }

    bool CorpsePinsLayer(uint32 characterGuid, uint32 ownNode, bool hasOwnRoute, std::vector<LiveLayer> const& layers, std::unordered_map<uint32, size_t> const& nodeToIndex, uint32 mapId, uint32 hardCap, ClusterPlaceResult& out)
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLUSTER_CORPSE_MAP);
        stmt->setUInt32(0, characterGuid);
        PreparedQueryResult result = CharacterDatabase.Query(stmt);
        if (!result)
            return false;

        bool onMap = false;
        do
        {
            if (result->Fetch()[0].GetUInt32() == mapId)
                onMap = true;
        } while (result->NextRow());

        if (!onMap || !hasOwnRoute)
            return false;

        std::unordered_map<uint32, size_t>::const_iterator layer = nodeToIndex.find(ownNode);
        if (layer == nodeToIndex.end())
            return false;

        if (layers[layer->second].population >= hardCap)
            return false;

        FillResult(layers[layer->second], 0, out);
        return true;
    }

    bool PreferGroupChoice(GroupChoice const& candidate, GroupChoice const& current)
    {
        if (!current.set)
            return true;
        if (candidate.transfers != current.transfers)
            return candidate.transfers < current.transfers;
        if (candidate.friends != current.friends)
            return candidate.friends > current.friends;
        if (candidate.guild != current.guild)
            return candidate.guild > current.guild;
        if (candidate.leader != current.leader)
            return candidate.leader;
        if (candidate.population != current.population)
            return candidate.population < current.population;
        return candidate.nodeId < current.nodeId;
    }

    bool ChooseGroup(std::vector<uint32> const& membersOnMap, uint32 leaderGuid, bool haveLeader, std::vector<LiveLayer> const& layers, std::unordered_map<uint32, uint32> const& routeByGuid, std::vector<uint32> const& friends, std::vector<uint32> const& guild, uint32 hardCap, ClusterPlaceResult& out)
    {
        uint32 leaderNode = 0;
        bool leaderOnMap = false;
        if (haveLeader)
        {
            for (uint32 guid : membersOnMap)
            {
                if (guid != leaderGuid)
                    continue;

                std::unordered_map<uint32, uint32>::const_iterator route = routeByGuid.find(guid);
                if (route == routeByGuid.end())
                    break;

                leaderOnMap = true;
                leaderNode = route->second;
                break;
            }
        }

        GroupChoice whole;
        whole.set = false;
        GroupChoice alone;
        alone.set = false;

        for (size_t i = 0; i < layers.size(); ++i)
        {
            uint32 already = 0;
            for (uint32 guid : membersOnMap)
            {
                std::unordered_map<uint32, uint32>::const_iterator route = routeByGuid.find(guid);
                if (route != routeByGuid.end() && route->second == layers[i].nodeId)
                    ++already;
            }

            uint32 transfers = uint32(membersOnMap.size()) - already;
            GroupChoice candidate;
            candidate.set = true;
            candidate.transfers = transfers;
            candidate.friends = friends[i];
            candidate.guild = guild[i];
            candidate.leader = leaderOnMap && leaderNode == layers[i].nodeId;
            candidate.population = layers[i].population;
            candidate.nodeId = layers[i].nodeId;
            candidate.index = i;

            if (uint64(layers[i].population) + transfers <= hardCap)
            {
                if (PreferGroupChoice(candidate, whole))
                    whole = candidate;
            }
            else if (uint64(layers[i].population) + 1 <= hardCap)
            {
                if (PreferGroupChoice(candidate, alone))
                    alone = candidate;
            }
        }

        GroupChoice const& chosen = whole.set ? whole : alone;
        if (!chosen.set)
            return false;

        FillResult(layers[chosen.index], chosen.transfers, out);
        return true;
    }

    bool ChooseSolo(uint32 ownNode, bool hasOwnRoute, std::vector<LiveLayer> const& layers, std::unordered_map<uint32, size_t> const& nodeToIndex, std::vector<uint32> const& friends, std::vector<uint32> const& guild, uint32 softCap, uint32 hardCap, ClusterPlaceResult& out)
    {
        bool foundScore = false;
        uint32 bestScore = 0;
        size_t bestScoreIndex = 0;

        for (size_t i = 0; i < layers.size(); ++i)
        {
            if (layers[i].population >= softCap || layers[i].population >= hardCap)
                continue;

            uint32 score = friends[i] * 1000 + guild[i];
            if (score == 0)
                continue;

            if (!foundScore || score > bestScore || (score == bestScore && (layers[i].population < layers[bestScoreIndex].population || (layers[i].population == layers[bestScoreIndex].population && layers[i].nodeId < layers[bestScoreIndex].nodeId))))
            {
                foundScore = true;
                bestScore = score;
                bestScoreIndex = i;
            }
        }

        if (foundScore)
        {
            FillResult(layers[bestScoreIndex], 0, out);
            return true;
        }

        if (hasOwnRoute)
        {
            std::unordered_map<uint32, size_t>::const_iterator layer = nodeToIndex.find(ownNode);
            if (layer != nodeToIndex.end() && layers[layer->second].population < hardCap)
            {
                FillResult(layers[layer->second], 0, out);
                return true;
            }
        }

        bool foundUnderSoft = false;
        size_t fullestIndex = 0;
        for (size_t i = 0; i < layers.size(); ++i)
        {
            if (layers[i].population >= softCap || layers[i].population >= hardCap)
                continue;

            if (!foundUnderSoft || layers[i].population > layers[fullestIndex].population || (layers[i].population == layers[fullestIndex].population && layers[i].nodeId < layers[fullestIndex].nodeId))
            {
                foundUnderSoft = true;
                fullestIndex = i;
            }
        }

        if (foundUnderSoft)
        {
            FillResult(layers[fullestIndex], 0, out);
            return true;
        }

        bool foundUnderHard = false;
        size_t leastIndex = 0;
        for (size_t i = 0; i < layers.size(); ++i)
        {
            if (layers[i].population >= hardCap)
                continue;

            if (!foundUnderHard || layers[i].population < layers[leastIndex].population || (layers[i].population == layers[leastIndex].population && layers[i].nodeId < layers[leastIndex].nodeId))
            {
                foundUnderHard = true;
                leastIndex = i;
            }
        }

        if (!foundUnderHard)
            return false;

        FillResult(layers[leastIndex], 0, out);
        return true;
    }
}

bool ClusterPlacement::Choose(uint32 characterGuid, uint32 accountId, uint32 mapId, ClusterPlaceReason reason, ClusterPlaceResult& out)
{
    (void)reason;

    if (!sClusterMgr->IsEnabled())
        return false;

    MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
    if (!mapEntry || mapEntry->Instanceable())
        return false;

    uint32 realmId = uint32(sConfigMgr->GetIntDefault("Cluster.RealmID", 1));
    uint32 staleSeconds = uint32(sConfigMgr->GetIntDefault("Cluster.StaleSeconds", 30));
    uint32 softCap = sClusterMgr->GetLayerSoftCap();
    uint32 hardCap = sClusterMgr->GetLayerHardCap();

    std::vector<LiveLayer> layers;
    std::unordered_map<uint32, size_t> nodeToIndex;
    if (!LoadLiveLayers(realmId, mapId, staleSeconds, layers, nodeToIndex))
        return false;

    std::unordered_map<uint32, uint32> routeByGuid;
    LoadMapRoutes(realmId, mapId, routeByGuid);

    uint32 ownNode = 0;
    bool hasOwnRoute = LoadOwnRoute(accountId, characterGuid, realmId, ownNode);
    if (CorpsePinsLayer(characterGuid, ownNode, hasOwnRoute, layers, nodeToIndex, mapId, hardCap, out))
        return true;

    std::vector<uint32> friendGuids;
    PreparedStatement* friendsStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLUSTER_FRIENDS_ON_MAP);
    friendsStmt->setUInt32(0, characterGuid);
    friendsStmt->setUInt8(1, uint8(SOCIAL_FLAG_FRIEND));
    friendsStmt->setUInt32(2, mapId);
    CollectGuids(CharacterDatabase.Query(friendsStmt), friendGuids);

    std::vector<uint32> guildGuids;
    PreparedStatement* guildStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLUSTER_GUILD_MATES_ON_MAP);
    guildStmt->setUInt32(0, characterGuid);
    guildStmt->setUInt32(1, mapId);
    CollectGuids(CharacterDatabase.Query(guildStmt), guildGuids);

    std::vector<uint32> friendCounts(layers.size(), 0);
    std::vector<uint32> guildCounts(layers.size(), 0);
    CountOnLayers(friendGuids, routeByGuid, nodeToIndex, friendCounts);
    CountOnLayers(guildGuids, routeByGuid, nodeToIndex, guildCounts);

    uint32 groupId = 0;
    bool inGroup = false;
    PreparedStatement* groupStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GROUP_MEMBER);
    groupStmt->setUInt32(0, characterGuid);
    if (PreparedQueryResult groupResult = CharacterDatabase.Query(groupStmt))
    {
        groupId = groupResult->Fetch()[0].GetUInt32();
        inGroup = true;
    }

    std::vector<uint32> membersOnMap;
    uint32 leaderGuid = 0;
    bool haveLeader = false;
    if (inGroup)
    {
        PreparedStatement* membersStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLUSTER_PARTY_ONLINE_ON_MAP);
        membersStmt->setUInt32(0, groupId);
        membersStmt->setUInt32(1, mapId);
        CollectGuids(CharacterDatabase.Query(membersStmt), membersOnMap);

        PreparedStatement* leaderStmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLUSTER_PARTY_LEADER);
        leaderStmt->setUInt32(0, groupId);
        if (PreparedQueryResult leaderResult = CharacterDatabase.Query(leaderStmt))
        {
            leaderGuid = leaderResult->Fetch()[0].GetUInt32();
            haveLeader = true;
        }

        // Login reads characters.online before it is set, so the arriving player
        // is missing from the member list and would not consume a slot.
        bool listed = false;
        for (uint32 guid : membersOnMap)
        {
            if (guid == characterGuid)
                listed = true;
        }
        if (!listed)
            membersOnMap.push_back(characterGuid);
    }

    if (membersOnMap.size() > 1)
        return ChooseGroup(membersOnMap, leaderGuid, haveLeader, layers, routeByGuid, friendCounts, guildCounts, hardCap, out);

    return ChooseSolo(ownNode, hasOwnRoute, layers, nodeToIndex, friendCounts, guildCounts, softCap, hardCap, out);
}
