/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_WOW_WIRE_OPCODES_H
#define SF_CLUSTER_WOW_WIRE_OPCODES_H

#include "Define.h"

namespace Skyfire
{
namespace Cluster
{
    /// 5.4.8 build 18414 wire numbers from this tree's Opcodes.cpp. Not 3.3.5.
    constexpr uint16 WIRE_MAX_OPCODE = 0x1FFF;
    constexpr uint16 WIRE_MSG_VERIFY_CONNECTIVITY = 0x4F57;
    constexpr uint16 WIRE_CMSG_AUTH_SESSION = 0x00B2;
    constexpr uint16 WIRE_CMSG_PING = 0x0012;
    constexpr uint16 WIRE_CMSG_KEEP_ALIVE = 0x1A87;
    constexpr uint16 WIRE_CMSG_LOG_DISCONNECT = 0x10B3;
    constexpr uint16 WIRE_CMSG_ENABLE_NAGLE = 0x12B3;
    constexpr uint16 WIRE_SMSG_AUTH_CHALLENGE = 0x0949;
    constexpr uint16 WIRE_SMSG_AUTH_RESPONSE = 0x0ABA;
    constexpr uint16 WIRE_SMSG_PONG = 0x1969;
    constexpr uint16 WIRE_SMSG_LOGIN_VERIFY_WORLD = 0x1C0F;
    constexpr uint16 WIRE_SMSG_TRANSFER_PENDING = 0x061B;
    constexpr uint16 WIRE_SMSG_NEW_WORLD = 0x1C3B;
    constexpr uint16 WIRE_MSG_MOVE_WORLDPORT_ACK = 0x1FAD;
}
}

#endif
