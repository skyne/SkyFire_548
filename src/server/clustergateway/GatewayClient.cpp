/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "GatewayClient.h"
#include "Auth/LoginIdentity.h"
#include "Cluster/WowWireOpcodes.h"
#include "Config.h"
#include "CryptoHash.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Network/BoostAsioUtils.h"
#include "Util.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <boost/asio/post.hpp>

#if defined(__GNUC__)
#pragma pack(1)
#else
#pragma pack(push, 1)
#endif

struct GatewayServerHeader
{
    uint8 header[4];
    uint8 length;

    GatewayServerHeader(uint32 size, uint16 cmd, AuthCrypt& crypt)
    {
        length = 4;
        if (crypt.IsInitialized())
        {
            uint32 data = (size << 13) | (cmd & Skyfire::Cluster::WIRE_MAX_OPCODE);
            memcpy(header, &data, 4);
            crypt.EncryptSend(header, 4);
        }
        else
        {
            memcpy(header, &size, 2);
            memcpy(header + 2, &cmd, 2);
        }
    }
};

#if defined(__GNUC__)
#pragma pack()
#else
#pragma pack(pop)
#endif

GatewayClient::GatewayClient(boost::asio::io_context& ioContext, boost::asio::ip::tcp::socket socket) :
    _ioContext(ioContext),
    _socket(std::move(socket)),
    _pendingOpcode(0),
    _headerRead(0),
    _payloadNeed(0),
    _accountId(0),
    _expansion(0),
    _locale(0),
    _security(0),
    _recruiter(0),
    _hasBoost(false),
    _usedEmailLogin(false),
    _virtualRealmId(0),
    _closed(false),
    _proxyProtocol(sConfigMgr->GetBoolDefault("Gateway.ProxyProtocol", false))
{
    boost::system::error_code error;
    boost::asio::ip::tcp::endpoint endpoint = _socket.remote_endpoint(error);
    _address = error ? "unknown" : endpoint.address().to_string();
    _writeQueue.reset(new Skyfire::Net::BoostAsioWriteQueue<boost::asio::ip::tcp::socket>(_socket));
    SkyFire::Crypto::GetRandomBytes(_encryptionSeeds);
    SkyFire::Crypto::GetRandomBytes(_seed);
}

GatewayClient::~GatewayClient()
{
    Close();
}

void GatewayClient::Start()
{
    if (_proxyProtocol)
    {
        BeginProxyRead();
        return;
    }
    SendConnectivity();
    AsyncRead();
}

void GatewayClient::BeginProxyRead()
{
    if (_closed)
        return;
    auto self = shared_from_this();
    _socket.async_read_some(boost::asio::buffer(_readBuffer),
        [self](boost::system::error_code const& error, size_t transferred)
        {
            self->HandleProxyRead(error, transferred);
        });
}

void GatewayClient::HandleProxyRead(boost::system::error_code const& error, size_t transferred)
{
    if (error || transferred == 0)
    {
        Close();
        return;
    }

    _proxyBuffer.insert(_proxyBuffer.end(), _readBuffer.begin(), _readBuffer.begin() + static_cast<std::ptrdiff_t>(transferred));
    if (!TryConsumeProxy())
        return;

    SendConnectivity();
    if (!_proxyBuffer.empty())
    {
        if (Consume(reinterpret_cast<char const*>(_proxyBuffer.data()), _proxyBuffer.size()) == -1)
        {
            Close();
            return;
        }
        _proxyBuffer.clear();
    }
    AsyncRead();
}

bool GatewayClient::TryConsumeProxy()
{
    static uint8 const sig[12] = { 0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A };
    if (_proxyBuffer.size() < 16)
    {
        BeginProxyRead();
        return false;
    }
    if (memcmp(_proxyBuffer.data(), sig, 12) != 0)
    {
        SF_LOG_ERROR("server.clustergateway", "PROXY protocol expected from %s.", _address.c_str());
        Close();
        return false;
    }

    uint8 verCmd = _proxyBuffer[12];
    uint8 fam = _proxyBuffer[13];
    uint16 len = uint16(_proxyBuffer[14] << 8 | _proxyBuffer[15]);
    size_t total = 16 + len;
    if (_proxyBuffer.size() < total)
    {
        BeginProxyRead();
        return false;
    }

    if ((verCmd & 0xF0) != 0x20)
    {
        Close();
        return false;
    }

    if ((verCmd & 0x0F) == 0x01 && (fam & 0xF0) == 0x10 && len >= 12)
    {
        char ip[INET_ADDRSTRLEN] = { 0 };
        inet_ntop(AF_INET, &_proxyBuffer[16], ip, sizeof(ip));
        if (ip[0])
            _address = ip;
    }

    _proxyBuffer.erase(_proxyBuffer.begin(), _proxyBuffer.begin() + static_cast<std::ptrdiff_t>(total));
    return true;
}

void GatewayClient::Close()
{
    if (_closed.exchange(true))
        return;

    if (_worldLink)
        _worldLink->Close();
    Skyfire::Net::CloseTcpSocket(_socket);
}

void GatewayClient::AsyncRead()
{
    if (_closed)
        return;

    auto self = shared_from_this();
    _socket.async_read_some(boost::asio::buffer(_readBuffer),
        [self](boost::system::error_code const& error, size_t transferred)
        {
            self->HandleRead(error, transferred);
        });
}

void GatewayClient::HandleRead(boost::system::error_code const& error, size_t transferred)
{
    if (error || transferred == 0)
    {
        Close();
        return;
    }

    if (Consume(_readBuffer.data(), transferred) == -1)
    {
        Close();
        return;
    }

    AsyncRead();
}

int GatewayClient::Consume(char const* data, size_t length)
{
    size_t offset = 0;
    while (offset < length)
    {
        size_t headerNeed = _crypt.IsInitialized() ? 4 : 6;
        if (_headerRead < headerNeed)
        {
            size_t take = std::min(headerNeed - _headerRead, length - offset);
            _header.insert(_header.end(), data + offset, data + offset + take);
            _headerRead += take;
            offset += take;
            if (_headerRead < headerNeed)
                return 0;
            if (HandleHeader() == -1)
                return -1;
        }

        if (_payloadNeed > 0)
        {
            size_t take = std::min(_payloadNeed - _payload.size(), length - offset);
            _payload.append(reinterpret_cast<uint8 const*>(data + offset), take);
            offset += take;
            if (_payload.size() < _payloadNeed)
                return 0;
        }

        if (HandlePayload() == -1)
            return -1;
    }
    return 0;
}

int GatewayClient::HandleHeader()
{
    if (_crypt.IsInitialized())
    {
        uint8 raw[4];
        memcpy(raw, _header.data(), 4);
        _crypt.DecryptRecv(raw, 4);
        uint32 value = 0;
        memcpy(&value, raw, 4);
        uint16 cmd = uint16(value & Skyfire::Cluster::WIRE_MAX_OPCODE);
        uint32 size = (value & ~uint32(Skyfire::Cluster::WIRE_MAX_OPCODE)) >> 13;
        if (size > 10236)
            return -1;
        _pendingOpcode = cmd;
        _payloadNeed = size;
        _payload.clear();
        return 0;
    }

    uint16 size = 0;
    uint32 cmd = 0;
    memcpy(&size, _header.data(), 2);
    memcpy(&cmd, _header.data() + 2, 4);
    if (size < 4 || size > 10240)
        return -1;
    _pendingOpcode = uint16(cmd);
    _payloadNeed = size - 4;
    _payload.clear();
    return 0;
}

int GatewayClient::HandlePayload()
{
    int result = HandleIncoming(_pendingOpcode, _payload);
    _header.clear();
    _headerRead = 0;
    _payload.clear();
    _payloadNeed = 0;
    return result;
}

int GatewayClient::SendWire(uint16 opcode, uint8 const* data, size_t size)
{
    if (_closed)
        return -1;

    uint32 headerSize = _crypt.IsInitialized() ? uint32(size) : uint32(size + 2);
    GatewayServerHeader header(headerSize, opcode, _crypt);
    std::vector<char> serialized;
    serialized.insert(serialized.end(), reinterpret_cast<char*>(header.header),
        reinterpret_cast<char*>(header.header) + header.length);
    if (size)
        serialized.insert(serialized.end(), reinterpret_cast<char const*>(data),
            reinterpret_cast<char const*>(data) + size);

    auto self = shared_from_this();
    _writeQueue->Queue(std::move(serialized),
        [self](boost::system::error_code const& error, size_t)
        {
            if (error)
                self->Close();
        });
    return 0;
}

int GatewayClient::SendConnectivity()
{
    std::string body("RLD OF WARCRAFT CONNECTION - SERVER TO CLIENT");
    return SendWire(Skyfire::Cluster::WIRE_MSG_VERIFY_CONNECTIVITY,
        reinterpret_cast<uint8 const*>(body.c_str()), body.size() + 1);
}

int GatewayClient::SendAuthChallenge()
{
    ByteBuffer packet;
    packet << uint16(0);
    packet.append(_encryptionSeeds);
    packet << uint8(1);
    packet.append(_seed);
    return SendWire(Skyfire::Cluster::WIRE_SMSG_AUTH_CHALLENGE, packet.contents(), packet.size());
}

int GatewayClient::SendAuthError(uint8 code)
{
    ByteBuffer packet;
    packet.WriteBit(0);
    packet.WriteBit(0);
    packet << uint8(code);
    packet.FlushBits();
    return SendWire(Skyfire::Cluster::WIRE_SMSG_AUTH_RESPONSE, packet.contents(), packet.size());
}

int GatewayClient::HandleIncoming(uint16 opcode, ByteBuffer& body)
{
    switch (opcode)
    {
        case Skyfire::Cluster::WIRE_MSG_VERIFY_CONNECTIVITY:
            return SendAuthChallenge();
        case Skyfire::Cluster::WIRE_CMSG_AUTH_SESSION:
            return HandleAuthSession(body);
        case Skyfire::Cluster::WIRE_CMSG_PING:
            return HandlePing(body);
        case Skyfire::Cluster::WIRE_CMSG_LOG_DISCONNECT:
        {
            uint32 reason = 0;
            if (body.size() >= body.rpos() + sizeof(uint32))
                body >> reason;
            SF_LOG_ERROR("server.clustergateway", "Client %s account %u sent CMSG_LOG_DISCONNECT reason %u.",
                _address.c_str(), _accountId, reason);
            return 0;
        }
        case Skyfire::Cluster::WIRE_CMSG_KEEP_ALIVE:
        case Skyfire::Cluster::WIRE_CMSG_ENABLE_NAGLE:
            return 0;
        default:
            if (_worldLink && !_worldLink->IsClosed())
            {
                if (body.size() > body.rpos())
                    _worldLink->SendWowOpcode(opcode, body.contents() + body.rpos(), body.size() - body.rpos());
                else
                    _worldLink->SendWowOpcode(opcode, NULL, 0);
                return 0;
            }
            SF_LOG_ERROR("server.clustergateway", "Unhandled opcode 0x%04X from %s before world attach.", opcode, _address.c_str());
            return -1;
    }
}

int GatewayClient::HandlePing(ByteBuffer& body)
{
    uint32 ping = 0;
    uint32 latency = 0;
    body >> latency >> ping;
    ByteBuffer pong;
    pong << ping;
    return SendWire(Skyfire::Cluster::WIRE_SMSG_PONG, pong.contents(), pong.size());
}

int GatewayClient::HandleAuthSession(ByteBuffer& body)
{
    SkyFire::Crypto::SHA1::Digest digest = {};
    std::array<uint8, 4> clientSeed = {};
    uint32 virtualRealmId = 0;
    uint16 clientBuild = 0;
    uint32 addonSize = 0;

    body.read_skip<uint32>();
    body.read_skip<uint32>();
    body >> digest[18];
    body >> digest[14];
    body >> digest[3];
    body >> digest[4];
    body >> digest[0];
    body >> virtualRealmId;
    body >> digest[11];
    body.read(clientSeed);
    body >> digest[19];
    body.read_skip<uint8>();
    body.read_skip<uint8>();
    body >> digest[2];
    body >> digest[9];
    body >> digest[12];
    body.read_skip<uint64>();
    body.read_skip<uint32>();
    body >> digest[16];
    body >> digest[5];
    body >> digest[6];
    body >> digest[8];
    body >> clientBuild;
    body >> digest[17];
    body >> digest[7];
    body >> digest[13];
    body >> digest[15];
    body >> digest[1];
    body >> digest[10];
    body >> addonSize;
    if (addonSize > 0xFFFFF || body.rpos() + addonSize > body.size())
    {
        SF_LOG_ERROR("server.clustergateway", "Auth session addon block rejected from %s size %u remaining %zu.",
            _address.c_str(), addonSize, body.size() - body.rpos());
        return -1;
    }
    _addonInfo.clear();
    if (addonSize)
    {
        _addonInfo.resize(addonSize);
        body.read(_addonInfo.contents(), addonSize);
    }
    body.ReadBit();
    uint32 accountNameLength = body.ReadBits(11);
    std::string account = body.ReadString(accountNameLength);

    Skyfire::Auth::LoginIdentity const identity = Skyfire::Auth::NormalizeLoginIdentity(account);
    PreparedStatement* stmt = NULL;
    if (identity.Kind == Skyfire::Auth::LoginIdentityKind::Email)
    {
        stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_INFO_BY_LOGIN_IDENTITY);
        stmt->setString(0, identity.Canonical);
        stmt->setString(1, identity.Canonical);
        stmt->setString(2, identity.Canonical);
    }
    else
    {
        stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_INFO_BY_NAME);
        stmt->setString(0, account);
    }

    PreparedQueryResult result = LoginDatabase.Query(stmt);
    if (!result)
    {
        SendAuthError(21); // AUTH_UNKNOWN_ACCOUNT
        return -1;
    }

    Field* fields = result->Fetch();
    uint32 id = fields[0].GetUInt32();
    SessionKey sessionKey = fields[1].GetBinary<SESSION_KEY_LENGTH>();
    uint8 expansion = fields[4].GetUInt8();
    uint8 locale = fields[6].GetUInt8();
    uint32 recruiter = fields[7].GetUInt32();
    bool hasBoost = fields[9].GetBool();

    uint8 t[4] = { 0, 0, 0, 0 };
    SkyFire::Crypto::SHA1 sha;
    sha.UpdateData(account);
    sha.UpdateData(t, 4);
    sha.UpdateData(clientSeed);
    sha.UpdateData(_seed);
    sha.UpdateData(sessionKey);
    sha.Finalize();
    if (sha.GetDigest() != digest)
    {
        SendAuthError(13); // AUTH_FAILED
        SF_LOG_ERROR("server.clustergateway", "Auth digest mismatch for account %u from %s.", id, _address.c_str());
        return -1;
    }

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_GMLEVEL_BY_REALMID);
    stmt->setUInt32(0, id);
    stmt->setInt32(1, int32(virtualRealmId));
    uint8 security = 0;
    if (PreparedQueryResult gm = LoginDatabase.Query(stmt))
        security = gm->Fetch()[0].GetUInt8();

    _accountId = id;
    _sessionKey = sessionKey;
    _accountName = account;
    _expansion = expansion;
    _locale = locale;
    _security = security;
    _recruiter = recruiter;
    _hasBoost = hasBoost;
    _usedEmailLogin = identity.Kind == Skyfire::Auth::LoginIdentityKind::Email;
    _virtualRealmId = virtualRealmId;
    _crypt.Init(sessionKey);

    if (!OpenWorldLink(id, virtualRealmId))
        return -1;

    SF_LOG_INFO("server.clustergateway", "Account %u attached from %s build %u.", id, _address.c_str(), clientBuild);
    return 0;
}

bool GatewayClient::LookupWorld(uint32 accountId, uint32 mapId, std::string& host, uint16& port)
{
    uint32 realmId = uint32(sConfigMgr->GetIntDefault("Cluster.RealmID", 1));
    uint32 stale = uint32(sConfigMgr->GetIntDefault("Cluster.StaleSeconds", 30));
    uint32 routedMap = mapId;

    // mapId 0 is char-select ("unknown"), not Eastern Kingdoms. A handoff passes the destination.
    PreparedStatement* route = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_ACCOUNT_ROUTE_LATEST);
    route->setUInt32(0, accountId);
    route->setUInt32(1, realmId);
    if (PreparedQueryResult routed = LoginDatabase.Query(route))
    {
        Field* fields = routed->Fetch();
        uint32 routeMap = fields[0].GetUInt32();
        uint32 routeNode = fields[1].GetUInt32();
        bool useRoute = !mapId || routeMap == mapId;
        if (!mapId)
            routedMap = routeMap;
        if (useRoute && routeNode)
        {
            PreparedStatement* node = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_NODE_ENDPOINT);
            node->setUInt32(0, realmId);
            node->setUInt32(1, routeNode);
            node->setUInt32(2, stale);
            if (PreparedQueryResult endpoint = LoginDatabase.Query(node))
            {
                host = endpoint->Fetch()[0].GetString();
                port = endpoint->Fetch()[1].GetUInt16();
            }
        }
    }

    if (host.empty() && routedMap)
    {
        PreparedStatement* assigned = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_MAP_OWNER_ENDPOINT);
        assigned->setUInt32(0, realmId);
        assigned->setUInt32(1, routedMap);
        assigned->setUInt32(2, stale);
        if (PreparedQueryResult endpoint = LoginDatabase.Query(assigned))
        {
            host = endpoint->Fetch()[1].GetString();
            port = endpoint->Fetch()[2].GetUInt16();
        }
    }

    // Char-select / first attach: no saved route. Map-sharded nodes never have
    // available_maps='', so pick the map-0 owner, then any live node.
    if (host.empty() && !routedMap)
    {
        PreparedStatement* assigned = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_MAP_OWNER_ENDPOINT);
        assigned->setUInt32(0, realmId);
        assigned->setUInt32(1, 0);
        assigned->setUInt32(2, stale);
        if (PreparedQueryResult endpoint = LoginDatabase.Query(assigned))
        {
            host = endpoint->Fetch()[1].GetString();
            port = endpoint->Fetch()[2].GetUInt16();
        }
    }

    if (host.empty())
    {
        PreparedStatement* wildcard = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_WILDCARD_ENDPOINT);
        wildcard->setUInt32(0, realmId);
        wildcard->setUInt32(1, stale);
        if (PreparedQueryResult endpoint = LoginDatabase.Query(wildcard))
        {
            host = endpoint->Fetch()[1].GetString();
            port = endpoint->Fetch()[2].GetUInt16();
        }
    }

    if (host.empty())
    {
        PreparedStatement* live = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_ANY_LIVE_ENDPOINT);
        live->setUInt32(0, realmId);
        live->setUInt32(1, stale);
        if (PreparedQueryResult endpoint = LoginDatabase.Query(live))
        {
            host = endpoint->Fetch()[1].GetString();
            port = endpoint->Fetch()[2].GetUInt16();
        }
    }

    return !host.empty();
}

void GatewayClient::BindWorldHandlers()
{
    auto self = shared_from_this();
    _worldLink->SetWowHandler([self](uint16 wire, ByteBuffer body)
    {
        self->SendWire(wire, body.empty() ? NULL : body.contents(), body.size());
    });
    _worldLink->SetHandoffHandler([self](Skyfire::Cluster::HandoffPayload const& handoff)
    {
        self->HandleHandoff(handoff);
    });
    _worldLink->SetCloseHandler([self]()
    {
        self->Close();
    });
    _worldLink->StartRead();
}

bool GatewayClient::ConnectWorld(std::string const& host, uint16 port, Skyfire::Cluster::HandoffPayload const* resume)
{
    uint32 realmId = uint32(sConfigMgr->GetIntDefault("Cluster.RealmID", 1));
    _worldLink.reset(new ClusterLinkClient());
    if (!_worldLink->Connect(host, port))
        return false;

    std::string token = sConfigMgr->GetStringDefault("Cluster.SharedToken", "");
    if (!_worldLink->Hello(realmId, token))
        return false;

    bool ok = false;
    if (resume)
    {
        ok = _worldLink->Resume(_accountId, _accountName, _address, _expansion, _locale, _security, _recruiter,
            _hasBoost, _usedEmailLogin, _virtualRealmId, _sessionKey, *resume);
    }
    else
    {
        ok = _worldLink->Attach(_accountId, _accountName, _address, _expansion, _locale, _security, _recruiter,
            _hasBoost, _usedEmailLogin, _virtualRealmId, _sessionKey, _addonInfo);
    }

    if (!ok)
    {
        SF_LOG_ERROR("server.clustergateway", "ClusterLink handshake failed for account %u on %s:%u.", _accountId, host.c_str(), port);
        return false;
    }

    BindWorldHandlers();
    return true;
}

void GatewayClient::SendTransferPackets(Skyfire::Cluster::HandoffPayload const& handoff)
{
    if (handoff.Flags & Skyfire::Cluster::HANDOFF_FLAG_LOGIN)
        return;

    ByteBuffer pending;
    pending.WriteBit(0);
    pending.WriteBit(0);
    pending << uint32(handoff.DestMap);
    SendWire(Skyfire::Cluster::WIRE_SMSG_TRANSFER_PENDING, pending.contents(), pending.size());

    ByteBuffer world;
    world << float(handoff.X);
    world << uint32(handoff.DestMap);
    world << float(handoff.Y);
    world << float(handoff.Z);
    world << float(handoff.O);
    SendWire(Skyfire::Cluster::WIRE_SMSG_NEW_WORLD, world.contents(), world.size());
}

void GatewayClient::HandleHandoff(Skyfire::Cluster::HandoffPayload const& handoff)
{
    auto self = shared_from_this();
    Skyfire::Cluster::HandoffPayload copy = handoff;
    boost::asio::post(_ioContext, [self, copy]()
    {
        std::string host;
        uint16 port = 0;
        if (!self->LookupWorld(self->_accountId, copy.DestMap, host, port))
        {
            SF_LOG_ERROR("server.clustergateway", "Handoff for account %u map %u has no owner.", self->_accountId, copy.DestMap);
            self->Close();
            return;
        }

        self->SendTransferPackets(copy);
        std::shared_ptr<ClusterLinkClient> old = self->_worldLink;
        self->_worldLink.reset();
        if (!self->ConnectWorld(host, port, &copy))
        {
            self->Close();
            return;
        }
        if (old)
            old->Close();
    });
}

bool GatewayClient::OpenWorldLink(uint32 accountId, uint32 virtualRealmId)
{
    uint32 realmId = uint32(sConfigMgr->GetIntDefault("Cluster.RealmID", 1));
    if (!virtualRealmId)
        virtualRealmId = realmId;
    _virtualRealmId = virtualRealmId;

    std::string host;
    uint16 port = 0;
    if (!LookupWorld(accountId, 0, host, port))
    {
        SF_LOG_ERROR("server.clustergateway", "No live cluster node for account %u realm %u.", accountId, realmId);
        return false;
    }

    return ConnectWorld(host, port, NULL);
}
