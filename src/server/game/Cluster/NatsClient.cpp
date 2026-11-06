/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "NatsClient.h"
#include "Log.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <cstring>
#include <sstream>

namespace
{
    constexpr size_t MAX_QUEUED_FRAMES = 256;
    constexpr size_t MAX_INBOUND = 2 * 1024 * 1024;
    constexpr size_t MAX_PAYLOAD = 1024 * 1024;

    std::string FramePublish(uint8 const* data, size_t size)
    {
        std::string frame = "PUB skyfire.bus " + std::to_string(size) + "\r\n";
        frame.append(reinterpret_cast<char const*>(data), size);
        frame.append("\r\n");
        return frame;
    }
}

NatsClient::NatsClient() :
    _executor(),
    _thread(),
    _socket(),
    _timer(),
    _host(),
    _port(),
    _readBuf(),
    _inbound(),
    _outbound(),
    _currentWrite(),
    _payloadNeed(size_t(-1)),
    _ready(false),
    _greeted(false),
    _writing(false),
    _stop(false),
    _handler()
{
}

NatsClient::~NatsClient()
{
    Stop();
}

bool NatsClient::Start(std::string const& host, uint16 port)
{
    if (_thread.joinable())
        return false;

    _stop = false;
    _host = host;
    _port = std::to_string(port);
    _executor.Restart();
    _executor.KeepAlive();

    try
    {
        _thread = std::thread([this] { _executor.Run(); });
    }
    catch (...)
    {
        _executor.ResetWork();
        _executor.Stop();
        return false;
    }

    _executor.Post([this] { Connect(); });
    return true;
}

void NatsClient::Stop()
{
    if (!_thread.joinable())
        return;

    _stop = true;
    _executor.Post([this]
    {
        if (_timer)
            _timer->cancel();
        if (_socket)
        {
            boost::system::error_code ignore;
            _socket->close(ignore);
        }
    });
    _executor.ResetWork();
    _executor.Stop();
    _thread.join();
    _socket.reset();
    _timer.reset();
    _ready = false;
    _writing = false;
    _outbound.clear();
    _inbound.clear();
}

void NatsClient::Publish(uint8 const* data, size_t size)
{
    if (_stop || !data || size > MAX_PAYLOAD)
        return;

    std::string frame = FramePublish(data, size);
    _executor.Post([this, frame = std::move(frame)]() mutable { Enqueue(std::move(frame)); });
}

void NatsClient::Connect()
{
    if (_stop)
        return;

    boost::system::error_code ec;
    boost::asio::ip::tcp::resolver resolver(_executor.GetIoContext());
    auto endpoints = resolver.resolve(_host, _port, ec);
    if (ec)
    {
        SF_LOG_ERROR("network", "NATS resolve %s:%s failed: %s", _host.c_str(), _port.c_str(), ec.message().c_str());
        ScheduleReconnect();
        return;
    }

    _socket.reset(new boost::asio::ip::tcp::socket(_executor.GetIoContext()));
    boost::asio::connect(*_socket, endpoints, ec);
    if (ec)
    {
        SF_LOG_ERROR("network", "NATS connect %s:%s failed: %s", _host.c_str(), _port.c_str(), ec.message().c_str());
        _socket.reset();
        ScheduleReconnect();
        return;
    }

    _inbound.clear();
    _payloadNeed = size_t(-1);
    _ready = false;
    _greeted = false;
    SF_LOG_INFO("network", "NATS connected to %s:%s.", _host.c_str(), _port.c_str());
    StartRead();
}

void NatsClient::ScheduleReconnect()
{
    if (_stop)
        return;

    if (!_timer)
        _timer.reset(new boost::asio::steady_timer(_executor.GetIoContext()));
    _timer->expires_after(std::chrono::seconds(2));
    _timer->async_wait([this](boost::system::error_code const& ec)
    {
        if (!ec && !_stop)
            Connect();
    });
}

void NatsClient::StartRead()
{
    if (!_socket || _stop)
        return;

    _socket->async_read_some(boost::asio::buffer(_readBuf), [this](boost::system::error_code const& ec, size_t transferred)
    {
        if (ec)
        {
            if (!_stop)
                SF_LOG_ERROR("network", "NATS read failed: %s", ec.message().c_str());
            Fail();
            return;
        }

        if (_inbound.size() + transferred > MAX_INBOUND)
        {
            SF_LOG_ERROR("network", "NATS inbound buffer overflow.");
            Fail();
            return;
        }

        _inbound.append(_readBuf.data(), transferred);
        Parse();
        if (_socket)
            StartRead();
    });
}

void NatsClient::Parse()
{
    for (;;)
    {
        if (_payloadNeed != size_t(-1))
        {
            if (_inbound.size() < _payloadNeed + 2)
                return;

            std::vector<uint8> payload(_payloadNeed);
            if (_payloadNeed)
                memcpy(payload.data(), _inbound.data(), _payloadNeed);
            _inbound.erase(0, _payloadNeed + 2);
            _payloadNeed = size_t(-1);
            if (_handler)
                _handler(std::move(payload));
            continue;
        }

        size_t lineEnd = _inbound.find("\r\n");
        if (lineEnd == std::string::npos)
            return;

        std::string line = _inbound.substr(0, lineEnd);
        _inbound.erase(0, lineEnd + 2);
        HandleLine(line);
    }
}

void NatsClient::HandleLine(std::string const& line)
{
    if (line.empty())
        return;

    if (line == "PING")
    {
        Enqueue("PONG\r\n");
        return;
    }

    if (!_greeted && line.compare(0, 4, "INFO") == 0)
    {
        _greeted = true;
        _ready = true;
        Enqueue("CONNECT {\"verbose\":false,\"pedantic\":false,\"echo\":false,\"protocol\":1,\"name\":\"skyfire-world\"}\r\nSUB skyfire.bus 1\r\n");
        return;
    }

    if (line.compare(0, 4, "-ERR") == 0)
    {
        SF_LOG_ERROR("network", "NATS error: %s", line.c_str());
        Fail();
        return;
    }

    if (line.compare(0, 3, "MSG") != 0)
        return;

    std::istringstream stream(line);
    std::string tag;
    std::string subject;
    std::string sid;
    std::string extra;
    std::string sizeText;
    stream >> tag >> subject >> sid >> extra;
    if (extra.empty())
        return;
    if (stream >> sizeText)
    {
        // MSG subject sid reply size
    }
    else
        sizeText = extra;

    char* end = NULL;
    unsigned long size = std::strtoul(sizeText.c_str(), &end, 10);
    if (!end || *end != '\0' || size > MAX_PAYLOAD)
    {
        SF_LOG_ERROR("network", "NATS bad MSG line: %s", line.c_str());
        Fail();
        return;
    }

    _payloadNeed = size;
}

void NatsClient::Enqueue(std::string frame)
{
    if (!_ready)
        return;

    if (_outbound.size() >= MAX_QUEUED_FRAMES)
    {
        SF_LOG_ERROR("network", "NATS outbound queue full, dropping a frame.");
        return;
    }

    _outbound.push_back(std::move(frame));
    PumpWrite();
}

void NatsClient::PumpWrite()
{
    if (_writing || _outbound.empty() || !_socket)
        return;

    _writing = true;
    _currentWrite = std::move(_outbound.front());
    _outbound.pop_front();
    boost::asio::async_write(*_socket, boost::asio::buffer(_currentWrite), [this](boost::system::error_code const& ec, size_t)
    {
        _writing = false;
        if (ec)
        {
            if (!_stop)
                SF_LOG_ERROR("network", "NATS write failed: %s", ec.message().c_str());
            Fail();
            return;
        }
        PumpWrite();
    });
}

void NatsClient::Fail()
{
    _ready = false;
    _greeted = false;
    _writing = false;
    _payloadNeed = size_t(-1);
    _inbound.clear();
    _outbound.clear();
    _currentWrite.clear();
    if (_socket)
    {
        boost::system::error_code ignore;
        _socket->close(ignore);
        _socket.reset();
    }
    ScheduleReconnect();
}
