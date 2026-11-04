#include "sim/GunGame.h"

#include <algorithm>

namespace origon {

void GunGame::reset() {
    m_phase = Phase::Playing;
    m_winnerId = -1;
    m_timeLeft = rules().timeLimitSeconds;
    m_matchTime = 0.0f;
}

bool GunGame::update(float dt, std::vector<Player>& players, std::vector<GameEvent>& events) {
    if (m_phase != Phase::Playing) {
        return false;
    }
    m_matchTime += dt;
    m_timeLeft = std::max(0.0f, m_timeLeft - dt);
    if (m_timeLeft > 0.0f || players.empty()) {
        return false;
    }
    events.push_back({.type = GameEvent::Type::TimeUp});
    finish(standings(players).front()->id, events);
    return true;
}

const WeaponDef& GunGame::weaponForTier(int tier) const {
    const auto& ladder = m_weapons.ladder();
    return *ladder[static_cast<size_t>(std::clamp(tier, 0, finalTier()))];
}

void GunGame::onKill(Player* killer, Player& victim, KillCause cause, std::vector<GameEvent>& events) {
    if (m_phase != Phase::Playing) {
        return;
    }
    const bool selfInflicted = killer == nullptr || killer == &victim;
    if (selfInflicted) {
        if (rules().suicideDemotes) {
            demote(victim, cause == KillCause::World ? KillCause::World : KillCause::Suicide, events);
        }
        return;
    }

    killer->kills++;
    const bool melee = cause == KillCause::Melee;
    if (melee && rules().meleeDemotesVictim) {
        demote(victim, KillCause::Melee, events);
    }

    // The final tier weapon is the knife: any kill while on the final tier wins the match.
    if (killer->tier >= finalTier()) {
        finish(killer->id, events);
        return;
    }
    if (!melee || rules().meleePromotesKiller) {
        promote(*killer, events);
    }
}

void GunGame::promote(Player& player, std::vector<GameEvent>& events) {
    player.tier = std::min(player.tier + 1, finalTier());
    player.tierReachedTime = m_matchTime;
    const WeaponDef& weapon = weaponFor(player);
    player.ammo = weapon.magazine;
    player.reloadTimer = 0.0f;
    player.fireCooldown = 0.25f;
    events.push_back({.type = GameEvent::Type::Promoted, .a = player.id, .tier = player.tier,
                      .weaponName = weapon.name});
    if (player.tier == finalTier()) {
        events.push_back({.type = GameEvent::Type::FinalTier, .a = player.id, .tier = player.tier});
    }
}

void GunGame::demote(Player& player, KillCause cause, std::vector<GameEvent>& events) {
    if (player.tier == 0) {
        events.push_back({.type = GameEvent::Type::Demoted, .a = player.id, .tier = 0, .cause = cause});
        return;
    }
    player.tier--;
    player.tierReachedTime = m_matchTime;
    const WeaponDef& weapon = weaponFor(player);
    player.ammo = weapon.magazine;
    player.reloadTimer = 0.0f;
    events.push_back({.type = GameEvent::Type::Demoted, .a = player.id, .tier = player.tier, .cause = cause,
                      .weaponName = weapon.name});
}

void GunGame::finish(int winnerId, std::vector<GameEvent>& events) {
    m_phase = Phase::Finished;
    m_winnerId = winnerId;
    events.push_back({.type = GameEvent::Type::Victory, .a = winnerId});
}

std::vector<const Player*> GunGame::standings(const std::vector<Player>& players) {
    std::vector<const Player*> order;
    order.reserve(players.size());
    for (const Player& p : players) {
        order.push_back(&p);
    }
    std::stable_sort(order.begin(), order.end(), [](const Player* a, const Player* b) {
        if (a->tier != b->tier) return a->tier > b->tier;
        if (a->kills != b->kills) return a->kills > b->kills;
        if (a->deaths != b->deaths) return a->deaths < b->deaths;
        return a->tierReachedTime < b->tierReachedTime;
    });
    return order;
}

} // namespace origon
