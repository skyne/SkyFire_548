/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "Cluster/ClusterBusProtocol.h"

namespace Skyfire
{
namespace Cluster
{
namespace
{
    bool CanRead(ByteBuffer const& in, size_t bytes)
    {
        return in.size() >= in.rpos() && in.size() - in.rpos() >= bytes;
    }

    bool ReadCString(ByteBuffer& in, std::string& value)
    {
        if (in.rpos() > in.size())
            return false;

        size_t pos = in.rpos();
        size_t remain = in.size() - pos;
        if (remain == 0)
            return false;

        uint8 const* data = in.contents() + pos;
        for (size_t i = 0; i < remain; ++i)
        {
            if (data[i] != 0)
                continue;
            value.assign(reinterpret_cast<char const*>(data), i);
            in.rpos(pos + i + 1);
            return true;
        }
        return false;
    }

    bool ReadPacket(ByteBuffer& in, uint16& opcode, std::vector<uint8>& packet)
    {
        if (!CanRead(in, sizeof(uint16) + sizeof(uint32)))
            return false;
        in >> opcode;
        uint32 size = 0;
        in >> size;
        if (size > BUS_MAX_PACKET || !CanRead(in, size))
            return false;
        packet.resize(size);
        if (size)
        {
            memcpy(packet.data(), in.contents() + in.rpos(), size);
            in.rpos(in.rpos() + size);
        }
        return true;
    }

    void WritePacket(ByteBuffer& out, uint16 opcode, std::vector<uint8> const& packet)
    {
        out << uint16(opcode);
        out << uint32(packet.size());
        if (!packet.empty())
            out.append(packet.data(), packet.size());
    }
}

    void EncodeBus(ByteBuffer& out, BusMessage type, uint32 originNode, ByteBuffer const& body)
    {
        out << uint16(type);
        out << uint32(originNode);
        out << uint32(body.size());
        if (!body.empty())
            out.append(body.contents(), body.size());
    }

    bool DecodeBus(ByteBuffer& in, BusEnvelope& envelope)
    {
        if (!CanRead(in, sizeof(uint16) + sizeof(uint32) + sizeof(uint32)))
            return false;

        uint16 type = 0;
        uint32 origin = 0;
        uint32 size = 0;
        in >> type;
        in >> origin;
        in >> size;
        if (size > BUS_MAX_PACKET || !CanRead(in, size))
            return false;

        envelope.Type = BusMessage(type);
        envelope.OriginNode = origin;
        envelope.Body.clear();
        if (size)
        {
            envelope.Body.append(in.contents() + in.rpos(), size);
            in.rpos(in.rpos() + size);
        }
        return true;
    }

    void WritePresence(ByteBuffer& out, PresenceBody const& body)
    {
        out << uint32(body.Guid);
        out << uint32(body.AccountId);
        out << uint32(body.GuildId);
        out << uint32(body.ZoneId);
        out << uint32(body.Team);
        out << uint32(body.VirtualRealm);
        out << uint8(body.Level);
        out << uint8(body.ClassId);
        out << uint8(body.Race);
        out << uint8(body.Gender);
        out << uint8(body.Flags);
        out << uint8(body.Security);
        out << body.Name;
        out << body.GuildName;
        out << body.AutoReply;
    }

    bool ReadPresence(ByteBuffer& in, PresenceBody& body)
    {
        if (!CanRead(in, sizeof(uint32) * 6 + sizeof(uint8) * 6))
            return false;
        in >> body.Guid;
        in >> body.AccountId;
        in >> body.GuildId;
        in >> body.ZoneId;
        in >> body.Team;
        in >> body.VirtualRealm;
        in >> body.Level;
        in >> body.ClassId;
        in >> body.Race;
        in >> body.Gender;
        in >> body.Flags;
        in >> body.Security;
        return ReadCString(in, body.Name) && ReadCString(in, body.GuildName) && ReadCString(in, body.AutoReply);
    }

    void WritePlayerPacket(ByteBuffer& out, PlayerPacketBody const& body)
    {
        out << uint16(body.Targets.size());
        for (uint32 guid : body.Targets)
            out << uint32(guid);
        WritePacket(out, body.Opcode, body.Packet);
    }

    bool ReadPlayerPacket(ByteBuffer& in, PlayerPacketBody& body)
    {
        if (!CanRead(in, sizeof(uint16)))
            return false;
        uint16 count = 0;
        in >> count;
        if (count > BUS_MAX_TARGETS || !CanRead(in, size_t(count) * sizeof(uint32)))
            return false;
        body.Targets.resize(count);
        for (uint16 i = 0; i < count; ++i)
            in >> body.Targets[i];
        return ReadPacket(in, body.Opcode, body.Packet);
    }

    void WriteGlobalPacket(ByteBuffer& out, GlobalPacketBody const& body)
    {
        out << uint32(body.Team);
        out << uint8(body.GmOnly);
        WritePacket(out, body.Opcode, body.Packet);
    }

    bool ReadGlobalPacket(ByteBuffer& in, GlobalPacketBody& body)
    {
        if (!CanRead(in, sizeof(uint32) + sizeof(uint8)))
            return false;
        in >> body.Team;
        in >> body.GmOnly;
        return ReadPacket(in, body.Opcode, body.Packet);
    }

    void WriteChannelPacket(ByteBuffer& out, ChannelPacketBody const& body)
    {
        out << uint32(body.Team);
        out << uint64(body.SpeakerGuid);
        out << body.Name;
        WritePacket(out, body.Opcode, body.Packet);
    }

    bool ReadChannelPacket(ByteBuffer& in, ChannelPacketBody& body)
    {
        if (!CanRead(in, sizeof(uint32) + sizeof(uint64)))
            return false;
        in >> body.Team;
        in >> body.SpeakerGuid;
        return ReadCString(in, body.Name) && ReadPacket(in, body.Opcode, body.Packet);
    }

    void WriteGuildPacket(ByteBuffer& out, GuildPacketBody const& body)
    {
        out << uint32(body.GuildId);
        out << uint32(body.SenderGuid);
        out << uint8(body.OfficerOnly);
        out << uint8(body.Addon);
        out << body.Prefix;
        WritePacket(out, body.Opcode, body.Packet);
    }

    bool ReadGuildPacket(ByteBuffer& in, GuildPacketBody& body)
    {
        if (!CanRead(in, sizeof(uint32) * 2 + sizeof(uint8) * 2))
            return false;
        in >> body.GuildId;
        in >> body.SenderGuid;
        in >> body.OfficerOnly;
        in >> body.Addon;
        return ReadCString(in, body.Prefix) && ReadPacket(in, body.Opcode, body.Packet);
    }
}
}
