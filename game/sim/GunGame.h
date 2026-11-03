#pragma once

#include "sim/SimTypes.h"
#include "sim/Weapons.h"

#include <vector>

namespace origon {

// Gun Game rules (see docs/research/gun-game-rules.md). Runs only on the authoritative simulation.
class GunGame {
public:
    enum class Phase { Playing, Finished };

    explicit GunGame(const WeaponDatabase& weapons) : m_weapons(weapons) {}

    void reset();
    // Advances the match clock. Returns true when the time limit ends the match this tick.
    bool update(float dt, std::vector<Player>& players, std::vector<GameEvent>& events);

    // Applies promotion, demotion and victory for one death. killer is null for world deaths.
    void onKill(Player* killer, Player& victim, KillCause cause, std::vector<GameEvent>& events);

    const WeaponDef& weaponFor(const Player& player) const { return weaponForTier(player.tier); }
    const WeaponDef& weaponForTier(int tier) const;
    int finalTier() const { return m_weapons.tierCount() - 1; }
    const GunGameRules& rules() const { return m_weapons.rules(); }

    Phase phase() const { return m_phase; }
    int winnerId() const { return m_winnerId; }
    float timeLeft() const { return m_timeLeft; }
    float matchTime() const { return m_matchTime; }

    // Returns players ordered by standing: tier, kills, fewer deaths, earliest tier reach.
    static std::vector<const Player*> standings(const std::vector<Player>& players);

private:
    void promote(Player& player, std::vector<GameEvent>& events);
    void demote(Player& player, KillCause cause, std::vector<GameEvent>& events);
    void finish(int winnerId, std::vector<GameEvent>& events);

    const WeaponDatabase& m_weapons;
    Phase m_phase = Phase::Playing;
    int m_winnerId = -1;
    float m_timeLeft = 0.0f;
    float m_matchTime = 0.0f;
};

} // namespace origon
