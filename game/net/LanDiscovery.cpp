#include "net/LanDiscovery.h"

#include "core/Log.h"
#include "net/Protocol.h"

#include <enet/enet.h>

#include <algorithm>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace origon::net {
namespace {

constexpr uint32_t kMagic = 0x4F524947; // "ORIG"

} // namespace

LanDiscovery::~LanDiscovery() { stop(); }

void LanDiscovery::stop() {
    if (m_sender >= 0) enet_socket_destroy(static_cast<ENetSocket>(m_sender));
    if (m_listener >= 0) enet_socket_destroy(static_cast<ENetSocket>(m_listener));
    m_sender = m_listener = -1;
    m_servers.clear();
    if (m_enetReady) {
        enet_deinitialize();
        m_enetReady = false;
    }
}

bool LanDiscovery::ensureSender() {
    if (m_sender >= 0) return true;
    if (!m_enetReady) m_enetReady = enet_initialize() == 0;
    const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL) return false;
    enet_socket_set_option(socket, ENET_SOCKOPT_BROADCAST, 1);
    enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
    m_sender = static_cast<int>(socket);
    return true;
}

bool LanDiscovery::ensureListener() {
    if (m_listener >= 0) return true;
    if (!m_enetReady) m_enetReady = enet_initialize() == 0;
    const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL) return false;
    enet_socket_set_option(socket, ENET_SOCKOPT_REUSEADDR, 1);
    enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = kDiscoveryPort;
    if (enet_socket_bind(socket, &address) != 0) {
        enet_socket_destroy(socket);
        log::warn("LAN discovery: cannot bind port {}", kDiscoveryPort);
        return false;
    }
    m_listener = static_cast<int>(socket);
    return true;
}

void LanDiscovery::announce(double now, uint16_t gamePort, const std::string& name, const std::string& map, int players,
                            int maxPlayers) {
    if (now < m_nextAnnounce || !ensureSender()) return;
    m_nextAnnounce = now + 1.0;
    Writer w;
    w.put(kMagic);
    w.put(kProtocolVersion);
    w.put(gamePort);
    w.putString(name.substr(0, 32));
    w.putString(map.substr(0, 32));
    w.put(static_cast<uint8_t>(players));
    w.put(static_cast<uint8_t>(maxPlayers));
    ENetBuffer buffer{};
    buffer.data = w.data.data();
    buffer.dataLength = w.data.size();
    auto sendTo = [&](uint32_t host) {
        ENetAddress target{};
        target.host = host;
        target.port = kDiscoveryPort;
        enet_socket_send(static_cast<ENetSocket>(m_sender), &target, &buffer, 1);
    };
    // Limited broadcast, every interface's directed broadcast (multi-homed machines),
    // and loopback for a second instance on the same computer.
    sendTo(ENET_HOST_BROADCAST);
#if !defined(_WIN32)
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0) {
        for (ifaddrs* it = interfaces; it; it = it->ifa_next) {
            if (it->ifa_addr && it->ifa_addr->sa_family == AF_INET && (it->ifa_flags & IFF_BROADCAST) && it->ifa_broadaddr) {
                sendTo(reinterpret_cast<sockaddr_in*>(it->ifa_broadaddr)->sin_addr.s_addr);
            }
        }
        freeifaddrs(interfaces);
    }
#endif
    sendTo(htonl(0x7F000001u));
}

void LanDiscovery::listen(double now) {
    if (!ensureListener()) return;
    uint8_t data[512];
    for (int i = 0; i < 32; ++i) {
        ENetAddress from{};
        ENetBuffer buffer{};
        buffer.data = data;
        buffer.dataLength = sizeof(data);
        const int received = enet_socket_receive(static_cast<ENetSocket>(m_listener), &from, &buffer, 1);
        if (received <= 0) break;
        Reader r(data, static_cast<size_t>(received));
        if (r.get<uint32_t>() != kMagic || r.get<uint32_t>() != kProtocolVersion) continue;
        Server server;
        server.port = r.get<uint16_t>();
        server.name = r.getString();
        server.map = r.getString();
        server.players = r.get<uint8_t>();
        server.maxPlayers = r.get<uint8_t>();
        if (!r.ok()) continue;
        char host[64] = {};
        enet_address_get_host_ip(&from, host, sizeof(host));
        server.address = host;
        server.lastSeen = now;
        auto it = std::find_if(m_servers.begin(), m_servers.end(),
                               [&](const Server& s) { return s.address == server.address && s.port == server.port; });
        if (it != m_servers.end()) *it = server;
        else m_servers.push_back(server);
    }
    std::erase_if(m_servers, [now](const Server& s) { return now - s.lastSeen > 4.0; });
}

} // namespace origon::net
