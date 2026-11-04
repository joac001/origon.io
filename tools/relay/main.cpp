// origon_relay: public meeting point and packet relay for internet games.
// Usage: origon_relay [--port 27030] [--max-rooms 256]
//
// Run it on any machine with a public IP (a small VPS is plenty: it only forwards packets,
// it does not simulate). Hosts register a room and get a five-character code; players join
// with that code or pick the room from the in-game list. See game/net/RelayProtocol.h.
#include "core/Log.h"
#include "net/Protocol.h"
#include "net/RelayProtocol.h"

#include <enet/enet.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace origon;
using namespace origon::net;

namespace {

constexpr size_t kMaxPacketBytes = 4u * 1024u * 1024u; // Map JSON in Welcome can be a few hundred KB.
constexpr size_t kMaxNameLength = 32;

volatile std::sig_atomic_t g_running = 1;

struct Room {
    std::string code;
    ENetPeer* host = nullptr;
    std::string name;
    std::string map;
    uint8_t players = 0;
    uint8_t maxPlayers = 0;
    std::map<uint32_t, ENetPeer*> clients;
};

struct PeerState {
    enum class Kind { Unknown, Host, Client } kind = Kind::Unknown;
    std::string room;  // Room code (hosts and joined clients).
    uint32_t id = 0;   // Client id inside the room.
};

class Relay {
public:
    explicit Relay(size_t maxRooms) : m_maxRooms(maxRooms), m_rng(std::random_device{}()) {}

    bool start(uint16_t port) {
        ENetAddress address{};
        address.host = ENET_HOST_ANY;
        address.port = port;
        // Peers: every host plus every client of every room.
        const size_t peers = std::min<size_t>(m_maxRooms * (kRelayMaxClientsPerRoom + 1), 4095);
        m_host = enet_host_create(&address, peers, kChannelCount, 0, 0);
        if (!m_host) {
            log::error("Relay: cannot bind UDP port {}", port);
            return false;
        }
        m_host->maximumPacketSize = kMaxPacketBytes;
        log::info("Relay listening on UDP port {} (up to {} rooms)", port, m_maxRooms);
        return true;
    }

    ~Relay() {
        if (m_host) enet_host_destroy(m_host);
    }

    void run() {
        auto lastRtt = std::chrono::steady_clock::now();
        while (g_running) {
            ENetEvent event;
            if (enet_host_service(m_host, &event, 50) > 0) {
                do {
                    handle(event);
                } while (enet_host_check_events(m_host, &event) > 0);
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRtt > std::chrono::seconds(1)) {
                lastRtt = now;
                reportRtt();
            }
        }
        log::info("Relay: shutting down");
        for (auto& [peer, state] : m_peers) enet_peer_disconnect_now(peer, 0);
        enet_host_flush(m_host);
    }

private:
    void handle(ENetEvent& event) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            m_peers[event.peer] = PeerState{};
            enet_peer_timeout(event.peer, 0, 5000, 15000);
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            receive(event.peer, event.packet, event.channelID);
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            leave(event.peer);
            break;
        default:
            break;
        }
    }

    void sendTo(ENetPeer* peer, const Writer& writer, bool reliable = true) {
        ENetPacket* packet = enet_packet_create(writer.data.data(), writer.data.size(),
                                                reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
        enet_peer_send(peer, reliable ? kChannelReliable : kChannelUnreliable, packet);
    }

    void sendError(ENetPeer* peer, const std::string& reason) {
        Writer w;
        w.put(RelayMessage::Error);
        w.putString(reason);
        sendTo(peer, w);
        enet_peer_disconnect_later(peer, 0);
    }

    std::string newCode() {
        std::uniform_int_distribution<size_t> pick(0, std::char_traits<char>::length(kRelayCodeAlphabet) - 1);
        for (;;) {
            std::string code;
            for (int i = 0; i < kRelayCodeLength; ++i) code += kRelayCodeAlphabet[pick(m_rng)];
            if (!m_rooms.contains(code)) return code;
        }
    }

    void receive(ENetPeer* peer, ENetPacket* packet, uint8_t channel) {
        auto found = m_peers.find(peer);
        if (found == m_peers.end() || packet->dataLength == 0) return;
        PeerState& state = found->second;
        Reader reader(packet->data, packet->dataLength);
        const uint8_t type = reader.get<uint8_t>();

        // A joined client speaks the game protocol: wrap everything for its host.
        if (state.kind == PeerState::Kind::Client) {
            auto room = m_rooms.find(state.room);
            if (room == m_rooms.end()) return;
            Writer w;
            w.put(RelayMessage::Data);
            w.put(state.id);
            const bool reliable = (packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0 || channel == kChannelReliable;
            w.put(static_cast<uint8_t>(reliable ? 1 : 0));
            w.data.insert(w.data.end(), packet->data, packet->data + packet->dataLength);
            sendTo(room->second.host, w, reliable);
            return;
        }
        if (!isRelayMessage(type)) return;

        switch (static_cast<RelayMessage>(type)) {
        case RelayMessage::Register: {
            const auto version = reader.get<uint32_t>();
            std::string name = reader.getString().substr(0, kMaxNameLength);
            std::string map = reader.getString().substr(0, kMaxNameLength);
            const auto players = reader.get<uint8_t>();
            const auto maxPlayers = reader.get<uint8_t>();
            if (!reader.ok()) return;
            if (version != kRelayVersion) {
                sendError(peer, "Versión del servidor intermedio incompatible");
                return;
            }
            if (state.kind == PeerState::Kind::Host) { // Info update.
                Room& room = m_rooms[state.room];
                room.name = name;
                room.map = map;
                room.players = players;
                room.maxPlayers = maxPlayers;
                return;
            }
            if (state.kind != PeerState::Kind::Unknown) return;
            if (m_rooms.size() >= m_maxRooms) {
                sendError(peer, "El servidor intermedio está lleno");
                return;
            }
            Room room;
            room.code = newCode();
            room.host = peer;
            room.name = name;
            room.map = map;
            room.players = players;
            room.maxPlayers = maxPlayers;
            state.kind = PeerState::Kind::Host;
            state.room = room.code;
            Writer w;
            w.put(RelayMessage::Registered);
            w.putString(room.code);
            sendTo(peer, w);
            log::info("Relay: room {} '{}' opened ({} rooms)", room.code, room.name, m_rooms.size() + 1);
            m_rooms.emplace(room.code, std::move(room));
            break;
        }
        case RelayMessage::List: {
            Writer w;
            w.put(RelayMessage::Rooms);
            const auto count = static_cast<uint16_t>(std::min<size_t>(m_rooms.size(), 200));
            w.put(count);
            uint16_t written = 0;
            for (const auto& [code, room] : m_rooms) {
                if (written++ == count) break;
                w.putString(code);
                w.putString(room.name);
                w.putString(room.map);
                w.put(room.players);
                w.put(room.maxPlayers);
            }
            sendTo(peer, w);
            enet_peer_disconnect_later(peer, 0);
            break;
        }
        case RelayMessage::Join: {
            const auto version = reader.get<uint32_t>();
            std::string code = reader.getString().substr(0, 16);
            for (char& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!reader.ok() || state.kind != PeerState::Kind::Unknown) return;
            if (version != kRelayVersion) {
                sendError(peer, "Versión del servidor intermedio incompatible");
                return;
            }
            auto room = m_rooms.find(code);
            if (room == m_rooms.end()) {
                sendError(peer, "No existe una partida con ese código");
                return;
            }
            if (room->second.clients.size() >= kRelayMaxClientsPerRoom) {
                sendError(peer, "La partida está llena");
                return;
            }
            state.kind = PeerState::Kind::Client;
            state.room = code;
            state.id = ++m_nextClientId;
            room->second.clients[state.id] = peer;
            Writer open;
            open.put(RelayMessage::Open);
            open.put(state.id);
            sendTo(room->second.host, open);
            Writer joined;
            joined.put(RelayMessage::Joined);
            sendTo(peer, joined);
            log::info("Relay: client {} joined room {}", state.id, code);
            break;
        }
        case RelayMessage::Data: {
            if (state.kind != PeerState::Kind::Host) return;
            const auto id = reader.get<uint32_t>();
            const bool reliable = reader.get<uint8_t>() != 0;
            if (!reader.ok()) return;
            Room& room = m_rooms[state.room];
            auto client = room.clients.find(id);
            if (client == room.clients.end()) return;
            ENetPacket* out = enet_packet_create(reader.cursor(), reader.remaining(),
                                                 reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
            enet_peer_send(client->second, reliable ? kChannelReliable : kChannelUnreliable, out);
            break;
        }
        case RelayMessage::Close: {
            if (state.kind != PeerState::Kind::Host) return;
            const auto id = reader.get<uint32_t>();
            Room& room = m_rooms[state.room];
            auto client = room.clients.find(id);
            if (reader.ok() && client != room.clients.end()) enet_peer_disconnect_later(client->second, 0);
            break;
        }
        default:
            break;
        }
    }

    void leave(ENetPeer* peer) {
        auto found = m_peers.find(peer);
        if (found == m_peers.end()) return;
        const PeerState state = found->second;
        m_peers.erase(found);
        auto room = m_rooms.find(state.room);
        if (room == m_rooms.end()) return;
        if (state.kind == PeerState::Kind::Host) {
            for (auto& [id, client] : room->second.clients) {
                Writer w;
                w.put(RelayMessage::Error);
                w.putString("El anfitrión cerró la partida");
                sendTo(client, w);
                enet_peer_disconnect_later(client, 0);
                m_peers[client].kind = PeerState::Kind::Unknown; // Stop forwarding.
            }
            log::info("Relay: room {} closed", state.room);
            m_rooms.erase(room);
        } else if (state.kind == PeerState::Kind::Client) {
            room->second.clients.erase(state.id);
            Writer w;
            w.put(RelayMessage::Close);
            w.put(state.id);
            sendTo(room->second.host, w);
        }
    }

    // Hosts use the client<->relay round trip for lag compensation (plus their own to the relay).
    void reportRtt() {
        for (auto& [code, room] : m_rooms) {
            for (auto& [id, client] : room.clients) {
                Writer w;
                w.put(RelayMessage::Rtt);
                w.put(id);
                w.put(static_cast<uint16_t>(std::min<uint32_t>(client->roundTripTime, 65535)));
                sendTo(room.host, w, false);
            }
        }
    }

    size_t m_maxRooms;
    ENetHost* m_host = nullptr;
    std::map<ENetPeer*, PeerState> m_peers;
    std::map<std::string, Room> m_rooms;
    uint32_t m_nextClientId = 0;
    std::mt19937 m_rng;
};

} // namespace

int main(int argc, char** argv) {
    uint16_t port = kRelayDefaultPort;
    size_t maxRooms = 256;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--max-rooms" && i + 1 < argc) maxRooms = static_cast<size_t>(std::max(1, std::atoi(argv[++i])));
        else {
            std::printf("Usage: origon_relay [--port %u] [--max-rooms 256]\n", kRelayDefaultPort);
            return arg == "--help" ? 0 : 1;
        }
    }
    std::signal(SIGINT, [](int) { g_running = 0; });
    std::signal(SIGTERM, [](int) { g_running = 0; });
    if (enet_initialize() != 0) {
        log::error("ENet initialization failed");
        return 1;
    }
    int result = 0;
    {
        Relay relay(maxRooms);
        if (relay.start(port)) relay.run();
        else result = 1;
    }
    enet_deinitialize();
    return result;
}
