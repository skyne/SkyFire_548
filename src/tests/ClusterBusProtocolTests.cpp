#include "Cluster/ClusterBusProtocol.h"

#include <iostream>

namespace
{
bool Expect(bool condition, char const* message)
{
    if (condition)
        return true;
    std::cerr << message << '\n';
    return false;
}
}

int main()
{
    bool passed = true;

    Skyfire::Cluster::PresenceBody presence;
    presence.Guid = 42;
    presence.AccountId = 7;
    presence.GuildId = 3;
    presence.ZoneId = 1519;
    presence.Team = 67;
    presence.VirtualRealm = 1;
    presence.Level = 90;
    presence.ClassId = 8;
    presence.Race = 1;
    presence.Gender = 1;
    presence.Flags = Skyfire::Cluster::PRESENCE_AFK | Skyfire::Cluster::PRESENCE_ACCEPT_WHISPERS;
    presence.Security = 0;
    presence.Name = "Theron";
    presence.GuildName = "Skyfire";
    presence.AutoReply = "away";

    ByteBuffer presenceBody;
    Skyfire::Cluster::WritePresence(presenceBody, presence);
    ByteBuffer encoded;
    Skyfire::Cluster::EncodeBus(encoded, Skyfire::Cluster::BusMessage::Presence, 4, presenceBody);

    ByteBuffer decoded;
    decoded.append(encoded.contents(), encoded.size());
    Skyfire::Cluster::BusEnvelope envelope;
    passed &= Expect(Skyfire::Cluster::DecodeBus(decoded, envelope), "Presence frame should decode.");
    passed &= Expect(envelope.Type == Skyfire::Cluster::BusMessage::Presence, "Presence type should round-trip.");
    passed &= Expect(envelope.OriginNode == 4, "Origin node should round-trip.");

    Skyfire::Cluster::PresenceBody out;
    passed &= Expect(Skyfire::Cluster::ReadPresence(envelope.Body, out), "Presence body should decode.");
    passed &= Expect(out.Guid == 42 && out.Name == "Theron" && out.AutoReply == "away", "Presence fields should round-trip.");
    passed &= Expect(out.Flags == presence.Flags && out.ZoneId == 1519, "Presence flags and zone should round-trip.");

    Skyfire::Cluster::PlayerPacketBody packet;
    packet.Targets.push_back(42);
    packet.Targets.push_back(99);
    packet.Opcode = 0x0532;
    packet.Packet.push_back(1);
    packet.Packet.push_back(2);
    packet.Packet.push_back(3);
    ByteBuffer packetBody;
    Skyfire::Cluster::WritePlayerPacket(packetBody, packet);
    ByteBuffer packetFrame;
    Skyfire::Cluster::EncodeBus(packetFrame, Skyfire::Cluster::BusMessage::PacketPlayers, 2, packetBody);
    ByteBuffer packetDecoded;
    packetDecoded.append(packetFrame.contents(), packetFrame.size());
    Skyfire::Cluster::BusEnvelope packetEnvelope;
    passed &= Expect(Skyfire::Cluster::DecodeBus(packetDecoded, packetEnvelope), "Player packet frame should decode.");
    Skyfire::Cluster::PlayerPacketBody packetOut;
    passed &= Expect(Skyfire::Cluster::ReadPlayerPacket(packetEnvelope.Body, packetOut), "Player packet body should decode.");
    passed &= Expect(packetOut.Targets.size() == 2 && packetOut.Targets[1] == 99, "Player targets should round-trip.");
    passed &= Expect(packetOut.Opcode == 0x0532 && packetOut.Packet.size() == 3 && packetOut.Packet[2] == 3, "Player bytes should round-trip.");

    ByteBuffer truncated;
    truncated << uint16(1);
    Skyfire::Cluster::BusEnvelope ignored;
    passed &= Expect(!Skyfire::Cluster::DecodeBus(truncated, ignored), "Short frame should fail.");

    if (!passed)
        return 1;
    std::cout << "cluster bus protocol tests passed\n";
    return 0;
}
