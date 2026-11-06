/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "ClusterMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MapManager.h"
#include "World.h"
#include <cstdlib>
#include <set>
#include <sstream>

extern uint32 realmID;

namespace
{
    std::string TrimCopy(std::string value)
    {
        size_t start = value.find_first_not_of(" \t");
        if (start == std::string::npos)
            return std::string();
        size_t end = value.find_last_not_of(" \t");
        return value.substr(start, end - start + 1);
    }
}

ClusterMgr::ClusterMgr() :
    _enabled(false),
    _hideWorldPort(false),
    _ownsAllMaps(true),
    _nodeId(1),
    _linkPort(8086),
    _heartbeatInterval(5000),
    _staleSeconds(30),
    _migrationTimeoutMs(300000),
    _heartbeatTimer(0),
    _reconcileTimer(0)
{
}

void ClusterMgr::ParseAvailableMaps(std::string const& value)
{
    _availableMaps.clear();
    _ownsAllMaps = true;
    _availableMapsText = TrimCopy(value);
    if (_availableMapsText.empty())
        return;
    if (_availableMapsText == "-" || _availableMapsText == "none")
    {
        _ownsAllMaps = false;
        _availableMaps.clear();
        return;
    }

    _ownsAllMaps = false;
    std::stringstream stream(_availableMapsText);
    std::string token;
    while (std::getline(stream, token, ','))
    {
        token = TrimCopy(token);
        if (token.empty())
            continue;
        char* end = NULL;
        unsigned long mapId = std::strtoul(token.c_str(), &end, 10);
        if (!end || *end != '\0')
        {
            SF_LOG_ERROR("server.loading", "Cluster.AvailableMaps has invalid token '%s'.", token.c_str());
            continue;
        }
        _availableMaps.insert(uint32(mapId));
    }

    if (_availableMaps.empty())
    {
        SF_LOG_ERROR("server.loading", "Cluster.AvailableMaps parsed empty after invalid tokens; treating as all maps.");
        _ownsAllMaps = true;
        _availableMapsText.clear();
    }
}

void ClusterMgr::LoadConfig()
{
    _enabled = sConfigMgr->GetBoolDefault("Cluster.Enabled", false);
    _hideWorldPort = sConfigMgr->GetBoolDefault("Cluster.HideWorldPort", false);
    _nodeId = uint32(sConfigMgr->GetIntDefault("Cluster.NodeId", 1));
    _linkPort = uint32(sConfigMgr->GetIntDefault("Cluster.LinkPort", 8086));
    _heartbeatInterval = uint32(sConfigMgr->GetIntDefault("Cluster.HeartbeatInterval", 5000));
    _staleSeconds = uint32(sConfigMgr->GetIntDefault("Cluster.StaleSeconds", 30));
    _linkBindIP = sConfigMgr->GetStringDefault("Cluster.LinkBindIP", "127.0.0.1");
    _advertisedHost = sConfigMgr->GetStringDefault("Cluster.AdvertisedHost", _linkBindIP);
    _sharedToken = sConfigMgr->GetStringDefault("Cluster.SharedToken", "");
    ParseAvailableMaps(sConfigMgr->GetStringDefault("Cluster.AvailableMaps", ""));
    _migrationTimeoutMs = uint32(sConfigMgr->GetIntDefault("Cluster.MigrationTimeout", 300)) * 1000;

    if (_heartbeatInterval < 1000)
        _heartbeatInterval = 1000;
    if (_migrationTimeoutMs < 10000)
        _migrationTimeoutMs = 10000;
    if (_staleSeconds < 5)
        _staleSeconds = 5;
    if (_linkPort == 0 || _linkPort > 0xFFFF)
        _linkPort = 8086;
    if (_nodeId == 0)
        _nodeId = 1;
}

bool ClusterMgr::OwnsMap(uint32 mapId) const
{
    if (!_enabled || _ownsAllMaps)
        return true;

    std::lock_guard<std::mutex> guard(_lock);
    return _availableMaps.find(mapId) != _availableMaps.end();
}

bool ClusterMgr::Initialize()
{
    LoadConfig();
    if (!_enabled)
        return true;

    if (_sharedToken.empty())
    {
        SF_LOG_ERROR("server.loading", "Cluster.Enabled requires Cluster.SharedToken.");
        return false;
    }

    if (!RegisterNode())
        return false;

    if (!ClaimMaps())
        return false;

    Heartbeat();
    SF_LOG_INFO("server.loading", "Cluster node %u realm %u link %s:%u maps '%s'.",
        _nodeId, realmID, _advertisedHost.c_str(), _linkPort,
        _ownsAllMaps ? "*" : _availableMapsText.c_str());
    return true;
}

bool ClusterMgr::RegisterNode()
{
    PreparedStatement* wildcard = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_WILDCARD_NODE);
    wildcard->setUInt32(0, realmID);
    wildcard->setUInt32(1, _staleSeconds);
    wildcard->setUInt32(2, _nodeId);
    if (PreparedQueryResult other = LoginDatabase.Query(wildcard))
    {
        Field* fields = other->Fetch();
        SF_LOG_ERROR("server.loading",
            "Cluster node %u refused: live wildcard owner already exists as node %u (empty AvailableMaps is exclusive).",
            _nodeId, fields[0].GetUInt32());
        return false;
    }

    if (_ownsAllMaps)
    {
        PreparedStatement* live = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_LIVE_NODE);
        live->setUInt32(0, realmID);
        live->setUInt32(1, _staleSeconds);
        live->setUInt32(2, _nodeId);
        if (PreparedQueryResult other = LoginDatabase.Query(live))
        {
            Field* fields = other->Fetch();
            SF_LOG_ERROR("server.loading",
                "Cluster node %u refused: another live node %u exists; wildcard AvailableMaps would dual-own maps.",
                _nodeId, fields[0].GetUInt32());
            return false;
        }
    }

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_CLUSTER_NODE);
    stmt->setUInt32(0, _nodeId);
    stmt->setUInt32(1, realmID);
    stmt->setString(2, _advertisedHost);
    stmt->setUInt16(3, uint16(_linkPort));
    stmt->setString(4, _availableMapsText);
    stmt->setUInt8(5, 2);
    LoginDatabase.DirectExecute(stmt);
    return true;
}

bool ClusterMgr::ClaimMaps()
{
    if (_ownsAllMaps)
        return true;

    std::lock_guard<std::mutex> guard(_lock);
    for (uint32 mapId : _availableMaps)
    {
        PreparedStatement* existing = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_MAP_OWNER);
        existing->setUInt32(0, realmID);
        existing->setUInt32(1, mapId);
        if (PreparedQueryResult result = LoginDatabase.Query(existing))
        {
            Field* fields = result->Fetch();
            uint32 owner = fields[0].GetUInt32();
            uint32 heartbeat = fields[1].GetUInt32();
            uint32 now = uint32(time(NULL));
            bool stale = heartbeat + _staleSeconds < now;
            if (owner != _nodeId && !stale)
            {
                SF_LOG_ERROR("server.loading", "Map %u is already owned by live node %u.", mapId, owner);
                return false;
            }
        }

        PreparedStatement* claim = LoginDatabase.GetPreparedStatement(LOGIN_REP_CLUSTER_MAP);
        claim->setUInt32(0, realmID);
        claim->setUInt32(1, mapId);
        claim->setUInt32(2, _nodeId);
        LoginDatabase.DirectExecute(claim);
    }
    return true;
}

void ClusterMgr::Heartbeat()
{
    if (!_enabled)
        return;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_CLUSTER_NODE_HEARTBEAT);
    stmt->setUInt32(0, _nodeId);
    stmt->setUInt32(1, realmID);
    LoginDatabase.Execute(stmt);
}

void ClusterMgr::ReconcileMaps()
{
    if (!_enabled)
        return;

    PreparedStatement* textStmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_NODE_MAPS_TEXT);
    textStmt->setUInt32(0, realmID);
    textStmt->setUInt32(1, _nodeId);
    PreparedQueryResult textResult = LoginDatabase.Query(textStmt);
    if (!textResult)
        return;

    std::string mapsText = textResult->Fetch()[0].GetString();

    std::unordered_set<uint32> assigned;
    PreparedStatement* mapsStmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_MAPS_FOR_NODE);
    mapsStmt->setUInt32(0, realmID);
    mapsStmt->setUInt32(1, _nodeId);
    if (PreparedQueryResult mapsResult = LoginDatabase.Query(mapsStmt))
    {
        do
            assigned.insert(mapsResult->Fetch()[0].GetUInt32());
        while (mapsResult->NextRow());
    }

    bool changed = false;
    {
        std::lock_guard<std::mutex> guard(_lock);
        bool prevAll = _ownsAllMaps;
        std::unordered_set<uint32> prev = _availableMaps;
        ParseAvailableMaps(mapsText);
        if (!_ownsAllMaps && !assigned.empty())
        {
            _availableMaps = assigned;
            std::ostringstream csv;
            bool first = true;
            for (uint32 mapId : std::set<uint32>(_availableMaps.begin(), _availableMaps.end()))
            {
                if (!first)
                    csv << ',';
                first = false;
                csv << mapId;
            }
            _availableMapsText = csv.str();
        }
        changed = prevAll != _ownsAllMaps || prev != _availableMaps;
    }

    if (!changed)
        return;

    SF_LOG_INFO("server.worldserver", "Cluster node %u map ownership updated to '%s'.",
        _nodeId, _ownsAllMaps ? "*" : _availableMapsText.c_str());
    sMapMgr->KickPlayersOnUnownedMaps();
}

void ClusterMgr::Update(uint32 diff)
{
    if (!_enabled)
        return;

    _heartbeatTimer -= int32(diff);
    if (_heartbeatTimer <= 0)
    {
        _heartbeatTimer = int32(_heartbeatInterval);
        Heartbeat();
    }

    _reconcileTimer -= int32(diff);
    if (_reconcileTimer <= 0)
    {
        _reconcileTimer = 500;
        ReconcileMaps();
    }
}

void ClusterMgr::MarkDown()
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_CLUSTER_NODE_STATE);
    stmt->setUInt8(0, 4);
    stmt->setUInt32(1, _nodeId);
    stmt->setUInt32(2, realmID);
    LoginDatabase.DirectExecute(stmt);
}

void ClusterMgr::Shutdown()
{
    if (!_enabled)
        return;

    MarkDown();
    _enabled = false;
}

bool ClusterMgr::LookupMapOwner(uint32 mapId, uint32& nodeId, std::string& host, uint16& port) const
{
    PreparedStatement* assigned = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_MAP_OWNER_ENDPOINT);
    assigned->setUInt32(0, realmID);
    assigned->setUInt32(1, mapId);
    assigned->setUInt32(2, _staleSeconds);
    if (PreparedQueryResult result = LoginDatabase.Query(assigned))
    {
        Field* fields = result->Fetch();
        nodeId = fields[0].GetUInt32();
        host = fields[1].GetString();
        port = fields[2].GetUInt16();
        return true;
    }

    PreparedStatement* wildcard = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_WILDCARD_ENDPOINT);
    wildcard->setUInt32(0, realmID);
    wildcard->setUInt32(1, _staleSeconds);
    if (PreparedQueryResult result = LoginDatabase.Query(wildcard))
    {
        Field* fields = result->Fetch();
        nodeId = fields[0].GetUInt32();
        host = fields[1].GetString();
        port = fields[2].GetUInt16();
        return true;
    }

    return false;
}

bool ClusterMgr::LookupAccountRoute(uint32 accountId, uint32 characterGuid, uint32& mapId, uint32& nodeId) const
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_ACCOUNT_ROUTE);
    stmt->setUInt32(0, accountId);
    stmt->setUInt32(1, characterGuid);
    stmt->setUInt32(2, realmID);
    if (PreparedQueryResult result = LoginDatabase.Query(stmt))
    {
        Field* fields = result->Fetch();
        mapId = fields[0].GetUInt32();
        nodeId = fields[1].GetUInt32();
        return true;
    }
    return false;
}

void ClusterMgr::UpdateAccountRoute(uint32 accountId, uint32 characterGuid, uint32 mapId)
{
    if (!_enabled)
        return;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_CLUSTER_ACCOUNT_ROUTE);
    stmt->setUInt32(0, accountId);
    stmt->setUInt32(1, characterGuid);
    stmt->setUInt32(2, realmID);
    stmt->setUInt32(3, mapId);
    stmt->setUInt32(4, _nodeId);
    LoginDatabase.Execute(stmt);
}
