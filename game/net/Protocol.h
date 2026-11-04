#pragma once

#include "sim/SimTypes.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace origon::net {

constexpr uint16_t kDefaultPort = 27015;
constexpr uint32_t kProtocolVersion = 3;
constexpr int kChannelReliable = 0;
constexpr int kChannelUnreliable = 1;
constexpr int kChannelCount = 2;

enum class MessageType : uint8_t {
    Hello = 1,      // C->S: version, name
    Welcome,        // S->C: player id, map JSON
    PlayerInfo,     // S->C: id, name, color, isBot
    Command,        // C->S: client tick, PlayerCommand
    Snapshot,       // S->C: match and player state, prop transforms
    Events,         // S->C: gameplay events
    Reject,         // S->C: reason
};

// Little-endian binary writer/reader.
class Writer {
public:
    template <typename T>
    void put(const T& value) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        data.insert(data.end(), bytes, bytes + sizeof(T));
    }
    void putString(const std::string& s) {
        put(static_cast<uint32_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    }
    void putVec3(const glm::vec3& v) {
        put(v.x);
        put(v.y);
        put(v.z);
    }
    std::vector<uint8_t> data;
};

class Reader {
public:
    Reader(const uint8_t* bytes, size_t size) : m_bytes(bytes), m_size(size) {}
    template <typename T>
    T get() {
        T value{};
        if (m_offset + sizeof(T) <= m_size) {
            std::memcpy(&value, m_bytes + m_offset, sizeof(T));
        } else {
            m_failed = true;
        }
        m_offset += sizeof(T);
        return value;
    }
    std::string getString() {
        const auto length = get<uint32_t>();
        if (m_offset + length > m_size) {
            m_failed = true;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(m_bytes + m_offset), length);
        m_offset += length;
        return s;
    }
    glm::vec3 getVec3() {
        const float x = get<float>();
        const float y = get<float>();
        const float z = get<float>();
        return {x, y, z};
    }
    bool ok() const { return !m_failed; }
    // Unread bytes (for wrapped packets).
    const uint8_t* cursor() const { return m_bytes + std::min(m_offset, m_size); }
    size_t remaining() const { return m_offset < m_size ? m_size - m_offset : 0; }

private:
    const uint8_t* m_bytes;
    size_t m_size;
    size_t m_offset = 0;
    bool m_failed = false;
};

struct PlayerState {
    uint8_t id = 0;
    uint8_t flags = 0; // 1 alive, 2 bot, 4 crouched, 8 on ground, 16 aiming
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float eyeHeight = kEyeHeight;
    float health = kMaxHealth;
    uint8_t tier = 0;
    uint16_t kills = 0;
    uint16_t deaths = 0;
    uint8_t ammo = 0;
    float reloadTimer = 0.0f;
    float respawnTimer = 0.0f;
    float spawnProtection = 0.0f;
    float viewKick = 0.0f;
    float meleeAnimation = 0.0f;
};

struct PropState {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct Snapshot {
    uint32_t serverTick = 0;
    float timeLeft = 0.0f;
    uint8_t phase = 0;
    int8_t winner = -1;
    std::vector<PlayerState> players;
    std::vector<PropState> props;
};

void writeCommand(Writer& w, uint32_t tick, const PlayerCommand& command);
PlayerCommand readCommand(Reader& r, uint32_t& tick);
void writeSnapshot(Writer& w, const Snapshot& snapshot);
Snapshot readSnapshot(Reader& r);
void writeEvents(Writer& w, const std::vector<GameEvent>& events);
std::vector<GameEvent> readEvents(Reader& r);

} // namespace origon::net
