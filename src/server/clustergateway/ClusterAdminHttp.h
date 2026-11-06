/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_CLUSTER_ADMIN_HTTP_H
#define SF_CLUSTER_ADMIN_HTTP_H

#include "Common.h"
#include "Threading/BoostAsioThreadGroup.h"
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <memory>
#include <string>

void ClusterAdminHttpSetCharacterDatabase(bool ready);

class ClusterAdminHttp
{
public:
    ClusterAdminHttp();
    ~ClusterAdminHttp();

    bool Open(uint16 port, std::string const& bindIp);
    void Close();

private:
    void AsyncAccept();
    void HandleAccept(std::shared_ptr<boost::asio::ip::tcp::socket> socket, boost::system::error_code const& error);

    Skyfire::Asio::IoContextThreadGroup _threadGroup;
    boost::asio::ip::tcp::acceptor _acceptor;
    std::atomic<bool> _closed;
};

#endif
