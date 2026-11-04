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
    CharacterLook look;
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
    // Client: join a room on a relay (servidor intermedio) by its code.
    bool connectViaRelay(const std::string& address, uint16_t port, const std::string& code, const std::string& name);
    void shutdown();
    // Client: appearance sent in Hello.
    void setLocalLook(const CharacterLook& look) { m_look = look; }

    // Server: also accept players through a relay. The room code arrives asynchronously (relayCode()).
    bool publishToRelay(const std::string& address, uint16_t port);
    void unpublishFromRelay();
    // Room name/map/player count shown in the relay's list. Sent only when it changes.
    void updateRelayInfo(const std::string& name, const std::string& map, int players, int maxPlayers);
    bool isPublished() const { return m_relay != nullptr; }
    const std::string& relayCode() const { return m_relayCode; }
    const std::string& relayStatus() const { return m_relayStatus; }

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
    // Round trip time to the client that controls this player (ms), or 0.
    float playerRttMs(int playerId) const;
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
    void sendLook(const CharacterLook& look);

private:
    struct RemoteClient {
        _ENetPeer* peer = nullptr; // Direct connection, or nullptr for a relayed client.
        uint32_t relayId = 0;
        float relayRttMs = 0.0f;   // Client <-> relay (relayed clients).
        int playerId = -1;
        std::string name;
        CharacterLook look;
        PlayerCommand command;
        uint32_t lastTick = 0;
    };
    using ClientKey = uint64_t;
    static ClientKey directKey(const _ENetPeer* peer) { return reinterpret_cast<uintptr_t>(peer); }
    static ClientKey relayKey(uint32_t id) { return (1ull << 63) | id; }

    void send(_ENetPeer* peer, const Writer& writer, bool reliable);
    void send(const RemoteClient& client, const Writer& writer, bool reliable);
    void broadcast(const Writer& writer, bool reliable);
    void sendRelayRegister();
    // Game messages from one client (direct or unwrapped from the relay).
    void handleClientMessage(ClientKey key, Reader& reader, Simulation& sim, const MapData& map, bool& rosterChanged);
    void dropClient(ClientKey key, Simulation& sim, bool& rosterChanged);
    void handleRelayMessage(Reader& reader, Simulation& sim, const MapData& map, bool& rosterChanged);
    void kick(const RemoteClient& client);

    Role m_role = Role::None;
    _ENetHost* m_host = nullptr;
    _ENetPeer* m_server = nullptr; // Client side.
    std::string m_name;
    CharacterLook m_look;
    std::string m_status;
    bool m_welcomed = false;
    std::map<ClientKey, RemoteClient> m_clients;

    // Relay (servidor intermedio).
    _ENetPeer* m_relay = nullptr;   // Server side: the upstream connection to the relay.
    bool m_viaRelay = false;        // Client side: m_server is a relay.
    std::string m_relayJoinCode;    // Client side.
    std::string m_relayCode;        // Server side: our room code.
    std::string m_relayStatus;
    std::string m_relayInfo;        // Last info sent (name|map|players|max).
    std::string m_roomName, m_roomMap;
    int m_roomPlayers = 0, m_roomMaxPlayers = 0;
};

} // namespace origon::net
