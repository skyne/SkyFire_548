/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_BUS_PROTOCOL_H
#define SF_CLUSTER_BUS_PROTOCOL_H

#include "ByteBuffer.h"
#include "Define.h"
#include <string>
#include <vector>

namespace Skyfire
{
namespace Cluster
{
    constexpr uint32 BUS_MAX_PACKET = 1024 * 1024;
    constexpr uint16 BUS_MAX_TARGETS = 4096;

    constexpr uint8 PRESENCE_AFK = 0x01;
    constexpr uint8 PRESENCE_DND = 0x02;
    constexpr uint8 PRESENCE_GM = 0x04;
    constexpr uint8 PRESENCE_ACCEPT_WHISPERS = 0x08;

    enum class BusMessage : uint16
    {
        Presence = 1,
        PresenceRemove = 2,
        PacketPlayers = 3,
        PacketGlobal = 4,
        PacketChannel = 5,
        PacketGuild = 6,
        MailNotify = 7
    };

    struct BusEnvelope
    {
        BusMessage Type = BusMessage::Presence;
        uint32 OriginNode = 0;
        ByteBuffer Body;
    };

    struct PresenceBody
    {
        uint32 Guid = 0;
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

    struct PlayerPacketBody
    {
        std::vector<uint32> Targets;
        uint16 Opcode = 0;
        std::vector<uint8> Packet;
    };

    struct GlobalPacketBody
    {
        uint32 Team = 0;
        uint8 GmOnly = 0;
        uint16 Opcode = 0;
        std::vector<uint8> Packet;
    };

    struct ChannelPacketBody
    {
        uint32 Team = 0;
        uint64 SpeakerGuid = 0;
        std::string Name;
        uint16 Opcode = 0;
        std::vector<uint8> Packet;
    };

    struct GuildPacketBody
    {
        uint32 GuildId = 0;
        uint32 SenderGuid = 0;
        uint8 OfficerOnly = 0;
        uint8 Addon = 0;
        std::string Prefix;
        uint16 Opcode = 0;
        std::vector<uint8> Packet;
    };

    void EncodeBus(ByteBuffer& out, BusMessage type, uint32 originNode, ByteBuffer const& body);
    bool DecodeBus(ByteBuffer& in, BusEnvelope& envelope);

    void WritePresence(ByteBuffer& out, PresenceBody const& body);
    bool ReadPresence(ByteBuffer& in, PresenceBody& body);

    void WritePlayerPacket(ByteBuffer& out, PlayerPacketBody const& body);
    bool ReadPlayerPacket(ByteBuffer& in, PlayerPacketBody& body);

    void WriteGlobalPacket(ByteBuffer& out, GlobalPacketBody const& body);
    bool ReadGlobalPacket(ByteBuffer& in, GlobalPacketBody& body);

    void WriteChannelPacket(ByteBuffer& out, ChannelPacketBody const& body);
    bool ReadChannelPacket(ByteBuffer& in, ChannelPacketBody& body);

    void WriteGuildPacket(ByteBuffer& out, GuildPacketBody const& body);
    bool ReadGuildPacket(ByteBuffer& in, GuildPacketBody& body);
}
}

#endif
