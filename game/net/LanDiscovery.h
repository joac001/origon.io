#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace origon::net {

// LAN server browser: hosts broadcast a small UDP announcement every second on
// kDiscoveryPort; clients listen and keep a list of servers seen in the last few seconds.
class LanDiscovery {
public:
    static constexpr uint16_t kDiscoveryPort = 27020;

    struct Server {
        std::string address;
        uint16_t port = 0;
        std::string name;
        std::string map;
        int players = 0;
        int maxPlayers = 0;
        double lastSeen = 0.0;
    };

    ~LanDiscovery();
    // Host side: call every frame while hosting.
    void announce(double now, uint16_t gamePort, const std::string& name, const std::string& map, int players, int maxPlayers);
    // Browser side: call every frame while the browser is open.
    void listen(double now);
    void stop();
    const std::vector<Server>& servers() const { return m_servers; }

private:
    bool ensureSender();
    bool ensureListener();

    int m_sender = -1;
    int m_listener = -1;
    bool m_enetReady = false;
    double m_nextAnnounce = 0.0;
    std::vector<Server> m_servers;
};

} // namespace origon::net
