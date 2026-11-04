#include "net/NetSession.h"

#include "net/RelayProtocol.h"

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
    for (auto& [key, client] : m_clients) {
        if (client.peer) enet_peer_disconnect_now(client.peer, 0);
    }
    if (m_relay) enet_peer_disconnect_now(m_relay, 0);
    enet_host_flush(m_host);
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_server = nullptr;
    m_relay = nullptr;
    m_viaRelay = false;
    m_relayCode.clear();
    m_relayStatus.clear();
    m_relayInfo.clear();
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
    m_host = enet_host_create(&address, 17, kChannelCount, 0, 0); // 16 players + the relay link.
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

bool NetSession::connectViaRelay(const std::string& address, uint16_t port, const std::string& code, const std::string& name) {
    if (!connect(address, port, name)) return false;
    m_viaRelay = true;
    m_relayJoinCode = code;
    m_status = std::format("Conectando al servidor intermedio {}:{}...", address, port);
    return true;
}

bool NetSession::publishToRelay(const std::string& address, uint16_t port) {
    if (m_role != Role::Server) return false;
    unpublishFromRelay();
    ENetAddress target{};
    if (enet_address_set_host(&target, address.c_str()) != 0) {
        m_relayStatus = "Dirección del servidor intermedio inválida";
        return false;
    }
    target.port = port;
    m_relay = enet_host_connect(m_host, &target, kChannelCount, 0);
    m_relayStatus = m_relay ? std::format("Conectando a {}:{}...", address, port) : "No se pudo conectar";
    log::info("Server: publishing to relay {}:{}", address, port);
    return m_relay != nullptr;
}

void NetSession::unpublishFromRelay() {
    if (!m_relay) return;
    enet_peer_disconnect_later(m_relay, 0);
    m_relay = nullptr;
    m_relayCode.clear();
    m_relayInfo.clear();
    m_relayStatus = "No publicada";
}

void NetSession::updateRelayInfo(const std::string& name, const std::string& map, int players, int maxPlayers) {
    m_roomName = name;
    m_roomMap = map;
    m_roomPlayers = players;
    m_roomMaxPlayers = maxPlayers;
    if (m_relay && !m_relayCode.empty() && m_relayInfo != std::format("{}|{}|{}|{}", name, map, players, maxPlayers)) {
        sendRelayRegister();
    }
}

void NetSession::sendRelayRegister() {
    Writer w;
    w.put(RelayMessage::Register);
    w.put(kRelayVersion);
    w.putString(m_roomName);
    w.putString(m_roomMap);
    w.put(static_cast<uint8_t>(std::clamp(m_roomPlayers, 0, 255)));
    w.put(static_cast<uint8_t>(std::clamp(m_roomMaxPlayers, 0, 255)));
    send(m_relay, w, true);
    m_relayInfo = std::format("{}|{}|{}|{}", m_roomName, m_roomMap, m_roomPlayers, m_roomMaxPlayers);
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

void NetSession::send(const RemoteClient& client, const Writer& writer, bool reliable) {
    if (client.peer) {
        send(client.peer, writer, reliable);
        return;
    }
    if (!m_relay) return;
    Writer w;
    w.data.reserve(writer.data.size() + 6);
    w.put(RelayMessage::Data);
    w.put(client.relayId);
    w.put(static_cast<uint8_t>(reliable ? 1 : 0));
    w.data.insert(w.data.end(), writer.data.begin(), writer.data.end());
    send(m_relay, w, reliable);
}

void NetSession::broadcast(const Writer& writer, bool reliable) {
    for (auto& [key, client] : m_clients) {
        if (client.playerId >= 0) send(client, writer, reliable);
    }
}

void NetSession::kick(const RemoteClient& client) {
    if (client.peer) {
        enet_peer_disconnect_later(client.peer, 0);
    } else if (m_relay) {
        Writer w;
        w.put(RelayMessage::Close);
        w.put(client.relayId);
        send(m_relay, w, true);
    }
}

// --- Server -------------------------------------------------------------------------------

void NetSession::serverPoll(Simulation& sim, const MapData& map) {
    if (m_role != Role::Server) return;
    ENetEvent event;
    bool rosterChanged = false;
    while (enet_host_service(m_host, &event, 0) > 0) {
        const bool fromRelay = m_relay && event.peer == m_relay;
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (fromRelay) {
                enet_peer_timeout(event.peer, 0, 5000, 15000);
                m_relayStatus = "Registrando partida...";
                sendRelayRegister();
                break;
            }
            m_clients[directKey(event.peer)] = RemoteClient{event.peer};
            enet_peer_timeout(event.peer, 0, 4000, 10000);
            log::info("Server: peer connected");
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            Reader reader(event.packet->data, event.packet->dataLength);
            if (fromRelay) {
                handleRelayMessage(reader, sim, map, rosterChanged);
            } else if (m_clients.contains(directKey(event.peer))) {
                handleClientMessage(directKey(event.peer), reader, sim, map, rosterChanged);
            }
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            if (fromRelay) {
                // Relayed players lose their link with the relay.
                std::vector<ClientKey> relayed;
                for (const auto& [key, client] : m_clients) {
                    if (!client.peer) relayed.push_back(key);
                }
                for (ClientKey key : relayed) dropClient(key, sim, rosterChanged);
                m_relay = nullptr;
                m_relayCode.clear();
                if (m_relayStatus.rfind("Error", 0) != 0) m_relayStatus = "Se perdió la conexión con el servidor intermedio";
                log::warn("Server: relay link lost");
                break;
            }
            dropClient(directKey(event.peer), sim, rosterChanged);
            break;
        default:
            break;
        }
    }
    if (rosterChanged) {
        broadcastPlayerInfo(sim);
    }
    m_status = std::format("Servidor: {} jugador(es) conectado(s)", m_clients.size());
}

void NetSession::handleRelayMessage(Reader& reader, Simulation& sim, const MapData& map, bool& rosterChanged) {
    const auto type = static_cast<RelayMessage>(reader.get<uint8_t>());
    switch (type) {
    case RelayMessage::Registered:
        m_relayCode = reader.getString();
        m_relayStatus = "Publicada";
        log::info("Server: relay room code {}", m_relayCode);
        break;
    case RelayMessage::Error:
        m_relayStatus = "Error: " + reader.getString();
        break;
    case RelayMessage::Open: {
        const auto id = reader.get<uint32_t>();
        if (!reader.ok()) break;
        RemoteClient client;
        client.relayId = id;
        m_clients[relayKey(id)] = client;
        log::info("Server: relayed peer {} connected", id);
        break;
    }
    case RelayMessage::Close: {
        const auto id = reader.get<uint32_t>();
        if (reader.ok()) dropClient(relayKey(id), sim, rosterChanged);
        break;
    }
    case RelayMessage::Rtt: {
        const auto id = reader.get<uint32_t>();
        const auto rtt = reader.get<uint16_t>();
        auto it = m_clients.find(relayKey(id));
        if (reader.ok() && it != m_clients.end()) it->second.relayRttMs = static_cast<float>(rtt);
        break;
    }
    case RelayMessage::Data: {
        const auto id = reader.get<uint32_t>();
        reader.get<uint8_t>(); // Reliability flag: not needed on this side.
        if (!reader.ok() || !m_clients.contains(relayKey(id))) break;
        Reader inner(reader.cursor(), reader.remaining());
        handleClientMessage(relayKey(id), inner, sim, map, rosterChanged);
        break;
    }
    default:
        break;
    }
}

void NetSession::handleClientMessage(ClientKey key, Reader& reader, Simulation& sim, const MapData& map, bool& rosterChanged) {
    RemoteClient& client = m_clients[key];
    const auto type = static_cast<MessageType>(reader.get<uint8_t>());
    if (type == MessageType::Hello && client.playerId < 0) {
        const auto version = reader.get<uint32_t>();
        const std::string name = reader.getString().substr(0, 24);
        if (version != kProtocolVersion) {
            Writer w;
            w.put(static_cast<uint8_t>(MessageType::Reject));
            w.putString("Versión incompatible");
            send(client, w, true);
            kick(client);
            return;
        }
        const int id = sim.addRemotePlayer(name.empty() ? "Jugador" : name);
        if (id < 0) {
            Writer w;
            w.put(static_cast<uint8_t>(MessageType::Reject));
            w.putString("Servidor lleno");
            send(client, w, true);
            kick(client);
            return;
        }
        client.playerId = id;
        client.name = name.empty() ? "Jugador" : name;
        Writer w;
        w.put(static_cast<uint8_t>(MessageType::Welcome));
        w.put(static_cast<uint8_t>(id));
        w.putString(map.toJson());
        send(client, w, true);
        rosterChanged = true;
        log::info("Server: '{}' joined as player {}{}", name, id, client.peer ? "" : " (via relay)");
    } else if (type == MessageType::Command && client.playerId >= 0) {
        uint32_t tick = 0;
        const PlayerCommand command = readCommand(reader, tick);
        if (reader.ok() && tick >= client.lastTick) {
            client.lastTick = tick;
            client.command = command;
        }
    }
}

void NetSession::dropClient(ClientKey key, Simulation& sim, bool& rosterChanged) {
    auto it = m_clients.find(key);
    if (it == m_clients.end()) return;
    if (it->second.playerId >= 0) {
        sim.removeRemotePlayer(it->second.playerId);
        rosterChanged = true;
        log::info("Server: player {} left", it->second.playerId);
    }
    m_clients.erase(it);
}

const PlayerCommand* NetSession::remoteCommand(int playerId) const {
    for (const auto& [key, client] : m_clients) {
        if (client.playerId == playerId) return &client.command;
    }
    return nullptr;
}

bool NetSession::isRemotePlayer(int playerId) const { return remoteCommand(playerId) != nullptr; }

float NetSession::playerRttMs(int playerId) const {
    for (const auto& [key, client] : m_clients) {
        if (client.playerId != playerId) continue;
        if (client.peer) return static_cast<float>(client.peer->roundTripTime);
        // Relayed: client <-> relay <-> host.
        return client.relayRttMs + (m_relay ? static_cast<float>(m_relay->roundTripTime) : 0.0f);
    }
    return 0.0f;
}

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
    for (auto& [key, client] : m_clients) {
        if (client.playerId < 0) continue;
        client.playerId = sim.addRemotePlayer(client.name);
        client.command = PlayerCommand{};
        client.lastTick = 0;
        Writer w;
        w.put(static_cast<uint8_t>(MessageType::Welcome));
        w.put(static_cast<uint8_t>(std::max(client.playerId, 0)));
        w.putString(mapJson);
        send(client, w, true);
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
            if (m_viaRelay) {
                w.put(RelayMessage::Join);
                w.put(kRelayVersion);
                w.putString(m_relayJoinCode);
                m_status = "Buscando la partida " + m_relayJoinCode + "...";
            } else {
                w.put(static_cast<uint8_t>(MessageType::Hello));
                w.put(kProtocolVersion);
                w.putString(m_name);
                m_status = "Conectado. Recibiendo mapa...";
            }
            send(m_server, w, true);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            Reader reader(event.packet->data, event.packet->dataLength);
            const uint8_t rawType = reader.get<uint8_t>();
            const auto type = static_cast<MessageType>(rawType);
            if (m_viaRelay && isRelayMessage(rawType)) {
                const auto relayType = static_cast<RelayMessage>(rawType);
                if (relayType == RelayMessage::Joined) {
                    Writer w;
                    w.put(static_cast<uint8_t>(MessageType::Hello));
                    w.put(kProtocolVersion);
                    w.putString(m_name);
                    send(m_server, w, true);
                    m_status = "Partida encontrada. Recibiendo mapa...";
                } else if (relayType == RelayMessage::Error) {
                    m_status = "Rechazado: " + reader.getString();
                }
            } else if (type == MessageType::Welcome) {
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
