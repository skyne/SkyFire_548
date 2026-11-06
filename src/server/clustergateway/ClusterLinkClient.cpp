/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "ClusterLinkClient.h"
#include "Log.h"
#include "Network/BoostAsioUtils.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <cstring>

ClusterLinkClient::ClusterLinkClient() :
    _ioContext(),
    _thread(),
    _closed(false)
{
}

ClusterLinkClient::~ClusterLinkClient()
{
    Close();
    _ioContext.stop();
    if (_thread.joinable() && _thread.get_id() != std::this_thread::get_id())
        _thread.join();
}

bool ClusterLinkClient::Connect(std::string const& host, uint16 port)
{
    try
    {
        _socket.reset(new boost::asio::ip::tcp::socket(_ioContext));
        boost::asio::ip::tcp::resolver resolver(_ioContext);
        boost::asio::connect(*_socket, resolver.resolve(host, std::to_string(port)));
        _writeQueue.reset(new Skyfire::Net::BoostAsioWriteQueue<boost::asio::ip::tcp::socket>(*_socket));
        return true;
    }
    catch (std::exception const& e)
    {
        SF_LOG_ERROR("server.clustergateway", "ClusterLink connect to %s:%u failed: %s", host.c_str(), port, e.what());
        Close();
        return false;
    }
}

void ClusterLinkClient::Close()
{
    if (_closed.exchange(true))
        return;

    if (_socket)
        Skyfire::Net::CloseTcpSocket(*_socket);
}

void ClusterLinkClient::QueueFrame(Skyfire::Cluster::LinkMessage type, ByteBuffer const& payload)
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

bool ClusterLinkClient::WaitMessage(Skyfire::Cluster::LinkMessage expected, ByteBuffer& payload)
{
    while (!_closed)
    {
        Skyfire::Cluster::LinkFrame frame;
        if (Skyfire::Cluster::TryDecodeFrame(_pending, frame))
        {
            if (frame.Type != expected)
                return false;
            payload = frame.Payload;
            return true;
        }

        std::array<char, 4096> buffer;
        boost::system::error_code error;
        size_t n = _socket->read_some(boost::asio::buffer(buffer), error);
        if (error || n == 0)
            return false;
        _pending.insert(_pending.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
    }
    return false;
}

bool ClusterLinkClient::Hello(uint32 realmId, std::string const& token)
{
    ByteBuffer hello;
    hello << uint8(Skyfire::Cluster::LinkRole::Gateway);
    hello << uint32(0);
    hello << uint32(realmId);
    hello << token;

    ByteBuffer encoded;
    Skyfire::Cluster::EncodeFrame(encoded, Skyfire::Cluster::LinkMessage::Hello, hello);
    boost::system::error_code error;
    boost::asio::write(*_socket, boost::asio::buffer(encoded.contents(), encoded.size()), error);
    if (error)
        return false;

    ByteBuffer ack;
    if (!WaitMessage(Skyfire::Cluster::LinkMessage::HelloAck, ack))
        return false;
    uint8 status = 0;
    ack >> status;
    return status == uint8(Skyfire::Cluster::LinkStatus::Ok);
}

bool ClusterLinkClient::Attach(uint32 accountId, std::string const& accountName, std::string const& clientIp,
    uint8 expansion, uint8 locale, uint8 security, uint32 recruiter, bool hasBoost,
    bool usedEmailLogin, uint32 virtualRealmId, SessionKey const& sessionKey,
    ByteBuffer const& addonInfo)
{
    ByteBuffer attach;
    attach << accountId << accountName << clientIp;
    attach << expansion << locale << security << recruiter << uint8(hasBoost ? 1 : 0)
        << uint8(usedEmailLogin ? 1 : 0) << virtualRealmId;
    attach.append(sessionKey.data(), sessionKey.size());
    attach << uint32(addonInfo.size());
    if (addonInfo.size())
        attach.append(addonInfo.contents(), addonInfo.size());

    ByteBuffer encoded;
    Skyfire::Cluster::EncodeFrame(encoded, Skyfire::Cluster::LinkMessage::Attach, attach);
    boost::system::error_code error;
    boost::asio::write(*_socket, boost::asio::buffer(encoded.contents(), encoded.size()), error);
    if (error)
        return false;

    ByteBuffer ack;
    if (!WaitMessage(Skyfire::Cluster::LinkMessage::AttachAck, ack))
        return false;
    uint8 status = 0;
    ack >> status;
    return status == uint8(Skyfire::Cluster::LinkStatus::Ok);
}

bool ClusterLinkClient::Resume(uint32 accountId, std::string const& accountName, std::string const& clientIp,
    uint8 expansion, uint8 locale, uint8 security, uint32 recruiter, bool hasBoost,
    bool usedEmailLogin, uint32 virtualRealmId, SessionKey const& sessionKey,
    Skyfire::Cluster::HandoffPayload const& handoff)
{
    ByteBuffer resume;
    resume << accountId << accountName << clientIp;
    resume << expansion << locale << security << recruiter << uint8(hasBoost ? 1 : 0)
        << uint8(usedEmailLogin ? 1 : 0) << virtualRealmId;
    resume.append(sessionKey.data(), sessionKey.size());
    Skyfire::Cluster::WriteHandoffPayload(resume, handoff);

    ByteBuffer encoded;
    Skyfire::Cluster::EncodeFrame(encoded, Skyfire::Cluster::LinkMessage::Resume, resume);
    boost::system::error_code error;
    boost::asio::write(*_socket, boost::asio::buffer(encoded.contents(), encoded.size()), error);
    if (error)
        return false;

    ByteBuffer ack;
    if (!WaitMessage(Skyfire::Cluster::LinkMessage::ResumeAck, ack))
        return false;
    uint8 status = 0;
    ack >> status;
    return status == uint8(Skyfire::Cluster::LinkStatus::Ok);
}

void ClusterLinkClient::SendWowOpcode(uint16 wireOpcode, uint8 const* data, size_t size)
{
    ByteBuffer payload;
    payload << wireOpcode;
    if (size)
        payload.append(data, size);
    QueueFrame(Skyfire::Cluster::LinkMessage::WowOpcode, payload);
}

void ClusterLinkClient::StartRead()
{
    ProcessPending();
    AsyncRead();
    if (_thread.joinable())
        return;

    _thread = std::thread([this]()
    {
        _ioContext.run();
    });
}

void ClusterLinkClient::AsyncRead()
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

void ClusterLinkClient::DispatchFrame(Skyfire::Cluster::LinkFrame& frame)
{
    if (frame.Type == Skyfire::Cluster::LinkMessage::WowOpcode && frame.Payload.size() >= 2 && _wowHandler)
    {
        uint16 wire = 0;
        frame.Payload >> wire;
        ByteBuffer body;
        if (frame.Payload.size() > frame.Payload.rpos())
            body.append(frame.Payload.contents() + frame.Payload.rpos(), frame.Payload.size() - frame.Payload.rpos());
        _wowHandler(wire, body);
    }
    else if (frame.Type == Skyfire::Cluster::LinkMessage::Handoff && _handoffHandler)
    {
        Skyfire::Cluster::HandoffPayload handoff;
        if (Skyfire::Cluster::ReadHandoffPayload(frame.Payload, handoff))
            _handoffHandler(handoff);
    }
    else if (frame.Type == Skyfire::Cluster::LinkMessage::Close)
    {
        if (_closeHandler)
            _closeHandler();
        Close();
    }
}

void ClusterLinkClient::ProcessPending()
{
    Skyfire::Cluster::LinkFrame frame;
    while (!_closed && Skyfire::Cluster::TryDecodeFrame(_pending, frame))
        DispatchFrame(frame);
}

void ClusterLinkClient::HandleRead(boost::system::error_code const& error, size_t transferred)
{
    if (error || transferred == 0)
    {
        if (_closeHandler)
            _closeHandler();
        Close();
        return;
    }

    _pending.insert(_pending.end(), _readBuffer.begin(), _readBuffer.begin() + static_cast<std::ptrdiff_t>(transferred));
    ProcessPending();
    AsyncRead();
}
