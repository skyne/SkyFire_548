#include "Cluster/ClusterLinkProtocol.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

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

    ByteBuffer payload;
    payload << uint32(42);
    ByteBuffer encoded;
    Skyfire::Cluster::EncodeFrame(encoded, Skyfire::Cluster::LinkMessage::Handoff, payload);

    std::vector<uint8> buffer(encoded.size());
    memcpy(buffer.data(), encoded.contents(), encoded.size());

    Skyfire::Cluster::LinkFrame frame;
    passed &= Expect(Skyfire::Cluster::TryDecodeFrame(buffer, frame), "Handoff frame should decode.");
    passed &= Expect(frame.Type == Skyfire::Cluster::LinkMessage::Handoff, "Decoded type should be Handoff.");
    passed &= Expect(buffer.empty(), "Decoder should consume the whole frame.");

    Skyfire::Cluster::HandoffPayload in;
    in.AccountId = 7;
    in.CharacterGuid = 99;
    in.DestMap = 870;
    in.X = 1.5f;
    in.Y = 2.5f;
    in.Z = 3.5f;
    in.O = 4.5f;
    in.Options = 0x20;
    in.Flags = Skyfire::Cluster::HANDOFF_FLAG_LOGIN | Skyfire::Cluster::HANDOFF_FLAG_TAXI;
    in.TaxiPathId = 12;
    in.TaxiNode = 3;

    ByteBuffer body;
    Skyfire::Cluster::WriteHandoffPayload(body, in);
    Skyfire::Cluster::HandoffPayload out;
    passed &= Expect(Skyfire::Cluster::ReadHandoffPayload(body, out), "Handoff payload should parse.");
    passed &= Expect(out.AccountId == 7 && out.CharacterGuid == 99 && out.DestMap == 870,
        "Handoff identity fields should round-trip.");
    passed &= Expect(out.Flags == in.Flags && out.TaxiPathId == 12,
        "Handoff flags and taxi fields should round-trip.");

    std::vector<uint8> junk(8, 0);
    Skyfire::Cluster::LinkFrame junkFrame;
    passed &= Expect(!Skyfire::Cluster::TryDecodeFrame(junk, junkFrame), "Invalid magic should not decode.");

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
