#include "net/NetSession.h"

#include "core/Log.h"
#include "sim/Simulation.h"
#include "world/MapData.h"

#include <enet/enet.h>

#include <algorithm>
#include <format>

namespace origon::net {
namespace {

int g_enetUsers = 0;

bool acquireEnet() {
    if (g_enetUsers == 0 && enet_initialize() != 0) {
        log::error("ENet initialization failed");
        return false;
    }
    ++g_enetUsers;
    return true;
}

void releaseEnet() {
    if (g_enetUsers > 0 && --g_enetUsers == 0) {
        enet_deinitialize();
    }
}

} // namespace

NetSession::NetSession() = default;

NetSession::~NetSession() { shutdown(); }

void NetSession::shutdown() {
    if (!m_host) {
        m_role = Role::None;
        return;
    }
    if (m_role == Role::Client && m_server) {
        enet_peer_disconnect_now(m_server, 0);
    }
    for (auto& [peer, client] : m_clients) {
        enet_peer_disconnect_now(peer, 0);
    }
    enet_host_flush(m_host);
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_server = nullptr;
    m_clients.clear();
    m_role = Role::None;
    m_welcomed = false;
    m_status = "Desconectado";
    releaseEnet();
}

bool NetSession::host(uint16_t port) {
    shutdown();
    if (!acquireEnet()) return false;
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    m_host = enet_host_create(&address, 16, kChannelCount, 0, 0);
    if (!m_host) {
        m_status = std::format("No se pudo abrir el puerto {}", port);
        log::error("Server: cannot bind port {}", port);
        releaseEnet();
        return false;
    }
    m_role = Role::Server;
    m_status = std::format("Servidor en el puerto {}", port);
    log::info("Server listening on port {}", port);
    return true;
}

bool NetSession::connect(const std::string& address, uint16_t port, const std::string& name) {
    shutdown();
    if (!acquireEnet()) return false;
    m_host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (!m_host) {
        releaseEnet();
        return false;
    }
    ENetAddress target{};
    if (enet_address_set_host(&target, address.c_str()) != 0) {
        m_status = "Dirección inválida";
        enet_host_destroy(m_host);
        m_host = nullptr;
        releaseEnet();
        return false;
    }
    target.port = port;
    m_server = enet_host_connect(m_host, &target, kChannelCount, 0);
    m_name = name;
    m_role = Role::Client;
    m_status = std::format("Conectando a {}:{}...", address, port);
    log::info("Connecting to {}:{}", address, port);
    return m_server != nullptr;
}

int NetSession::peerCount() const {
    return m_role == Role::Server ? static_cast<int>(m_clients.size()) : (m_welcomed ? 1 : 0);
}

float NetSession::pingMs() const { return m_server ? static_cast<float>(m_server->roundTripTime) : 0.0f; }

void NetSession::send(_ENetPeer* peer, const Writer& writer, bool reliable) {
    ENetPacket* packet = enet_packet_create(writer.data.data(), writer.data.size(),
                                            reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(peer, reliable ? kChannelReliable : kChannelUnreliable, packet);
}

void NetSession::broadcast(const Writer& writer, bool reliable) {
    for (auto& [peer, client] : m_clients) {
        if (client.playerId >= 0) send(peer, writer, reliable);
    }
}

// --- Server -------------------------------------------------------------------------------

void NetSession::serverPoll(Simulation& sim, const MapData& map) {
    if (m_role != Role::Server) return;
    ENetEvent event;
    bool rosterChanged = false;
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            m_clients[event.peer] = RemoteClient{};
            enet_peer_timeout(event.peer, 0, 4000, 10000);
            log::info("Server: peer connected");
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            Reader reader(event.packet->data, event.packet->dataLength);
            const auto type = static_cast<MessageType>(reader.get<uint8_t>());
            RemoteClient& client = m_clients[event.peer];
            if (type == MessageType::Hello && client.playerId < 0) {
                const auto version = reader.get<uint32_t>();
                const std::string name = reader.getString().substr(0, 24);
                if (version != kProtocolVersion) {
                    Writer w;
                    w.put(static_cast<uint8_t>(MessageType::Reject));
                    w.putString("Versión incompatible");
                    send(event.peer, w, true);
                    enet_peer_disconnect_later(event.peer, 0);
                } else {
                    const int id = sim.addRemotePlayer(name.empty() ? "Jugador" : name);
                    if (id < 0) {
                        Writer w;
                        w.put(static_cast<uint8_t>(MessageType::Reject));
                        w.putString("Servidor lleno");
                        send(event.peer, w, true);
                        enet_peer_disconnect_later(event.peer, 0);
                    } else {
                        client.playerId = id;
                        client.name = name.empty() ? "Jugador" : name;
                        Writer w;
                        w.put(static_cast<uint8_t>(MessageType::Welcome));
                        w.put(static_cast<uint8_t>(id));
                        w.putString(map.toJson());
                        send(event.peer, w, true);
                        rosterChanged = true;
                        log::info("Server: '{}' joined as player {}", name, id);
                    }
                }
            } else if (type == MessageType::Command && client.playerId >= 0) {
                uint32_t tick = 0;
                const PlayerCommand command = readCommand(reader, tick);
                if (reader.ok() && tick >= client.lastTick) {
                    client.lastTick = tick;
                    client.command = command;
                }
            }
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            auto it = m_clients.find(event.peer);
            if (it != m_clients.end()) {
                if (it->second.playerId >= 0) {
                    sim.removeRemotePlayer(it->second.playerId);
                    rosterChanged = true;
                    log::info("Server: player {} left", it->second.playerId);
                }
                m_clients.erase(it);
            }
            break;
        }
        default:
            break;
        }
    }
    if (rosterChanged) {
        broadcastPlayerInfo(sim);
    }
    m_status = std::format("Servidor: {} jugador(es) conectado(s)", m_clients.size());
}

const PlayerCommand* NetSession::remoteCommand(int playerId) const {
    for (const auto& [peer, client] : m_clients) {
        if (client.playerId == playerId) return &client.command;
    }
    return nullptr;
}

bool NetSession::isRemotePlayer(int playerId) const { return remoteCommand(playerId) != nullptr; }

void NetSession::broadcastEvents(const std::vector<GameEvent>& events) {
    if (m_role != Role::Server || events.empty() || m_clients.empty()) return;
    Writer w;
    writeEvents(w, events);
    broadcast(w, true);
}

void NetSession::broadcastSnapshot(const Snapshot& snapshot) {
    if (m_role != Role::Server || m_clients.empty()) return;
    Writer w;
    writeSnapshot(w, snapshot);
    broadcast(w, false);
    enet_host_flush(m_host);
}

void NetSession::serverRematch(Simulation& sim, const MapData& map) {
    if (m_role != Role::Server) return;
    const std::string mapJson = map.toJson();
    for (auto& [peer, client] : m_clients) {
        if (client.playerId < 0) continue;
        client.playerId = sim.addRemotePlayer(client.name);
        client.command = PlayerCommand{};
        client.lastTick = 0;
        Writer w;
        w.put(static_cast<uint8_t>(MessageType::Welcome));
        w.put(static_cast<uint8_t>(std::max(client.playerId, 0)));
        w.putString(mapJson);
        send(peer, w, true);
    }
    broadcastPlayerInfo(sim);
}

void NetSession::broadcastPlayerInfo(const Simulation& sim) {
    if (m_role != Role::Server) return;
    Writer w;
    w.put(static_cast<uint8_t>(MessageType::PlayerInfo));
    w.put(static_cast<uint8_t>(sim.players().size()));
    for (const Player& p : sim.players()) {
        w.put(static_cast<uint8_t>(p.id));
        w.putString(p.name);
        w.put(static_cast<uint8_t>(p.isBot ? 1 : 0));
    }
    broadcast(w, true);
}

// --- Client --------------------------------------------------------------------------------

NetSession::ClientUpdate NetSession::clientPoll() {
    ClientUpdate update;
    if (m_role != Role::Client) return update;
    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            Writer w;
            w.put(static_cast<uint8_t>(MessageType::Hello));
            w.put(kProtocolVersion);
            w.putString(m_name);
            send(m_server, w, true);
            m_status = "Conectado. Recibiendo mapa...";
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            Reader reader(event.packet->data, event.packet->dataLength);
            const auto type = static_cast<MessageType>(reader.get<uint8_t>());
            if (type == MessageType::Welcome) {
                update.welcomed = true;
                update.localId = reader.get<uint8_t>();
                update.mapJson = reader.getString();
                m_welcomed = true;
                m_status = "En partida";
            } else if (type == MessageType::Snapshot) {
                Snapshot snapshot = readSnapshot(reader);
                if (reader.ok() && (!update.snapshot || snapshot.serverTick > update.snapshot->serverTick)) {
                    update.snapshot = std::move(snapshot);
                }
            } else if (type == MessageType::Events) {
                auto events = readEvents(reader);
                update.events.insert(update.events.end(), events.begin(), events.end());
            } else if (type == MessageType::PlayerInfo) {
                const auto count = reader.get<uint8_t>();
                update.playerInfo.clear();
                for (int i = 0; i < count && reader.ok(); ++i) {
                    PlayerInfo info;
                    info.id = reader.get<uint8_t>();
                    info.name = reader.getString();
                    info.isBot = reader.get<uint8_t>() != 0;
                    update.playerInfo.push_back(info);
                }
            } else if (type == MessageType::Reject) {
                m_status = "Rechazado: " + reader.getString();
            }
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            update.disconnected = true;
            if (m_status.rfind("Rechazado", 0) != 0) m_status = "Conexión perdida";
            m_welcomed = false;
            break;
        default:
            break;
        }
    }
    return update;
}

void NetSession::sendCommand(uint32_t tick, const PlayerCommand& command) {
    if (m_role != Role::Client || !m_welcomed) return;
    Writer w;
    writeCommand(w, tick, command);
    send(m_server, w, false);
    enet_host_flush(m_host);
}

} // namespace origon::net
