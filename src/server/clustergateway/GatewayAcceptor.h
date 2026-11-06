/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_GATEWAY_ACCEPTOR_H
#define SF_GATEWAY_ACCEPTOR_H

#include "Threading/BoostAsioThreadGroup.h"
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <string>

class GatewayAcceptor
{
public:
    GatewayAcceptor();
    ~GatewayAcceptor();

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
