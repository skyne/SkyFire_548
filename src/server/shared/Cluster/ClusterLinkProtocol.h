/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_LINK_PROTOCOL_H
#define SF_CLUSTER_LINK_PROTOCOL_H

#include "ByteBuffer.h"
#include "Define.h"
#include <vector>

namespace Skyfire
{
namespace Cluster
{
    /// Private world <-> gateway framing. Not a client opcode and not copied from 3.3.5.
    constexpr uint32 LINK_MAGIC = 0x4C434653; // 'SFCL'
    constexpr uint16 LINK_VERSION = 1;
    constexpr uint32 LINK_MAX_PAYLOAD = 1024 * 1024;

    enum class LinkMessage : uint16
    {
        Hello = 1,
        HelloAck = 2,
        Heartbeat = 3,
        Attach = 4,
        AttachAck = 5,
        Resume = 6,
        ResumeAck = 7,
        Handoff = 8,
        HandoffAck = 9,
        WowOpcode = 10,
        Close = 11
    };

    enum class LinkRole : uint8
    {
        World = 1,
        Gateway = 2
    };

    enum class LinkStatus : uint8
    {
        Ok = 0,
        BadToken = 1,
        BadVersion = 2,
        Rejected = 3
    };

    struct LinkFrame
    {
        LinkMessage Type = LinkMessage::Hello;
        ByteBuffer Payload;
    };

    constexpr uint8 HANDOFF_FLAG_TAXI = 0x01;
    constexpr uint8 HANDOFF_FLAG_FORCE = 0x02;
    constexpr uint8 HANDOFF_FLAG_LOGIN = 0x04;

    struct HandoffPayload
    {
        uint32 AccountId = 0;
        uint32 CharacterGuid = 0;
        uint32 DestMap = 0;
        float X = 0.f;
        float Y = 0.f;
        float Z = 0.f;
        float O = 0.f;
        uint32 Options = 0;
        uint8 Flags = 0;
        uint32 TaxiPathId = 0;
        uint32 TaxiNode = 0;
    };

    void EncodeFrame(ByteBuffer& out, LinkMessage type, ByteBuffer const& payload);
    bool TryDecodeFrame(std::vector<uint8>& buffer, LinkFrame& frame);
    void WriteHandoffPayload(ByteBuffer& out, HandoffPayload const& data);
    bool ReadHandoffPayload(ByteBuffer& in, HandoffPayload& data);
}
}

#endif
