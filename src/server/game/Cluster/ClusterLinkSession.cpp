/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "ClusterLinkSession.h"
#include "AccountMgr.h"
#include "ClusterMgr.h"
#include "Log.h"
#include "Network/BoostAsioUtils.h"
#include "Opcodes.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <cstring>
#include <utility>

ClusterLinkSession::ClusterLinkSession(std::unique_ptr<boost::asio::ip::tcp::socket> socket) :
    _socket(std::move(socket)),
    _session(NULL),
    _closed(false),
    _authed(false)
{
    if (_socket)
        _writeQueue.reset(new Skyfire::Net::BoostAsioWriteQueue<boost::asio::ip::tcp::socket>(*_socket));
}

ClusterLinkSession::~ClusterLinkSession()
{
    Close();
}

void ClusterLinkSession::Start()
{
    AsyncRead();
}

void ClusterLinkSession::Close()
{
    if (_closed.exchange(true))
        return;

    {
        std::lock_guard<std::mutex> guard(_sessionLock);
        if (_session)
        {
            _session->SetClusterLink(NULL);
            _session = NULL;
        }
    }

    if (_socket)
        Skyfire::Net::CloseTcpSocket(*_socket);
}

void ClusterLinkSession::DetachWorldSession()
{
    std::lock_guard<std::mutex> guard(_sessionLock);
    _session = NULL;
}

void ClusterLinkSession::SendHandoff(Skyfire::Cluster::HandoffPayload const& payload)
{
    ByteBuffer body;
    Skyfire::Cluster::WriteHandoffPayload(body, payload);
    QueueFrame(Skyfire::Cluster::LinkMessage::Handoff, body);
}

void ClusterLinkSession::SendWowPacket(WorldPacket const& packet)
{
    OpcodeHandler const* handler = serverOpcodeTable[packet.GetOpcode()];
    if (!handler)
        return;

    ByteBuffer payload;
    payload << uint16(handler->OpcodeNumber);
    if (!packet.empty())
        payload.append(packet.contents(), packet.size());
    QueueFrame(Skyfire::Cluster::LinkMessage::WowOpcode, payload);
}

void ClusterLinkSession::QueueFrame(Skyfire::Cluster::LinkMessage type, ByteBuffer const& payload)
{
    if (_closed || !_writeQueue)
        return;

    ByteBuffer encoded;
    Skyfire::Cluster::EncodeFrame(encoded, type, payload);
    std::vector<char> bytes(encoded.size());
    if (!encoded.empty())
        memcpy(bytes.data(), encoded.contents(), encoded.size());

    auto self = shared_from_this();
    _writeQueue->Queue(std::move(bytes),
        [self](boost::system::error_code const& error, size_t)
        {
            if (error)
                self->Close();
        });
}

void ClusterLinkSession::AsyncRead()
{
    if (_closed || !_socket)
        return;

    auto self = shared_from_this();
    _socket->async_read_some(boost::asio::buffer(_readBuffer),
        [self](boost::system::error_code const& error, size_t transferred)
        {
            self->HandleRead(error, transferred);
        });
}

void ClusterLinkSession::HandleRead(boost::system::error_code const& error, size_t transferred)
{
    if (error || transferred == 0)
    {
        Close();
        return;
    }

    _pending.insert(_pending.end(), _readBuffer.begin(), _readBuffer.begin() + static_cast<std::ptrdiff_t>(transferred));

    Skyfire::Cluster::LinkFrame frame;
    while (Skyfire::Cluster::TryDecodeFrame(_pending, frame))
        HandleFrame(frame);

    AsyncRead();
}

void ClusterLinkSession::HandleFrame(Skyfire::Cluster::LinkFrame& frame)
{
    switch (frame.Type)
    {
        case Skyfire::Cluster::LinkMessage::Hello:
            HandleHello(frame.Payload);
            break;
        case Skyfire::Cluster::LinkMessage::Heartbeat:
            break;
        case Skyfire::Cluster::LinkMessage::Attach:
            HandleAttach(frame.Payload);
            break;
        case Skyfire::Cluster::LinkMessage::Resume:
            HandleResume(frame.Payload);
            break;
        case Skyfire::Cluster::LinkMessage::WowOpcode:
            HandleWowOpcode(frame.Payload);
            break;
        case Skyfire::Cluster::LinkMessage::Close:
            Close();
            break;
        default:
            SF_LOG_ERROR("network", "ClusterLink: unhandled message %u.", uint32(frame.Type));
            break;
    }
}

void ClusterLinkSession::HandleHello(ByteBuffer& payload)
{
    uint8 role = 0;
    uint32 nodeId = 0;
    uint32 realm = 0;
    std::string token;
    payload >> role >> nodeId >> realm >> token;

    ByteBuffer ack;
    if (token != sClusterMgr->GetSharedToken())
    {
        ack << uint8(Skyfire::Cluster::LinkStatus::BadToken);
        ack << uint32(sClusterMgr->GetNodeId());
        QueueFrame(Skyfire::Cluster::LinkMessage::HelloAck, ack);
        Close();
        return;
    }

    _authed = true;
    ack << uint8(Skyfire::Cluster::LinkStatus::Ok);
    ack << uint32(sClusterMgr->GetNodeId());
    QueueFrame(Skyfire::Cluster::LinkMessage::HelloAck, ack);
}

void ClusterLinkSession::HandleAttach(ByteBuffer& payload)
{
    if (!_authed)
    {
        Close();
        return;
    }

    uint32 accountId = 0;
    std::string accountName;
    std::string clientIp;
    uint8 expansion = 0;
    uint8 locale = 0;
    uint8 security = 0;
    uint32 recruiter = 0;
    uint8 hasBoost = 0;
    uint8 usedEmailLogin = 0;
    uint32 virtualRealmId = 0;
    SessionKey sessionKey = {};

    payload >> accountId >> accountName >> clientIp;
    payload >> expansion >> locale >> security >> recruiter >> hasBoost >> usedEmailLogin >> virtualRealmId;
    if (payload.size() - payload.rpos() < sessionKey.size())
    {
        Close();
        return;
    }
    payload.read(sessionKey.data(), sessionKey.size());

    WorldSession* session = new WorldSession(accountId, NULL, AccountTypes(security), expansion, time_t(0),
        LocaleConstant(locale), recruiter, false, hasBoost != 0, usedEmailLogin != 0, false, accountName, sessionKey);
    session->SetRemoteAddress(clientIp);
    session->SetVirtualRealmID(virtualRealmId);
    session->SetClusterLink(this);
    session->LoadGlobalAccountData();
    session->LoadTutorialsData();
    session->LoadPermissions();

    {
        std::lock_guard<std::mutex> guard(_sessionLock);
        _session = session;
    }

    sWorld->AddSession(session);

    ByteBuffer ack;
    ack << uint8(Skyfire::Cluster::LinkStatus::Ok);
    QueueFrame(Skyfire::Cluster::LinkMessage::AttachAck, ack);
}

void ClusterLinkSession::HandleResume(ByteBuffer& payload)
{
    if (!_authed)
    {
        Close();
        return;
    }

    uint32 accountId = 0;
    std::string accountName;
    std::string clientIp;
    uint8 expansion = 0;
    uint8 locale = 0;
    uint8 security = 0;
    uint32 recruiter = 0;
    uint8 hasBoost = 0;
    uint8 usedEmailLogin = 0;
    uint32 virtualRealmId = 0;
    SessionKey sessionKey = {};
    Skyfire::Cluster::HandoffPayload handoff;

    payload >> accountId >> accountName >> clientIp;
    payload >> expansion >> locale >> security >> recruiter >> hasBoost >> usedEmailLogin >> virtualRealmId;
    if (payload.size() - payload.rpos() < sessionKey.size())
    {
        Close();
        return;
    }
    payload.read(sessionKey.data(), sessionKey.size());
    if (!Skyfire::Cluster::ReadHandoffPayload(payload, handoff))
    {
        Close();
        return;
    }

    WorldSession* session = new WorldSession(accountId, NULL, AccountTypes(security), expansion, time_t(0),
        LocaleConstant(locale), recruiter, false, hasBoost != 0, usedEmailLogin != 0, false, accountName, sessionKey);
    session->SetRemoteAddress(clientIp);
    session->SetVirtualRealmID(virtualRealmId);
    session->SetClusterLink(this);
    session->LoadGlobalAccountData();
    session->LoadTutorialsData();
    session->LoadPermissions();
    if (handoff.Flags & Skyfire::Cluster::HANDOFF_FLAG_LOGIN)
        session->StartClusterLogin(handoff.CharacterGuid);
    else
        session->StartClusterResume(handoff.CharacterGuid, handoff.DestMap, handoff.X, handoff.Y, handoff.Z,
            handoff.O, handoff.Options);

    {
        std::lock_guard<std::mutex> guard(_sessionLock);
        _session = session;
    }

    sWorld->AddSession(session);

    ByteBuffer ack;
    ack << uint8(Skyfire::Cluster::LinkStatus::Ok);
    QueueFrame(Skyfire::Cluster::LinkMessage::ResumeAck, ack);
}

void ClusterLinkSession::HandleWowOpcode(ByteBuffer& payload)
{
    if (!_authed)
        return;

    if (payload.size() < 2)
        return;

    uint16 wire = 0;
    payload >> wire;
    Opcodes opcode = clientOpcodeTable.GetOpcodeByNumber(wire);
    WorldPacket* packet = new WorldPacket(opcode, payload.size() - payload.rpos());
    packet->SetReceivedOpcode(wire);
    if (payload.size() > payload.rpos())
        packet->append(payload.contents() + payload.rpos(), payload.size() - payload.rpos());

    std::lock_guard<std::mutex> guard(_sessionLock);
    if (_session)
        _session->QueuePacket(packet);
    else
        delete packet;
}
