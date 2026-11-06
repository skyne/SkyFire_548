/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#ifndef SF_GATEWAY_CLIENT_H
#define SF_GATEWAY_CLIENT_H

#include "AuthCrypt.h"
#include "ByteBuffer.h"
#include "ClusterLinkClient.h"
#include "Network/BoostAsioWriteQueue.h"
#include <boost/asio/ip/tcp.hpp>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

class GatewayClient : public std::enable_shared_from_this<GatewayClient>
{
public:
    GatewayClient(boost::asio::io_context& ioContext, boost::asio::ip::tcp::socket socket);
    ~GatewayClient();

    void Start();

private:
    void Close();
    void AsyncRead();
    void HandleRead(boost::system::error_code const& error, size_t transferred);
    int Consume(char const* data, size_t length);
    int HandleHeader();
    int HandlePayload();
    int SendWire(uint16 opcode, uint8 const* data, size_t size);
    int SendConnectivity();
    int SendAuthChallenge();
    int HandleIncoming(uint16 opcode, ByteBuffer& body);
    int HandleAuthSession(ByteBuffer& body);
    int HandlePing(ByteBuffer& body);
    int SendAuthError(uint8 code);
    bool OpenWorldLink(uint32 accountId, uint32 virtualRealmId);
    bool LookupWorld(uint32 accountId, uint32 mapId, std::string& host, uint16& port);
    bool ConnectWorld(std::string const& host, uint16 port, Skyfire::Cluster::HandoffPayload const* resume);
    void BindWorldHandlers();
    void HandleHandoff(Skyfire::Cluster::HandoffPayload const& handoff);
    void SendTransferPackets(Skyfire::Cluster::HandoffPayload const& handoff);
    void BeginProxyRead();
    void HandleProxyRead(boost::system::error_code const& error, size_t transferred);
    bool TryConsumeProxy();

    boost::asio::io_context& _ioContext;
    boost::asio::ip::tcp::socket _socket;
    uint16 _pendingOpcode;
    std::unique_ptr<Skyfire::Net::BoostAsioWriteQueue<boost::asio::ip::tcp::socket>> _writeQueue;
    AuthCrypt _crypt;
    std::string _address;
    std::array<uint8, 32> _encryptionSeeds;
    std::array<uint8, 4> _seed;
    std::array<char, 4096> _readBuffer;
    std::vector<uint8> _header;
    size_t _headerRead;
    ByteBuffer _payload;
    size_t _payloadNeed;
    std::shared_ptr<ClusterLinkClient> _worldLink;
    uint32 _accountId;
    SessionKey _sessionKey;
    std::string _accountName;
    uint8 _expansion;
    uint8 _locale;
    uint8 _security;
    uint32 _recruiter;
    bool _hasBoost;
    bool _usedEmailLogin;
    uint32 _virtualRealmId;
    std::atomic<bool> _closed;
    bool _proxyProtocol;
    std::vector<uint8> _proxyBuffer;
};

#endif
