/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_LINK_SESSION_H
#define SF_CLUSTER_LINK_SESSION_H

#include "Cluster/ClusterLinkProtocol.h"
#include "Network/BoostAsioWriteQueue.h"
#include <boost/asio/ip/tcp.hpp>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class WorldPacket;
class WorldSession;

class ClusterLinkSession : public std::enable_shared_from_this<ClusterLinkSession>
{
public:
    explicit ClusterLinkSession(std::unique_ptr<boost::asio::ip::tcp::socket> socket);
    ~ClusterLinkSession();

    void Start();
    void Close();
    bool IsClosed() const { return _closed; }

    void DetachWorldSession();
    void SendWowPacket(WorldPacket const& packet);
    void SendHandoff(Skyfire::Cluster::HandoffPayload const& payload);

private:
    void AsyncRead();
    void HandleRead(boost::system::error_code const& error, size_t transferred);
    void QueueFrame(Skyfire::Cluster::LinkMessage type, ByteBuffer const& payload);
    void HandleFrame(Skyfire::Cluster::LinkFrame& frame);
    void HandleHello(ByteBuffer& payload);
    void HandleAttach(ByteBuffer& payload);
    void HandleResume(ByteBuffer& payload);
    void HandleWowOpcode(ByteBuffer& payload);

    std::unique_ptr<boost::asio::ip::tcp::socket> _socket;
    std::unique_ptr<Skyfire::Net::BoostAsioWriteQueue<boost::asio::ip::tcp::socket>> _writeQueue;
    std::array<char, 4096> _readBuffer;
    std::vector<uint8> _pending;
    WorldSession* _session;
    std::mutex _sessionLock;
    std::atomic<bool> _closed;
    bool _authed;
};

#endif
