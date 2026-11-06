/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "ClusterLinkServer.h"
#include "ClusterLinkSession.h"
#include "Log.h"
#include "Network/BoostAsioUtils.h"

ClusterLinkServer::ClusterLinkServer() :
    _threadGroup(),
    _acceptor(_threadGroup.GetIoContext()),
    _closed(true)
{
}

ClusterLinkServer::~ClusterLinkServer()
{
    Stop();
}

bool ClusterLinkServer::Start(uint16 port, std::string const& bindIp)
{
    if (!Skyfire::Net::OpenTcpAcceptor(_threadGroup.GetIoContext(), _acceptor, port, bindIp, "network", "clusterlink"))
        return false;

    _closed = false;
    AsyncAccept();
    if (_threadGroup.Start(1) == -1)
    {
        Stop();
        return false;
    }

    SF_LOG_INFO("server.worldserver", "ClusterLink listening on %s:%u", bindIp.c_str(), port);
    return true;
}

void ClusterLinkServer::Stop()
{
    bool expected = false;
    if (!_closed.compare_exchange_strong(expected, true))
        return;

    Skyfire::Net::CloseTcpAcceptor(_acceptor);
    _threadGroup.StopAndJoin();
}

void ClusterLinkServer::AsyncAccept()
{
    if (!_acceptor.is_open())
        return;

    std::shared_ptr<boost::asio::ip::tcp::socket> socket(new boost::asio::ip::tcp::socket(_threadGroup.GetIoContext()));
    _acceptor.async_accept(*socket,
        [this, socket](boost::system::error_code const& error)
        {
            HandleAccept(socket, error);
        });
}

void ClusterLinkServer::HandleAccept(std::shared_ptr<boost::asio::ip::tcp::socket> socket, boost::system::error_code const& error)
{
    if (_closed)
        return;

    if (!error)
    {
        std::unique_ptr<boost::asio::ip::tcp::socket> owned(new boost::asio::ip::tcp::socket(std::move(*socket)));
        std::shared_ptr<ClusterLinkSession> session(new ClusterLinkSession(std::move(owned)));
        session->Start();
    }
    else if (error != boost::asio::error::operation_aborted)
        SF_LOG_ERROR("network", "ClusterLink accept failed, error %d", error.value());

    AsyncAccept();
}
