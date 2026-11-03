#pragma once

#include "sim/GunGame.h"
#include "sim/SimTypes.h"
#include "sim/Weapons.h"
#include "world/MapData.h"

#include <array>
#include <random>
#include <string>
#include <vector>

namespace origon {

class PhysicsWorld;

// Authoritative game simulation at a fixed tick. Reads only PlayerCommands, never raw input,
// so the same code can run on a listen server with remote clients.
class Simulation {
public:
    Simulation(PhysicsWorld& physics, const WeaponDatabase& weapons);

    // Starts a new match on the given map with one local player and botCount bots.
    void startMatch(const MapData& map, int botCount);
    void tick(const std::vector<PlayerCommand>& commands);

    // Bot commands for this tick (local AI). Index = player id; non-bots keep their entry.
    void computeBotCommands(std::vector<PlayerCommand>& commands);

    std::vector<Player>& players() { return m_players; }
    const std::vector<Player>& players() const { return m_players; }
    const GunGame& gunGame() const { return m_gunGame; }
    const WeaponDef& weaponFor(const Player& player) const { return m_gunGame.weaponFor(player); }
    int localPlayerId() const { return m_localId; }
    void setLocalPlayerId(int id) { m_localId = id; }

    // --- Networking (listen server) ---
    // Server: a joining human takes over a bot slot (or a new slot). Returns the player id, -1 if full.
    int addRemotePlayer(const std::string& name);
    // Server: the slot goes back to a bot.
    void removeRemotePlayer(int id);
    // Client: replica of the server state. Creates players (and their controllers) as needed.
    void startClientMatch(const MapData& map, int localId);
    void ensurePlayerCount(int count);
    void setMatchState(float timeLeft, int phase, int winner) { m_gunGame.setReplicated(timeLeft, phase, winner); }
    // Client prediction: moves only the given player with the same movement code as the server.
    void predictMovement(int id, const PlayerCommand& command);
    std::vector<Player>& mutablePlayers() { return m_players; }
    // Lag compensation: shots from this player test targets as they were this many ticks ago.
    void setRewindTicks(int playerId, int ticks);

    // Events since the last call. The caller takes ownership.
    std::vector<GameEvent> takeEvents();

    // Debug: kill the local player (counts as suicide).
    void killSelf(int playerId);

private:
    void spawn(Player& player);
    void updateMovement(Player& player, const PlayerCommand& command);
    void updateWeapon(Player& player, const PlayerCommand& command);
    void fire(Player& player, const WeaponDef& weapon);
    void startReload(Player& player, const WeaponDef& weapon);
    void melee(Player& player);
    void applyDamage(Player* attacker, Player& victim, float damage, KillCause cause, bool headshot);
    void kill(Player* killer, Player& victim, KillCause cause);
    const SpawnData* chooseSpawn(const Player& player);

    // Returns the hit distance to a player's hit volume, or a negative value on miss.
    static float rayVsPlayer(const glm::vec3& origin, const glm::vec3& dir, const Player& target, bool& headshot);
    bool lineOfSight(const glm::vec3& from, const glm::vec3& to) const;

    PhysicsWorld& m_physics;
    const WeaponDatabase& m_weapons;
    GunGame m_gunGame;
    MapData m_map;
    std::vector<Player> m_players;
    std::vector<GameEvent> m_events;
    std::mt19937 m_rng{1337};
    int m_localId = 0;

    // Per-tick history of hit volumes for lag compensation.
    static constexpr int kHistoryTicks = 32;
    struct HitboxSample {
        glm::vec3 position{0.0f};
        float eyeHeight = kEyeHeight;
        bool alive = false;
    };
    std::vector<std::array<HitboxSample, kHistoryTicks>> m_history;
    std::vector<int> m_rewindTicks;
    uint32_t m_tickCount = 0;
    void recordHistory();
    // Copy of a player with its hit volume moved back in time (only position/eyeHeight/alive).
    bool historicalTarget(const Player& target, int rewind, Player& out) const;
};

glm::vec3 viewDirection(float yaw, float pitch);

} // namespace origon
