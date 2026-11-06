/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "ClusterAdminHttp.h"
#include "Common.h"
#include "Configuration/Config.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Network/BoostAsioUtils.h"
#include "SystemConfig.h"
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <atomic>
#include <map>
#include <set>
#include <sstream>
#include <vector>

std::atomic<bool> g_clusterAdminCharacterDb{false};

void ClusterAdminHttpSetCharacterDatabase(bool ready)
{
    g_clusterAdminCharacterDb = ready;
}

using boost::asio::ip::tcp;

namespace
{
std::string JsonEscape(std::string const& in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        switch (c)
        {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

void ParseCsvUints(std::string const& csv, std::vector<uint32>& out)
{
    std::string token;
    std::istringstream ss(csv);
    while (std::getline(ss, token, ','))
    {
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
            token.erase(token.begin());
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
            token.pop_back();
        if (token.empty())
            continue;
        char* end = nullptr;
        unsigned long value = std::strtoul(token.c_str(), &end, 10);
        if (end != token.c_str() && *end == '\0')
            out.push_back(uint32(value));
    }
}

std::string JoinMapCsv(std::set<uint32> const& maps)
{
    if (maps.empty())
        return "-";
    std::ostringstream csv;
    bool first = true;
    for (uint32 mapId : maps)
    {
        if (!first)
            csv << ',';
        first = false;
        csv << mapId;
    }
    return csv.str();
}

std::string ExtractJsonString(std::string const& json, std::string const& field)
{
    std::string needle = "\"" + field + "\"";
    std::size_t key = json.find(needle);
    if (key == std::string::npos)
        return {};
    std::size_t colon = json.find(':', key + needle.size());
    if (colon == std::string::npos)
        return {};
    std::size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos)
        return {};
    std::string value;
    for (std::size_t i = q1 + 1; i < json.size(); ++i)
    {
        if (json[i] == '\\' && i + 1 < json.size())
        {
            value.push_back(json[++i]);
            continue;
        }
        if (json[i] == '"')
            break;
        value.push_back(json[i]);
    }
    return value;
}

bool HasJsonField(std::string const& json, std::string const& field)
{
    return json.find("\"" + field + "\"") != std::string::npos;
}

uint32 ExtractJsonUint(std::string const& json, std::string const& field, uint32 def)
{
    std::string needle = "\"" + field + "\"";
    std::size_t key = json.find(needle);
    if (key == std::string::npos)
        return def;
    std::size_t colon = json.find(':', key + needle.size());
    if (colon == std::string::npos)
        return def;
    return uint32(std::strtoul(json.c_str() + colon + 1, nullptr, 10));
}

struct AdminNode
{
    uint32 Id = 0;
    std::string Host;
    std::string MapsText;
    std::set<uint32> Maps;
    uint8 State = 0;
    uint32 Heartbeat = 0;
    bool Online = false;
    bool Wildcard = false;
};

std::vector<AdminNode> LoadAdminNodes(uint32 realmId, uint32 stale)
{
    std::vector<AdminNode> nodes;
    time_t now = time(nullptr);
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_NODES_FOR_REALM);
    stmt->setUInt32(0, realmId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);
    if (!result)
        return nodes;

    do
    {
        Field* fields = result->Fetch();
        AdminNode node;
        node.Id = fields[0].GetUInt32();
        node.Host = fields[1].GetString();
        node.MapsText = fields[3].GetString();
        node.Wildcard = node.MapsText.empty();
        std::vector<uint32> maps;
        ParseCsvUints(node.MapsText, maps);
        node.Maps.insert(maps.begin(), maps.end());
        node.State = fields[4].GetUInt8();
        node.Heartbeat = fields[5].GetUInt32();
        node.Online = node.State == 2 && node.Heartbeat + stale >= uint32(now);
        nodes.push_back(node);
    } while (result->NextRow());

    PreparedStatement* assigned = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_MAPS_FOR_REALM);
    assigned->setUInt32(0, realmId);
    if (PreparedQueryResult maps = LoginDatabase.Query(assigned))
    {
        do
        {
            Field* fields = maps->Fetch();
            uint32 mapId = fields[0].GetUInt32();
            uint32 nodeId = fields[1].GetUInt32();
            for (AdminNode& node : nodes)
            {
                if (node.Id == nodeId)
                    node.Maps.insert(mapId);
            }
        } while (maps->NextRow());
    }
    return nodes;
}

bool NodeMatches(AdminNode const& node, std::string const& needle, uint32 id)
{
    if (id && node.Id == id)
        return true;
    if (needle.empty())
        return false;
    if (node.Host == needle)
        return true;
    std::string prefixed = "sf-world-" + needle;
    if (node.Host == prefixed)
        return true;
    std::string suffix = node.Host;
    std::string const prefix = "sf-world-";
    if (suffix.rfind(prefix, 0) == 0 && suffix.substr(prefix.size()) == needle)
        return true;
    return std::to_string(node.Id) == needle;
}

AdminNode const* FindAdminNode(std::vector<AdminNode> const& nodes, std::string const& name, uint32 id)
{
    for (AdminNode const& node : nodes)
        if (NodeMatches(node, name, id))
            return &node;
    return nullptr;
}

void WriteNodeMaps(uint32 realmId, uint32 nodeId, std::set<uint32> const& maps)
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_CLUSTER_NODE_MAPS);
    stmt->setString(0, JoinMapCsv(maps));
    stmt->setUInt32(1, realmId);
    stmt->setUInt32(2, nodeId);
    LoginDatabase.DirectExecute(stmt);
}

void ClaimMap(uint32 realmId, uint32 mapId, uint32 nodeId)
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_CLUSTER_MAP);
    stmt->setUInt32(0, realmId);
    stmt->setUInt32(1, mapId);
    stmt->setUInt32(2, nodeId);
    LoginDatabase.DirectExecute(stmt);
}

std::string AssignMapToNode(uint32 realmId, uint32 stale, uint32 mapId, std::string const& destName, uint32 destId)
{
    std::vector<AdminNode> nodes = LoadAdminNodes(realmId, stale);
    AdminNode const* dest = FindAdminNode(nodes, destName, destId);
    if (!dest)
        return "{\"error\":\"destination node not found\"}";
    if (!dest->Online)
        return "{\"error\":\"destination node is offline\"}";
    if (dest->Wildcard)
        return "{\"error\":\"cannot assign onto a wildcard (all-maps) node\"}";

    AdminNode const* source = nullptr;
    for (AdminNode const& node : nodes)
        if (node.Maps.count(mapId))
            source = &node;

    if (source && source->Wildcard)
        return "{\"error\":\"cannot peel a map off a wildcard (all-maps) node\"}";
    if (source && source->Id == dest->Id)
        return "{\"error\":\"map already owned by destination\"}";

    std::set<uint32> destMaps = dest->Maps;
    destMaps.insert(mapId);
    WriteNodeMaps(realmId, dest->Id, destMaps);
    if (source)
    {
        std::set<uint32> sourceMaps = source->Maps;
        sourceMaps.erase(mapId);
        WriteNodeMaps(realmId, source->Id, sourceMaps);
    }
    ClaimMap(realmId, mapId, dest->Id);

    std::ostringstream json;
    json << "{\"ok\":true,\"op\":\"split\",\"mapId\":" << mapId
         << ",\"fromNodeId\":" << (source ? source->Id : 0)
         << ",\"toNodeId\":" << dest->Id << '}';
    return json.str();
}

std::string MergeNodeMaps(uint32 realmId, uint32 stale, std::string const& sourceName, uint32 sourceId,
    std::string const& destName, uint32 destId)
{
    std::vector<AdminNode> nodes = LoadAdminNodes(realmId, stale);
    AdminNode const* source = FindAdminNode(nodes, sourceName, sourceId);
    AdminNode const* dest = FindAdminNode(nodes, destName, destId);
    if (!source)
        return "{\"error\":\"source node not found\"}";
    if (!dest)
        return "{\"error\":\"destination node not found\"}";
    if (source->Id == dest->Id)
        return "{\"error\":\"source and destination are the same node\"}";
    if (!dest->Online)
        return "{\"error\":\"destination node is offline\"}";
    if (source->Wildcard || dest->Wildcard)
        return "{\"error\":\"cannot merge wildcard (all-maps) nodes\"}";
    if (source->Maps.empty())
        return "{\"error\":\"source node has no maps to merge\"}";

    std::set<uint32> destMaps = dest->Maps;
    std::set<uint32> moved = source->Maps;
    destMaps.insert(moved.begin(), moved.end());
    WriteNodeMaps(realmId, dest->Id, destMaps);
    WriteNodeMaps(realmId, source->Id, std::set<uint32>());
    for (uint32 mapId : moved)
        ClaimMap(realmId, mapId, dest->Id);

    std::ostringstream json;
    json << "{\"ok\":true,\"op\":\"merge\",\"fromNodeId\":" << source->Id
         << ",\"toNodeId\":" << dest->Id << ",\"maps\":[";
    bool first = true;
    for (uint32 mapId : moved)
    {
        if (!first)
            json << ',';
        first = false;
        json << mapId;
    }
    json << "]}";
    return json.str();
}

class ClusterAdminHttpSession : public std::enable_shared_from_this<ClusterAdminHttpSession>
{
public:
    explicit ClusterAdminHttpSession(tcp::socket&& socket) : _socket(std::move(socket)) { }

    void Start()
    {
        boost::system::error_code error;
        std::size_t n = boost::asio::read_until(_socket, _buffer, "\r\n\r\n", error);
        if (error || n == 0)
            return;

        std::istream input(&_buffer);
        std::string requestLine;
        if (!std::getline(input, requestLine))
            return;
        if (!requestLine.empty() && requestLine.back() == '\r')
            requestLine.pop_back();

        std::size_t sp1 = requestLine.find(' ');
        std::size_t sp2 = requestLine.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos)
        {
            Send(400, "Bad Request", "{\"error\":\"bad request line\"}");
            return;
        }

        std::string method = requestLine.substr(0, sp1);
        std::string path = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
        std::size_t q = path.find('?');
        if (q != std::string::npos)
            path.resize(q);

        std::size_t contentLength = 0;
        std::string headerLine;
        while (std::getline(input, headerLine))
        {
            if (!headerLine.empty() && headerLine.back() == '\r')
                headerLine.pop_back();
            if (headerLine.empty())
                break;
            std::size_t colon = headerLine.find(':');
            if (colon == std::string::npos)
                continue;
            std::string name = headerLine.substr(0, colon);
            std::string value = headerLine.substr(colon + 1);
            while (!value.empty() && value.front() == ' ')
                value.erase(value.begin());
            for (char& c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name == "content-length")
                contentLength = std::strtoul(value.c_str(), nullptr, 10);
        }

        if (contentLength > 64 * 1024)
        {
            Send(413, "Payload Too Large", "{\"error\":\"body too large\"}");
            return;
        }

        std::string body;
        if (contentLength > 0)
        {
            std::size_t already = _buffer.size();
            if (already < contentLength)
            {
                boost::asio::read(_socket, _buffer, boost::asio::transfer_exactly(contentLength - already), error);
                if (error)
                    return;
            }
            body.resize(contentLength);
            input.read(body.data(), static_cast<std::streamsize>(contentLength));
        }

        Handle(method, path, body);
    }

private:
    uint32 RealmId() const
    {
        return uint32(sConfigMgr->GetIntDefault("Cluster.RealmID", 1));
    }

    uint32 StaleSeconds() const
    {
        int32 stale = sConfigMgr->GetIntDefault("Cluster.StaleSeconds", 30);
        if (stale < 5)
            stale = 5;
        return uint32(stale);
    }

    void Handle(std::string const& method, std::string const& path, std::string const& body)
    {
        if (method == "OPTIONS")
        {
            Send(204, "No Content", "");
            return;
        }

        if (method == "GET" && path == "/health")
        {
            std::ostringstream json;
            json << "{\"status\":\"ok\",\"service\":\"clustergateway\",\"version\":\""
                 << JsonEscape(SKYFIRE_VER_PRODUCTVERSION_STR) << "\"}";
            Send(200, "OK", json.str());
            return;
        }

        if (method == "POST" && path == "/v1/reload")
        {
            Send(200, "OK", "{\"ok\":true}");
            return;
        }

        if (method == "GET" && path == "/v1/nodes")
        {
            Send(200, "OK", BuildNodesJson());
            return;
        }

        if (method == "GET" && path == "/v1/maps")
        {
            Send(200, "OK", BuildMapsJson());
            return;
        }

        if (method == "GET" && path == "/v1/routes")
        {
            Send(200, "OK", BuildRoutesJson());
            return;
        }

        if (method == "GET" && path == "/v1/players")
        {
            Send(200, "OK", BuildPlayersJson());
            return;
        }

        if (method == "POST" && (path == "/v1/maps/split" || path == "/v1/maps/assign"))
        {
            if (!HasJsonField(body, "mapId"))
            {
                Send(400, "Bad Request", "{\"error\":\"mapId required\"}");
                return;
            }
            uint32 mapId = ExtractJsonUint(body, "mapId", 0);
            std::string destName = ExtractJsonString(body, "destNodeName");
            if (destName.empty())
                destName = ExtractJsonString(body, "nodeName");
            uint32 destId = ExtractJsonUint(body, "destNodeId", 0);
            if (destName.empty() && destId == 0)
            {
                Send(400, "Bad Request", "{\"error\":\"destNodeId or destNodeName required\"}");
                return;
            }
            std::string json = AssignMapToNode(RealmId(), StaleSeconds(), mapId, destName, destId);
            Send(json.find("\"ok\":true") != std::string::npos ? 200 : 400, json.find("\"ok\":true") != std::string::npos ? "OK" : "Bad Request", json);
            return;
        }

        if (method == "POST" && path == "/v1/maps/merge")
        {
            std::string sourceName = ExtractJsonString(body, "sourceNodeName");
            std::string destName = ExtractJsonString(body, "destNodeName");
            uint32 sourceId = ExtractJsonUint(body, "sourceNodeId", 0);
            uint32 destId = ExtractJsonUint(body, "destNodeId", 0);
            if ((sourceName.empty() && sourceId == 0) || (destName.empty() && destId == 0))
            {
                Send(400, "Bad Request", "{\"error\":\"source and destination nodes required\"}");
                return;
            }
            std::string json = MergeNodeMaps(RealmId(), StaleSeconds(), sourceName, sourceId, destName, destId);
            Send(json.find("\"ok\":true") != std::string::npos ? 200 : 400, json.find("\"ok\":true") != std::string::npos ? "OK" : "Bad Request", json);
            return;
        }

        Send(404, "Not Found", "{\"error\":\"not found\"}");
    }

    std::string BuildNodesJson()
    {
        uint32 realmId = RealmId();
        uint32 stale = StaleSeconds();
        time_t now = time(nullptr);

        PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_NODES_FOR_REALM);
        stmt->setUInt32(0, realmId);
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        std::ostringstream json;
        json << '[';
        bool first = true;
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 id = fields[0].GetUInt32();
                std::string host = fields[1].GetString();
                uint16 port = fields[2].GetUInt16();
                uint8 state = fields[4].GetUInt8();
                uint32 heartbeat = fields[5].GetUInt32();
                bool online = state == 2 && heartbeat + stale >= uint32(now);

                if (!first)
                    json << ',';
                first = false;
                json << "{\"id\":" << id
                     << ",\"name\":\"" << JsonEscape(host)
                     << "\",\"address\":\"" << JsonEscape(host)
                     << "\",\"port\":" << port
                     << ",\"restAdminUrl\":\"\",\"state\":" << uint32(state)
                     << ",\"heartbeat\":" << heartbeat
                     << ",\"online\":" << (online ? "true" : "false")
                     << '}';
            } while (result->NextRow());
        }
        json << ']';
        return json.str();
    }

    std::string BuildMapsJson()
    {
        uint32 realmId = RealmId();
        std::map<uint32, std::pair<uint32, std::string>> maps;

        PreparedStatement* assigned = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_MAPS_FOR_REALM);
        assigned->setUInt32(0, realmId);
        if (PreparedQueryResult result = LoginDatabase.Query(assigned))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 mapId = fields[0].GetUInt32();
                uint32 nodeId = fields[1].GetUInt32();
                maps[mapId] = std::make_pair(nodeId, fields[2].GetString());
            } while (result->NextRow());
        }

        PreparedStatement* nodes = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_NODES_FOR_REALM);
        nodes->setUInt32(0, realmId);
        if (PreparedQueryResult result = LoginDatabase.Query(nodes))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 nodeId = fields[0].GetUInt32();
                std::string host = fields[1].GetString();
                std::vector<uint32> claimed;
                ParseCsvUints(fields[3].GetString(), claimed);
                for (uint32 mapId : claimed)
                {
                    if (maps.find(mapId) == maps.end())
                        maps[mapId] = std::make_pair(nodeId, host);
                }
            } while (result->NextRow());
        }

        std::ostringstream json;
        json << '[';
        bool first = true;
        for (auto const& entry : maps)
        {
            if (!first)
                json << ',';
            first = false;
            json << "{\"mapId\":" << entry.first
                 << ",\"nodeId\":" << entry.second.first
                 << ",\"nodeName\":\"" << JsonEscape(entry.second.second) << "\"}";
        }
        json << ']';
        return json.str();
    }

    std::string BuildRoutesJson()
    {
        PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CLUSTER_ROUTES_FOR_REALM);
        stmt->setUInt32(0, RealmId());
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        std::ostringstream json;
        json << '[';
        bool first = true;
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                if (!first)
                    json << ',';
                first = false;
                json << "{\"accountId\":" << fields[0].GetUInt32()
                     << ",\"username\":\"" << JsonEscape(fields[1].GetString())
                     << "\",\"mapId\":" << fields[2].GetUInt32()
                     << ",\"nodeId\":" << fields[3].GetUInt32()
                     << ",\"updatedAt\":" << fields[4].GetUInt32()
                     << ",\"gmLevel\":" << fields[5].GetUInt32()
                     << '}';
            } while (result->NextRow());
        }
        json << ']';
        return json.str();
    }

    std::string BuildPlayersJson()
    {
        if (!g_clusterAdminCharacterDb)
            return "[]";

        uint32 realmId = RealmId();
        char sql[192];
        snprintf(sql, sizeof(sql),
            "SELECT map, COUNT(*) FROM characters WHERE online = 1 AND realm = %u GROUP BY map", realmId);
        QueryResult result = CharacterDatabase.Query(sql);

        std::ostringstream json;
        json << '[';
        bool first = true;
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                if (!first)
                    json << ',';
                first = false;
                json << "{\"mapId\":" << uint32(fields[0].GetUInt16())
                     << ",\"count\":" << fields[1].GetUInt64() << '}';
            } while (result->NextRow());
        }
        json << ']';
        return json.str();
    }

    void Send(int status, char const* statusText, std::string const& body)
    {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << ' ' << statusText
                 << "\r\nContent-Type: application/json; charset=utf-8"
                 << "\r\nContent-Length: " << body.size()
                 << "\r\nConnection: close"
                 << "\r\nAccess-Control-Allow-Origin: *"
                 << "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS"
                 << "\r\nAccess-Control-Allow-Headers: Content-Type"
                 << "\r\n\r\n" << body;
        std::string payload = response.str();
        boost::system::error_code error;
        boost::asio::write(_socket, boost::asio::buffer(payload), error);
        Skyfire::Net::CloseTcpSocket(_socket);
    }

    tcp::socket _socket;
    boost::asio::streambuf _buffer;
};
}

ClusterAdminHttp::ClusterAdminHttp() :
    _threadGroup(),
    _acceptor(_threadGroup.GetIoContext()),
    _closed(true)
{
}

ClusterAdminHttp::~ClusterAdminHttp()
{
    Close();
}

bool ClusterAdminHttp::Open(uint16 port, std::string const& bindIp)
{
    if (!Skyfire::Net::OpenTcpAcceptor(_threadGroup.GetIoContext(), _acceptor, port, bindIp,
            "server.clustergateway", "cluster-admin-http"))
        return false;

    _closed = false;
    AsyncAccept();
    if (_threadGroup.Start(1) == -1)
    {
        Close();
        return false;
    }

    SF_LOG_INFO("server.clustergateway", "Admin HTTP listening on http://%s:%u", bindIp.c_str(), uint32(port));
    return true;
}

void ClusterAdminHttp::Close()
{
    bool expected = false;
    if (!_closed.compare_exchange_strong(expected, true))
        return;

    Skyfire::Net::CloseTcpAcceptor(_acceptor);
    _threadGroup.StopAndJoin();
}

void ClusterAdminHttp::AsyncAccept()
{
    if (!_acceptor.is_open())
        return;

    std::shared_ptr<tcp::socket> socket(new tcp::socket(_threadGroup.GetIoContext()));
    _acceptor.async_accept(*socket,
        [this, socket](boost::system::error_code const& error)
        {
            HandleAccept(socket, error);
        });
}

void ClusterAdminHttp::HandleAccept(std::shared_ptr<tcp::socket> socket, boost::system::error_code const& error)
{
    if (_closed)
        return;

    if (!error)
    {
        std::shared_ptr<ClusterAdminHttpSession> session(new ClusterAdminHttpSession(std::move(*socket)));
        session->Start();
    }
    else if (error != boost::asio::error::operation_aborted)
        SF_LOG_ERROR("server.clustergateway", "Failed to accept admin HTTP, error %d", error.value());

    AsyncAccept();
}
