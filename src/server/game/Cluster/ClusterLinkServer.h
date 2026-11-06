/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_LINK_SERVER_H
#define SF_CLUSTER_LINK_SERVER_H

#include "Platform/Singleton.h"
#include "Threading/BoostAsioThreadGroup.h"
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <memory>
#include <string>

class ClusterLinkServer
{
    friend class Skyfire::Singleton<ClusterLinkServer, Skyfire::Mutex>;

public:
    bool Start(uint16 port, std::string const& bindIp);
    void Stop();

private:
    ClusterLinkServer();
    ~ClusterLinkServer();

    void AsyncAccept();
    void HandleAccept(std::shared_ptr<boost::asio::ip::tcp::socket> socket, boost::system::error_code const& error);

    Skyfire::Asio::IoContextThreadGroup _threadGroup;
    boost::asio::ip::tcp::acceptor _acceptor;
    std::atomic<bool> _closed;
};

#define sClusterLinkServer Skyfire::Singleton<ClusterLinkServer, Skyfire::Mutex>::instance()

#endif
