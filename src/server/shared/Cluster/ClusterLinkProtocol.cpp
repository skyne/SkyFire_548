/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "Cluster/ClusterLinkProtocol.h"
#include <cstring>

namespace Skyfire
{
namespace Cluster
{
    void EncodeFrame(ByteBuffer& out, LinkMessage type, ByteBuffer const& payload)
    {
        out << uint32(LINK_MAGIC);
        out << uint16(LINK_VERSION);
        out << uint16(type);
        out << uint32(payload.size());
        if (!payload.empty())
            out.append(payload.contents(), payload.size());
    }

    bool TryDecodeFrame(std::vector<uint8>& buffer, LinkFrame& frame)
    {
        constexpr size_t headerSize = 4 + 2 + 2 + 4;
        if (buffer.size() < headerSize)
            return false;

        uint32 magic = 0;
        uint16 version = 0;
        uint16 type = 0;
        uint32 size = 0;
        memcpy(&magic, &buffer[0], 4);
        memcpy(&version, &buffer[4], 2);
        memcpy(&type, &buffer[6], 2);
        memcpy(&size, &buffer[8], 4);

        if (magic != LINK_MAGIC || version != LINK_VERSION || size > LINK_MAX_PAYLOAD)
        {
            buffer.clear();
            return false;
        }

        if (buffer.size() < headerSize + size)
            return false;

        frame.Type = LinkMessage(type);
        frame.Payload.clear();
        if (size)
            frame.Payload.append(&buffer[headerSize], size);

        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(headerSize + size));
        return true;
    }

    void WriteHandoffPayload(ByteBuffer& out, HandoffPayload const& data)
    {
        out << data.AccountId << data.CharacterGuid << data.DestMap;
        out << data.X << data.Y << data.Z << data.O;
        out << data.Options << data.Flags << data.TaxiPathId << data.TaxiNode;
    }

    bool ReadHandoffPayload(ByteBuffer& in, HandoffPayload& data)
    {
        if (in.size() - in.rpos() < 4 * 6 + 4 * 4 + 1)
            return false;
        in >> data.AccountId >> data.CharacterGuid >> data.DestMap;
        in >> data.X >> data.Y >> data.Z >> data.O;
        in >> data.Options >> data.Flags >> data.TaxiPathId >> data.TaxiNode;
        return true;
    }
}
}
