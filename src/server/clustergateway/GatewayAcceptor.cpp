/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "GatewayAcceptor.h"
#include "GatewayClient.h"
#include "Log.h"
#include "Network/BoostAsioUtils.h"

GatewayAcceptor::GatewayAcceptor() :
    _threadGroup(),
    _acceptor(_threadGroup.GetIoContext()),
    _closed(true)
{
}

GatewayAcceptor::~GatewayAcceptor()
{
    Close();
}

bool GatewayAcceptor::Open(uint16 port, std::string const& bindIp)
{
    if (!Skyfire::Net::OpenTcpAcceptor(_threadGroup.GetIoContext(), _acceptor, port, bindIp, "server.clustergateway", "gateway"))
        return false;

    _closed = false;
    AsyncAccept();
    if (_threadGroup.Start(1) == -1)
    {
        Close();
        return false;
    }

    return true;
}

void GatewayAcceptor::Close()
{
    bool expected = false;
    if (!_closed.compare_exchange_strong(expected, true))
        return;

    Skyfire::Net::CloseTcpAcceptor(_acceptor);
    _threadGroup.StopAndJoin();
}

void GatewayAcceptor::AsyncAccept()
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

void GatewayAcceptor::HandleAccept(std::shared_ptr<boost::asio::ip::tcp::socket> socket, boost::system::error_code const& error)
{
    if (_closed)
        return;

    if (!error)
    {
        std::shared_ptr<GatewayClient> client(new GatewayClient(_threadGroup.GetIoContext(), std::move(*socket)));
        client->Start();
    }
    else if (error != boost::asio::error::operation_aborted)
        SF_LOG_ERROR("server.clustergateway", "Failed to accept client, error %d", error.value());

    AsyncAccept();
}
