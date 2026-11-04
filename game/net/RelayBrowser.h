#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

namespace origon::net {

// Lists the rooms published on a relay (servidor intermedio): a short-lived ENet connection
// sends List and reads the Rooms reply.
class RelayBrowser {
public:
    struct Room {
        std::string code;
        std::string name;
        std::string map;
        int players = 0;
        int maxPlayers = 0;
    };

    ~RelayBrowser();
    // Starts a query unless one is running. Call refresh() again later to update the list.
    void refresh(const std::string& address, uint16_t port);
    // Call every frame while the browser is visible.
    void poll();
    bool busy() const { return m_host != nullptr; }
    const std::vector<Room>& rooms() const { return m_rooms; }
    const std::string& status() const { return m_status; }

private:
    void close();

    _ENetHost* m_host = nullptr;
    _ENetPeer* m_peer = nullptr;
    bool m_enetReady = false;
    bool m_replied = false;
    std::vector<Room> m_rooms;
    std::string m_status;
};

} // namespace origon::net
