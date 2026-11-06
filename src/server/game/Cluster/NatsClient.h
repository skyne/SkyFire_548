/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_NATS_CLIENT_H
#define SF_NATS_CLIENT_H

#include "Threading/BoostAsioExecutor.h"
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

class NatsClient
{
public:
    typedef std::function<void(std::vector<uint8> payload)> MessageHandler;

    NatsClient();
    ~NatsClient();

    bool Start(std::string const& host, uint16 port);
    void Stop();
    void Publish(uint8 const* data, size_t size);
    void SetMessageHandler(MessageHandler handler) { _handler = std::move(handler); }

private:
    void Connect();
    void ScheduleReconnect();
    void StartRead();
    void Parse();
    void HandleLine(std::string const& line);
    void Enqueue(std::string frame);
    void PumpWrite();
    void Fail();

    Skyfire::Asio::IoContextExecutor _executor;
    std::thread _thread;
    std::unique_ptr<boost::asio::ip::tcp::socket> _socket;
    std::unique_ptr<boost::asio::steady_timer> _timer;
    std::string _host;
    std::string _port;
    std::array<char, 8192> _readBuf;
    std::string _inbound;
    std::deque<std::string> _outbound;
    std::string _currentWrite;
    size_t _payloadNeed;
    bool _ready;
    bool _greeted;
    bool _writing;
    std::atomic<bool> _stop;
    MessageHandler _handler;
};

#endif
