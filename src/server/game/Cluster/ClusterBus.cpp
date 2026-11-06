/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "ClusterBus.h"
#include "NatsClient.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "Cluster/ClusterBusProtocol.h"
#include "ClusterMgr.h"
#include "Config.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "Player.h"
#include "SocialMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr uint8 MAIL_PULL_ATTEMPTS = 20;

    bool ParseNatsUrl(std::string const& url, std::string& host, uint16& port)
    {
        std::string rest = url;
        if (rest.compare(0, 7, "nats://") == 0)
            rest.erase(0, 7);
        if (rest.empty())
            return false;

        size_t colon = rest.rfind(':');
        if (colon == std::string::npos)
        {
            host = rest;
            port = 4222;
            return true;
        }

        host = rest.substr(0, colon);
        char* end = NULL;
        unsigned long parsed = std::strtoul(rest.c_str() + colon + 1, &end, 10);
        if (host.empty() || !end || *end != '\0' || parsed == 0 || parsed > 0xFFFF)
            return false;
        port = uint16(parsed);
        return true;
    }

    void CopyPacket(WorldPacket const* packet, uint16& opcode, std::vector<uint8>& bytes)
    {
        opcode = 0;
        bytes.clear();
        if (!packet)
            return;
        opcode = uint16(packet->GetOpcode());
        if (!packet->empty())
        {
            bytes.resize(packet->size());
            memcpy(bytes.data(), packet->contents(), packet->size());
        }
    }

    WorldPacket MakePacket(uint16 opcode, std::vector<uint8> const& bytes)
    {
        WorldPacket packet(Opcodes(opcode), bytes.size());
        if (!bytes.empty())
            packet.append(bytes.data(), bytes.size());
        return packet;
    }
}

ClusterBus::ClusterBus() :
    _enabled(false),
    _applying(false),
    _nodeId(0),
    _client(),
    _inboundLock(),
    _inbound(),
    _byGuid(),
    _byName(),
    _mailPull()
{
}

ClusterBus::~ClusterBus()
{
    Stop();
}

bool ClusterBus::Start()
{
    if (_enabled)
        return true;
    if (!sClusterMgr->IsEnabled())
        return true;

    std::string url = sConfigMgr->GetStringDefault("Cluster.NatsUrl", "");
    if (url.empty())
    {
        SF_LOG_INFO("server.worldserver", "Cluster.NatsUrl is empty. Cross-node chat and mail stay on this world.");
        return true;
    }

    std::string host;
    uint16 port = 0;
    if (!ParseNatsUrl(url, host, port))
    {
        SF_LOG_ERROR("server.worldserver", "Cluster.NatsUrl '%s' is not a NATS url.", url.c_str());
        return false;
    }

    _nodeId = sClusterMgr->GetNodeId();
    _client.reset(new NatsClient());
    _client->SetMessageHandler([this](std::vector<uint8> payload) { HandleIncoming(std::move(payload)); });
    if (!_client->Start(host, port))
    {
        _client.reset();
        SF_LOG_ERROR("server.worldserver", "Failed to start the NATS client.");
        return false;
    }

    _enabled = true;
    SF_LOG_INFO("server.worldserver", "Cluster bus node %u publishing to %s.", _nodeId, url.c_str());
    return true;
}

void ClusterBus::Stop()
{
    _enabled = false;
    if (_client)
    {
        _client->Stop();
        _client.reset();
    }
    std::lock_guard<std::mutex> guard(_inboundLock);
    _inbound.clear();
}

void ClusterBus::HandleIncoming(std::vector<uint8> payload)
{
    std::lock_guard<std::mutex> guard(_inboundLock);
    if (_inbound.size() >= 4096)
        _inbound.erase(_inbound.begin());
    _inbound.push_back(std::move(payload));
}

void ClusterBus::Update()
{
    if (!_enabled)
        return;

    std::vector<std::vector<uint8>> batch;
    {
        std::lock_guard<std::mutex> guard(_inboundLock);
        batch.swap(_inbound);
    }

    for (std::vector<uint8>& payload : batch)
    {
        ByteBuffer buffer;
        if (!payload.empty())
            buffer.append(payload.data(), payload.size());
        Dispatch(buffer);
    }

    for (auto it = _mailPull.begin(); it != _mailPull.end();)
    {
        Player* player = ObjectAccessor::FindPlayer(MAKE_NEW_GUID(it->first, 0, HIGHGUID_PLAYER));
        if (!player)
        {
            it = _mailPull.erase(it);
            continue;
        }

        bool arrived = player->PullNewMail(it->second.first);
        if (arrived || it->second.second == 0 || --it->second.second == 0)
            it = _mailPull.erase(it);
        else
            ++it;
    }
}

void ClusterBus::Publish(uint16 type, ByteBuffer const& body) const
{
    if (!_enabled || _applying || !_client)
        return;

    ByteBuffer frame;
    Skyfire::Cluster::EncodeBus(frame, Skyfire::Cluster::BusMessage(type), _nodeId, body);
    _client->Publish(frame.contents(), frame.size());
}

std::string ClusterBus::NameKey(std::string const& name)
{
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    return key;
}

void ClusterBus::PublishPresence(Player* player)
{
    if (!_enabled || !player || !player->GetSession())
        return;

    Skyfire::Cluster::PresenceBody body;
    body.Guid = player->GetGUIDLow();
    body.AccountId = player->GetSession()->GetAccountId();
    body.GuildId = player->GetGuildId();
    body.ZoneId = player->GetZoneId();
    body.Team = player->GetTeam();
    body.VirtualRealm = player->GetSession()->GetVirtualRealmID();
    body.Level = player->getLevel();
    body.ClassId = player->getClass();
    body.Race = player->getRace();
    body.Gender = player->getGender();
    body.Security = uint8(player->GetSession()->GetSecurity());
    body.Name = player->GetName();
    body.GuildName = sGuildMgr->GetGuildNameById(player->GetGuildId());
    body.AutoReply = player->autoReplyMsg;
    if (player->isAFK())
        body.Flags |= Skyfire::Cluster::PRESENCE_AFK;
    if (player->isDND())
        body.Flags |= Skyfire::Cluster::PRESENCE_DND;
    if (player->IsGameMaster())
        body.Flags |= Skyfire::Cluster::PRESENCE_GM;
    if (player->isAcceptWhispers())
        body.Flags |= Skyfire::Cluster::PRESENCE_ACCEPT_WHISPERS;

    ByteBuffer encoded;
    Skyfire::Cluster::WritePresence(encoded, body);
    Publish(uint16(Skyfire::Cluster::BusMessage::Presence), encoded);
}

void ClusterBus::PublishPresenceRemove(uint32 lowGuid)
{
    if (!_enabled || !lowGuid)
        return;

    ByteBuffer body;
    body << uint32(lowGuid);
    Publish(uint16(Skyfire::Cluster::BusMessage::PresenceRemove), body);
}

void ClusterBus::RelayToPlayers(std::vector<uint32> const& lowGuids, WorldPacket const* packet) const
{
    if (!_enabled || _applying || lowGuids.empty() || !packet)
        return;
    if (lowGuids.size() > Skyfire::Cluster::BUS_MAX_TARGETS)
        return;

    Skyfire::Cluster::PlayerPacketBody body;
    body.Targets = lowGuids;
    CopyPacket(packet, body.Opcode, body.Packet);
    ByteBuffer encoded;
    Skyfire::Cluster::WritePlayerPacket(encoded, body);
    Publish(uint16(Skyfire::Cluster::BusMessage::PacketPlayers), encoded);
}

void ClusterBus::RelayGlobal(WorldPacket const* packet, uint32 team, bool gmOnly) const
{
    if (!_enabled || _applying || !packet)
        return;

    Skyfire::Cluster::GlobalPacketBody body;
    body.Team = team;
    body.GmOnly = gmOnly ? 1 : 0;
    CopyPacket(packet, body.Opcode, body.Packet);
    ByteBuffer encoded;
    Skyfire::Cluster::WriteGlobalPacket(encoded, body);
    Publish(uint16(Skyfire::Cluster::BusMessage::PacketGlobal), encoded);
}

void ClusterBus::RelaySystemText(std::string const& text, bool gmOnly) const
{
    if (!_enabled || _applying || text.empty())
        return;

    char* copy = strdup(text.c_str());
    char* pos = copy;
    while (char* line = ChatHandler::LineFromMessage(pos))
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, ChatMsg::CHAT_MSG_SYSTEM, Language::LANG_UNIVERSAL, NULL, NULL, line);
        RelayGlobal(&data, 0, gmOnly);
    }
    free(copy);
}

void ClusterBus::RelayChannel(uint32 team, std::string const& name, uint64 speakerGuid, WorldPacket const* packet) const
{
    if (!_enabled || _applying || name.empty() || !packet)
        return;

    Skyfire::Cluster::ChannelPacketBody body;
    body.Team = team;
    body.SpeakerGuid = speakerGuid;
    body.Name = name;
    CopyPacket(packet, body.Opcode, body.Packet);
    ByteBuffer encoded;
    Skyfire::Cluster::WriteChannelPacket(encoded, body);
    Publish(uint16(Skyfire::Cluster::BusMessage::PacketChannel), encoded);
}

void ClusterBus::RelayGuildChat(uint32 guildId, uint32 senderLow, bool officerOnly, bool addon, std::string const& prefix, WorldPacket const* packet) const
{
    if (!_enabled || _applying || !guildId || !packet)
        return;

    Skyfire::Cluster::GuildPacketBody body;
    body.GuildId = guildId;
    body.SenderGuid = senderLow;
    body.OfficerOnly = officerOnly ? 1 : 0;
    body.Addon = addon ? 1 : 0;
    body.Prefix = prefix;
    CopyPacket(packet, body.Opcode, body.Packet);
    ByteBuffer encoded;
    Skyfire::Cluster::WriteGuildPacket(encoded, body);
    Publish(uint16(Skyfire::Cluster::BusMessage::PacketGuild), encoded);
}

void ClusterBus::NotifyMail(uint32 receiverLow, uint32 mailId)
{
    if (!_enabled || _applying || !receiverLow || !mailId)
        return;

    ByteBuffer body;
    body << uint32(receiverLow);
    body << uint32(mailId);
    Publish(uint16(Skyfire::Cluster::BusMessage::MailNotify), body);
}

void ClusterBus::NoteMail(uint32 receiverLow, uint32 mailId)
{
    Player* player = ObjectAccessor::FindPlayer(MAKE_NEW_GUID(receiverLow, 0, HIGHGUID_PLAYER));
    if (!player)
        return;
    _mailPull[receiverLow] = std::make_pair(mailId, MAIL_PULL_ATTEMPTS);
}

ClusterRemotePlayer const* ClusterBus::FindRemote(uint32 lowGuid) const
{
    std::map<uint32, ClusterRemotePlayer>::const_iterator it = _byGuid.find(lowGuid);
    return it == _byGuid.end() ? NULL : &it->second;
}

ClusterRemotePlayer const* ClusterBus::FindRemoteByName(std::string const& name) const
{
    std::map<std::string, uint32>::const_iterator named = _byName.find(NameKey(name));
    if (named == _byName.end())
        return NULL;
    return FindRemote(named->second);
}

void ClusterBus::VisitRemotes(std::function<void(ClusterRemotePlayer const&)> const& visitor) const
{
    for (std::map<uint32, ClusterRemotePlayer>::const_iterator it = _byGuid.begin(); it != _byGuid.end(); ++it)
        visitor(it->second);
}

void ClusterBus::NotifyFriends(ClusterRemotePlayer const& remote, bool online)
{
    FriendsResult result = online ? FRIEND_ONLINE : FRIEND_OFFLINE;
    AccountTypes gmSecLevel = AccountTypes(sWorld->getIntConfig(WorldIntConfigs::CONFIG_GM_LEVEL_IN_WHO_LIST));
    for (SessionMap::const_iterator it = sWorld->GetAllSessions().begin(); it != sWorld->GetAllSessions().end(); ++it)
    {
        WorldSession* session = it->second;
        Player* player = session ? session->GetPlayer() : NULL;
        if (!player || !player->IsInWorld() || !player->GetSocial()->HasFriend(remote.Guid))
            continue;
        if (!session->HasPermission(rbac::RBAC_PERM_WHO_SEE_ALL_SEC_LEVELS) && remote.Security > uint8(gmSecLevel))
            continue;
        if (player->GetTeam() != remote.Team && !session->HasPermission(rbac::RBAC_PERM_TWO_SIDE_WHO_LIST))
            continue;

        FriendInfo info;
        sSocialMgr->GetFriendInfo(player, remote.Guid, info);
        WorldPacket data;
        sSocialMgr->MakeFriendStatusPacket(result, remote.Guid, &data);
        if (online)
        {
            data << uint8(info.Status);
            data << uint32(info.Area);
            data << uint32(info.Level);
            data << uint32(info.Class);
        }
        session->SendPacket(&data);
    }
}

void ClusterBus::Dispatch(ByteBuffer& payload)
{
    Skyfire::Cluster::BusEnvelope envelope;
    if (!Skyfire::Cluster::DecodeBus(payload, envelope))
    {
        SF_LOG_ERROR("network", "Cluster bus dropped a malformed frame.");
        return;
    }
    if (envelope.OriginNode == _nodeId)
        return;

    _applying = true;
    switch (envelope.Type)
    {
        case Skyfire::Cluster::BusMessage::Presence:
        {
            Skyfire::Cluster::PresenceBody body;
            if (!Skyfire::Cluster::ReadPresence(envelope.Body, body) || body.Name.empty())
                break;

            std::map<uint32, ClusterRemotePlayer>::iterator existing = _byGuid.find(body.Guid);
            if (existing != _byGuid.end())
                _byName.erase(NameKey(existing->second.Name));

            ClusterRemotePlayer remote;
            remote.Guid = body.Guid;
            remote.NodeId = envelope.OriginNode;
            remote.AccountId = body.AccountId;
            remote.GuildId = body.GuildId;
            remote.ZoneId = body.ZoneId;
            remote.Team = body.Team;
            remote.VirtualRealm = body.VirtualRealm;
            remote.Level = body.Level;
            remote.ClassId = body.ClassId;
            remote.Race = body.Race;
            remote.Gender = body.Gender;
            remote.Flags = body.Flags;
            remote.Security = body.Security;
            remote.Name = body.Name;
            remote.GuildName = body.GuildName;
            remote.AutoReply = body.AutoReply;
            _byGuid[body.Guid] = remote;
            _byName[NameKey(body.Name)] = body.Guid;
            NotifyFriends(_byGuid[body.Guid], true);
            break;
        }
        case Skyfire::Cluster::BusMessage::PresenceRemove:
        {
            if (envelope.Body.size() < sizeof(uint32))
                break;
            uint32 guid = 0;
            envelope.Body >> guid;
            std::map<uint32, ClusterRemotePlayer>::iterator existing = _byGuid.find(guid);
            if (existing == _byGuid.end() || existing->second.NodeId != envelope.OriginNode)
                break;
            ClusterRemotePlayer remote = existing->second;
            _byName.erase(NameKey(remote.Name));
            _byGuid.erase(existing);
            NotifyFriends(remote, false);
            break;
        }
        case Skyfire::Cluster::BusMessage::PacketPlayers:
        {
            Skyfire::Cluster::PlayerPacketBody body;
            if (!Skyfire::Cluster::ReadPlayerPacket(envelope.Body, body))
                break;
            WorldPacket packet = MakePacket(body.Opcode, body.Packet);
            for (uint32 lowGuid : body.Targets)
            {
                Player* player = ObjectAccessor::FindPlayer(MAKE_NEW_GUID(lowGuid, 0, HIGHGUID_PLAYER));
                if (player && player->IsInWorld() && player->GetSession())
                    player->GetSession()->SendPacket(&packet);
            }
            break;
        }
        case Skyfire::Cluster::BusMessage::PacketGlobal:
        {
            Skyfire::Cluster::GlobalPacketBody body;
            if (!Skyfire::Cluster::ReadGlobalPacket(envelope.Body, body))
                break;
            WorldPacket packet = MakePacket(body.Opcode, body.Packet);
            if (body.GmOnly)
                sWorld->SendGlobalGMMessage(&packet, NULL, body.Team);
            else
                sWorld->SendGlobalMessage(&packet, NULL, body.Team);
            break;
        }
        case Skyfire::Cluster::BusMessage::PacketChannel:
        {
            Skyfire::Cluster::ChannelPacketBody body;
            if (!Skyfire::Cluster::ReadChannelPacket(envelope.Body, body))
                break;
            ChannelMgr* manager = ChannelMgr::forTeam(body.Team);
            if (!manager)
                break;
            Channel* channel = manager->GetChannel(body.Name, NULL, false);
            if (!channel)
                break;
            WorldPacket packet = MakePacket(body.Opcode, body.Packet);
            channel->DeliverClusterPacket(&packet, body.SpeakerGuid);
            break;
        }
        case Skyfire::Cluster::BusMessage::PacketGuild:
        {
            Skyfire::Cluster::GuildPacketBody body;
            if (!Skyfire::Cluster::ReadGuildPacket(envelope.Body, body))
                break;
            Guild* guild = sGuildMgr->GetGuildById(body.GuildId);
            if (!guild)
                break;
            WorldPacket packet = MakePacket(body.Opcode, body.Packet);
            guild->DeliverClusterChat(&packet, body.SenderGuid, body.OfficerOnly != 0, body.Addon != 0, body.Prefix);
            break;
        }
        case Skyfire::Cluster::BusMessage::MailNotify:
        {
            if (envelope.Body.size() < sizeof(uint32) * 2)
                break;
            uint32 receiver = 0;
            uint32 mailId = 0;
            envelope.Body >> receiver;
            envelope.Body >> mailId;
            NoteMail(receiver, mailId);
            break;
        }
        default:
            SF_LOG_ERROR("network", "Cluster bus ignored message %u.", uint32(envelope.Type));
            break;
    }
    _applying = false;
}
