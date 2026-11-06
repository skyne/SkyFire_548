/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_LINK_CLIENT_H
#define SF_CLUSTER_LINK_CLIENT_H

#include "Authentication/AuthDefines.h"
#include "Cluster/ClusterLinkProtocol.h"
#include "Network/BoostAsioWriteQueue.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class ClusterLinkClient : public std::enable_shared_from_this<ClusterLinkClient>
{
public:
    typedef std::function<void(uint16, ByteBuffer)> WowHandler;
    typedef std::function<void(Skyfire::Cluster::HandoffPayload const&)> HandoffHandler;
    typedef std::function<void()> CloseHandler;

    ClusterLinkClient();
    ~ClusterLinkClient();

    bool Connect(std::string const& host, uint16 port);
    void Close();
    bool IsClosed() const { return _closed; }

    bool Hello(uint32 realmId, std::string const& token);
    bool Attach(uint32 accountId, std::string const& accountName, std::string const& clientIp,
        uint8 expansion, uint8 locale, uint8 security, uint32 recruiter, bool hasBoost,
        bool usedEmailLogin, uint32 virtualRealmId, SessionKey const& sessionKey);
    bool Resume(uint32 accountId, std::string const& accountName, std::string const& clientIp,
        uint8 expansion, uint8 locale, uint8 security, uint32 recruiter, bool hasBoost,
        bool usedEmailLogin, uint32 virtualRealmId, SessionKey const& sessionKey,
        Skyfire::Cluster::HandoffPayload const& handoff);
    void SendWowOpcode(uint16 wireOpcode, uint8 const* data, size_t size);
    void SetWowHandler(WowHandler handler) { _wowHandler = std::move(handler); }
    void SetHandoffHandler(HandoffHandler handler) { _handoffHandler = std::move(handler); }
    void SetCloseHandler(CloseHandler handler) { _closeHandler = std::move(handler); }
    void StartRead();

private:
    void AsyncRead();
    void HandleRead(boost::system::error_code const& error, size_t transferred);
    void ProcessPending();
    void DispatchFrame(Skyfire::Cluster::LinkFrame& frame);
    void QueueFrame(Skyfire::Cluster::LinkMessage type, ByteBuffer const& payload);
    bool WaitMessage(Skyfire::Cluster::LinkMessage expected, ByteBuffer& payload);

    // Own io_context so the blocking handshake never runs on the client socket's thread.
    boost::asio::io_context _ioContext;
    std::thread _thread;
    std::unique_ptr<boost::asio::ip::tcp::socket> _socket;
    std::unique_ptr<Skyfire::Net::BoostAsioWriteQueue<boost::asio::ip::tcp::socket>> _writeQueue;
    std::array<char, 4096> _readBuffer;
    std::vector<uint8> _pending;
    WowHandler _wowHandler;
    HandoffHandler _handoffHandler;
    CloseHandler _closeHandler;
    std::atomic<bool> _closed;
};

#endif
