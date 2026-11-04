#pragma once

#include <cstdint>
#include <string>

// Relay ("servidor intermedio") protocol, shared by the game and tools/relay.
//
// Hosts behind NAT cannot accept connections from the internet, but they can connect out. The relay
// is a small public ENet server both sides connect to:
//   host   -> relay: RelayRegister            relay -> host: RelayRegistered(code)
//   client -> relay: RelayJoin(code)          relay -> client: RelayJoined / RelayError
//                                             relay -> host: RelayOpen(id)
// After joining, the client speaks the normal game protocol to the relay, which wraps each packet in
// RelayData(id, ...) for the host. The host answers with RelayData(id, ...) and the relay unwraps it.
// Message type bytes are >= 0x80 so they never collide with game messages (net::MessageType).
namespace origon::net {

constexpr uint16_t kRelayDefaultPort = 27030;
constexpr uint32_t kRelayVersion = 1;
constexpr int kRelayCodeLength = 5;
constexpr int kRelayMaxClientsPerRoom = 16;

enum class RelayMessage : uint8_t {
    Register = 0x80, // H->R: version, server name, map name, players, max players (also updates the info)
    Registered,      // R->H: room code
    List,            // B->R: (empty)
    Rooms,           // R->B: count, then code, name, map, players, max players
    Join,            // C->R: version, code
    Joined,          // R->C: (empty) - start the game protocol
    Error,           // R->C/H: reason (Spanish, shown to the player)
    Open,            // R->H: client id
    Close,           // R->H: client id left; H->R: kick client id
    Data,            // R<->H: client id, reliable flag, game packet bytes
    Rtt,             // R->H: client id, round trip time client<->relay (ms)
};

inline bool isRelayMessage(uint8_t type) { return type >= 0x80; }

// Uppercase letters and digits without look-alikes (no 0/O, 1/I/L).
constexpr const char* kRelayCodeAlphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

// "host:port" or "host" (default port). Returns false on an empty host or a bad port.
inline bool parseHostPort(const std::string& text, std::string& host, uint16_t& port, uint16_t defaultPort) {
    const size_t colon = text.rfind(':');
    host = colon == std::string::npos ? text : text.substr(0, colon);
    port = defaultPort;
    if (colon != std::string::npos) {
        const std::string digits = text.substr(colon + 1);
        if (digits.empty() || digits.size() > 5 || digits.find_first_not_of("0123456789") != std::string::npos) return false;
        const unsigned long value = std::stoul(digits);
        if (value == 0 || value > 65535) return false;
        port = static_cast<uint16_t>(value);
    }
    return !host.empty();
}

} // namespace origon::net
