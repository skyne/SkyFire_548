/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_MGR_H
#define SF_CLUSTER_MGR_H

#include "Platform/Singleton.h"
#include <mutex>
#include <string>
#include <unordered_set>

class ClusterMgr
{
    friend class Skyfire::Singleton<ClusterMgr, Skyfire::Mutex>;

public:
    void LoadConfig();
    bool Initialize();
    void Update(uint32 diff);
    void Shutdown();

    bool IsEnabled() const { return _enabled; }
    bool HideWorldPort() const { return _hideWorldPort; }
    uint32 GetNodeId() const { return _nodeId; }
    uint32 GetLinkPort() const { return _linkPort; }
    std::string const& GetLinkBindIP() const { return _linkBindIP; }
    std::string const& GetSharedToken() const { return _sharedToken; }
    std::string const& GetAdvertisedHost() const { return _advertisedHost; }

    bool OwnsMap(uint32 mapId) const;
    bool OwnsAllMaps() const { return _ownsAllMaps; }
    uint32 GetMigrationTimeoutMs() const { return _migrationTimeoutMs; }

    bool LookupMapOwner(uint32 mapId, uint32& nodeId, std::string& host, uint16& port) const;
    bool LookupAccountRoute(uint32 accountId, uint32 characterGuid, uint32& mapId, uint32& nodeId) const;
    void UpdateAccountRoute(uint32 accountId, uint32 characterGuid, uint32 mapId);

private:
    ClusterMgr();
    ~ClusterMgr() = default;

    void ParseAvailableMaps(std::string const& value);
    bool RegisterNode();
    bool ClaimMaps();
    void Heartbeat();
    void MarkDown();
    void ReconcileMaps();

    bool _enabled;
    bool _hideWorldPort;
    bool _ownsAllMaps;
    uint32 _nodeId;
    uint32 _linkPort;
    uint32 _heartbeatInterval;
    uint32 _staleSeconds;
    uint32 _migrationTimeoutMs;
    int32 _heartbeatTimer;
    int32 _reconcileTimer;
    std::string _linkBindIP;
    std::string _advertisedHost;
    std::string _sharedToken;
    std::string _availableMapsText;
    std::unordered_set<uint32> _availableMaps;
    mutable std::mutex _lock;
};

#define sClusterMgr Skyfire::Singleton<ClusterMgr, Skyfire::Mutex>::instance()

#endif
