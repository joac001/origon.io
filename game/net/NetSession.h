#pragma once

#include "net/Protocol.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

namespace origon {
class Simulation;
struct MapData;
} // namespace origon

namespace origon::net {

struct PlayerInfo {
    int id = 0;
    std::string name;
    bool isBot = false;
};

// Listen server or client over ENet. The server owns the authoritative Simulation;
// clients send commands and receive snapshots and events.
class NetSession {
public:
    enum class Role { None, Server, Client };

    NetSession();
    ~NetSession();
    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    bool host(uint16_t port);
    bool connect(const std::string& address, uint16_t port, const std::string& name);
    void shutdown();

    Role role() const { return m_role; }
    bool isServer() const { return m_role == Role::Server; }
    bool isClient() const { return m_role == Role::Client; }
    const std::string& status() const { return m_status; }
    int peerCount() const;
    float pingMs() const;

    // --- Server ---
    // Accepts connections and reads client messages. Joins/leaves change the simulation's players.
    void serverPoll(Simulation& sim, const MapData& map);
    // Latest command received for a remote player, or nullptr.
    const PlayerCommand* remoteCommand(int playerId) const;
    bool isRemotePlayer(int playerId) const;
    void broadcastEvents(const std::vector<GameEvent>& events);
    void broadcastSnapshot(const Snapshot& snapshot);
    void broadcastPlayerInfo(const Simulation& sim);
    // After the server starts a new match (restart or map change): remote humans take slots again
    // and receive the (possibly new) map.
    void serverRematch(Simulation& sim, const MapData& map);

    // --- Client ---
    struct ClientUpdate {
        bool welcomed = false;
        int localId = -1;
        std::string mapJson;
        std::vector<GameEvent> events;
        std::optional<Snapshot> snapshot;
        std::vector<PlayerInfo> playerInfo;
        bool disconnected = false;
    };
    ClientUpdate clientPoll();
    void sendCommand(uint32_t tick, const PlayerCommand& command);

private:
    void send(_ENetPeer* peer, const Writer& writer, bool reliable);
    void broadcast(const Writer& writer, bool reliable);

    Role m_role = Role::None;
    _ENetHost* m_host = nullptr;
    _ENetPeer* m_server = nullptr; // Client side.
    std::string m_name;
    std::string m_status;
    bool m_welcomed = false;

    struct RemoteClient {
        int playerId = -1;
        std::string name;
        PlayerCommand command;
        uint32_t lastTick = 0;
    };
    std::map<_ENetPeer*, RemoteClient> m_clients;
};

} // namespace origon::net
