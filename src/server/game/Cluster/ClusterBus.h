/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_BUS_H
#define SF_CLUSTER_BUS_H

#include "ByteBuffer.h"
#include "Cluster/ClusterBusProtocol.h"
#include "Platform/Singleton.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class NatsClient;
class Player;
class WorldPacket;

/// Cross-node fanout for state that already lives in the shared databases.
/// Wired: global and GM text, whispers, chat channels, guild chat and
/// broadcasts, party and raid packets, mail arrival, friend status, /who.
/// Still local until a single owner exists: battleground and arena queues,
/// auction house and black market bids, calendar mutations, guild bank
/// withdrawals. Say, yell, and trade stay on the map owner.
struct ClusterRemotePlayer
{
    uint32 Guid = 0;
    uint32 NodeId = 0;
    uint32 AccountId = 0;
    uint32 GuildId = 0;
    uint32 ZoneId = 0;
    uint32 Team = 0;
    uint32 VirtualRealm = 0;
    uint8 Level = 0;
    uint8 ClassId = 0;
    uint8 Race = 0;
    uint8 Gender = 0;
    uint8 Flags = 0;
    uint8 Security = 0;
    std::string Name;
    std::string GuildName;
    std::string AutoReply;
};

class ClusterBus
{
    friend class Skyfire::Singleton<ClusterBus, Skyfire::Mutex>;

public:
    bool Start();
    void Stop();
    void Update();

    bool IsEnabled() const { return _enabled; }
    bool IsApplying() const { return _applying; }

    void PublishPresence(Player* player);
    void PublishPresenceRemove(uint32 lowGuid);

    void RelayToPlayers(std::vector<uint32> const& lowGuids, WorldPacket const* packet) const;
    void RelayGlobal(WorldPacket const* packet, uint32 team, bool gmOnly) const;
    void RelaySystemText(std::string const& text, bool gmOnly) const;
    void RelayChannel(uint32 team, std::string const& name, uint64 speakerGuid, WorldPacket const* packet) const;
    void RelayGuildChat(uint32 guildId, uint32 senderLow, bool officerOnly, bool addon, std::string const& prefix, WorldPacket const* packet) const;
    void NotifyMail(uint32 receiverLow, uint32 mailId);

    ClusterRemotePlayer const* FindRemote(uint32 lowGuid) const;
    ClusterRemotePlayer const* FindRemoteByName(std::string const& name) const;
    void VisitRemotes(std::function<void(ClusterRemotePlayer const&)> const& visitor) const;

private:
    ClusterBus();
    ~ClusterBus();

    void Publish(uint16 type, ByteBuffer const& body) const;
    void HandleIncoming(std::vector<uint8> payload);
    void Dispatch(ByteBuffer& payload);
    void NoteMail(uint32 receiverLow, uint32 mailId);
    void NotifyFriends(ClusterRemotePlayer const& remote, bool online);
    static std::string NameKey(std::string const& name);

    bool _enabled;
    bool _applying;
    uint32 _nodeId;
    std::unique_ptr<NatsClient> _client;
    std::mutex _inboundLock;
    std::vector<std::vector<uint8>> _inbound;
    std::map<uint32, ClusterRemotePlayer> _byGuid;
    std::map<std::string, uint32> _byName;
    std::map<uint32, std::pair<uint32, uint8>> _mailPull;
};

#define sClusterBus Skyfire::Singleton<ClusterBus, Skyfire::Mutex>::instance()

#endif
