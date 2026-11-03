#include "sim/Simulation.h"

#include "physics/PhysicsWorld.h"

#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace origon {
namespace {

constexpr float kWalkSpeed = 4.6f;
constexpr float kSprintSpeed = 6.8f;
constexpr float kAdsSpeedFactor = 0.6f;
constexpr float kJumpSpeed = 5.4f;
constexpr float kGravity = -16.0f;
constexpr float kGroundAccel = 60.0f;
constexpr float kAirAccel = 10.0f;
constexpr float kMaxPitch = 1.53f;
constexpr float kMaxRange = 300.0f;
constexpr float kMeleeReach = 2.3f;
constexpr float kMeleeCooldown = 0.7f;
constexpr float kRegenDelay = 4.0f;
constexpr float kRegenRate = 30.0f;
constexpr float kKillHeight = -25.0f;
constexpr float kCrouchSpeedFactor = 0.5f;
constexpr float kFallDamageSpeed = 13.0f;
constexpr float kFallDamagePerMs = 9.0f;
constexpr float kBulletImpulseScale = 0.12f;

const char* kBotNames[] = {"Lobo", "Halcón", "Puma", "Cóndor", "Jaguar", "Zorro", "Oso", "Lince", "Búho"};
const glm::vec3 kPlayerColors[] = {
    {0.20f, 0.55f, 0.95f}, {0.90f, 0.30f, 0.25f}, {0.95f, 0.75f, 0.20f}, {0.35f, 0.80f, 0.35f},
    {0.75f, 0.40f, 0.90f}, {0.95f, 0.55f, 0.15f}, {0.25f, 0.85f, 0.85f}, {0.90f, 0.45f, 0.65f},
    {0.60f, 0.60f, 0.60f}, {0.55f, 0.35f, 0.20f},
};

glm::vec3 flatForward(float yaw) { return {-std::sin(yaw), 0.0f, -std::cos(yaw)}; }
glm::vec3 flatRight(float yaw) { return {std::cos(yaw), 0.0f, -std::sin(yaw)}; }

glm::vec3 moveTowards(const glm::vec3& current, const glm::vec3& target, float maxDelta) {
    const glm::vec3 delta = target - current;
    const float length = glm::length(delta);
    if (length <= maxDelta || length < 1e-5f) {
        return target;
    }
    return current + delta / length * maxDelta;
}

float wrapAngle(float angle) {
    while (angle > glm::pi<float>()) angle -= glm::two_pi<float>();
    while (angle < -glm::pi<float>()) angle += glm::two_pi<float>();
    return angle;
}

float raySphere(const glm::vec3& o, const glm::vec3& d, const glm::vec3& center, float radius) {
    const glm::vec3 m = o - center;
    const float b = glm::dot(m, d);
    const float c = glm::dot(m, m) - radius * radius;
    if (c > 0.0f && b > 0.0f) return -1.0f;
    const float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    return std::max(0.0f, -b - std::sqrt(disc));
}

// Ray against a capsule (segment a-b, radius r). Direction must be normalized.
float rayCapsule(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, float r) {
    const glm::vec3 ba = b - a;
    const glm::vec3 oa = o - a;
    const float baba = glm::dot(ba, ba);
    const float bard = glm::dot(ba, d);
    const float baoa = glm::dot(ba, oa);
    const float rdoa = glm::dot(d, oa);
    const float oaoa = glm::dot(oa, oa);
    const float qa = baba - bard * bard;
    const float qb = baba * rdoa - baoa * bard;
    const float qc = baba * oaoa - baoa * baoa - r * r * baba;
    float best = -1.0f;
    if (qa > 1e-6f) {
        const float h = qb * qb - qa * qc;
        if (h >= 0.0f) {
            const float t = (-qb - std::sqrt(h)) / qa;
            const float y = baoa + t * bard;
            if (y > 0.0f && y < baba && t >= 0.0f) {
                return t;
            }
        }
    }
    for (const glm::vec3& cap : {a, b}) {
        const float t = raySphere(o, d, cap, r);
        if (t >= 0.0f && (best < 0.0f || t < best)) {
            best = t;
        }
    }
    return best;
}

} // namespace

glm::vec3 viewDirection(float yaw, float pitch) {
    return {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

Simulation::Simulation(PhysicsWorld& physics, const WeaponDatabase& weapons)
    : m_physics(physics), m_weapons(weapons), m_gunGame(weapons) {}

void Simulation::startMatch(const MapData& map, int botCount) {
    m_map = map;
    m_players.clear();
    m_events.clear();
    m_gunGame.reset();

    const int maxPlayers = m_gunGame.rules().maxPlayers;
    const int total = std::clamp(botCount + 1, 1, maxPlayers);
    m_players.resize(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
        Player& p = m_players[static_cast<size_t>(i)];
        p.id = i;
        p.isBot = i != 0;
        p.name = p.isBot ? std::string("Bot ") + kBotNames[(i - 1) % std::size(kBotNames)] : "Jugador";
        p.color = kPlayerColors[i % std::size(kPlayerColors)];
        p.character = m_physics.createCharacter({}, glm::vec3(0.0f, -500.0f - 5.0f * i, 0.0f));
    }
    for (Player& p : m_players) {
        spawn(p);
    }
}

int Simulation::addRemotePlayer(const std::string& name) {
    for (Player& p : m_players) {
        if (p.isBot && p.id != m_localId) {
            p.isBot = false;
            p.name = name;
            p.bot = BotState{};
            return p.id;
        }
    }
    if (static_cast<int>(m_players.size()) >= m_gunGame.rules().maxPlayers) {
        return -1;
    }
    Player p;
    p.id = static_cast<int>(m_players.size());
    p.name = name;
    p.color = kPlayerColors[static_cast<size_t>(p.id) % std::size(kPlayerColors)];
    p.character = m_physics.createCharacter({}, glm::vec3(0.0f, -500.0f - 5.0f * static_cast<float>(p.id), 0.0f));
    m_players.push_back(std::move(p));
    spawn(m_players.back());
    return m_players.back().id;
}

void Simulation::removeRemotePlayer(int id) {
    if (id < 0 || id == m_localId || id >= static_cast<int>(m_players.size())) {
        return;
    }
    Player& p = m_players[static_cast<size_t>(id)];
    p.isBot = true;
    p.name = std::string("Bot ") + kBotNames[static_cast<size_t>(id - 1) % std::size(kBotNames)];
    p.bot = BotState{};
}

void Simulation::startClientMatch(const MapData& map, int localId) {
    m_map = map;
    m_players.clear();
    m_events.clear();
    m_gunGame.reset();
    m_localId = localId;
    ensurePlayerCount(localId + 1);
}

void Simulation::ensurePlayerCount(int count) {
    while (static_cast<int>(m_players.size()) < count) {
        Player p;
        p.id = static_cast<int>(m_players.size());
        p.name = "Jugador " + std::to_string(p.id + 1);
        p.color = kPlayerColors[static_cast<size_t>(p.id) % std::size(kPlayerColors)];
        p.character = m_physics.createCharacter({}, glm::vec3(0.0f, -500.0f - 5.0f * static_cast<float>(p.id), 0.0f));
        m_players.push_back(std::move(p));
    }
}

void Simulation::predictMovement(int id, const PlayerCommand& command) {
    if (id < 0 || id >= static_cast<int>(m_players.size())) {
        return;
    }
    Player& p = m_players[static_cast<size_t>(id)];
    if (!p.alive) {
        return;
    }
    p.previousPosition = p.position;
    p.yaw = wrapAngle(command.yaw);
    p.pitch = std::clamp(command.pitch, -kMaxPitch, kMaxPitch);
    updateMovement(p, command);
    p.previousButtons = command.buttons;
}

void Simulation::setRewindTicks(int playerId, int ticks) {
    if (playerId < 0) return;
    if (static_cast<int>(m_rewindTicks.size()) <= playerId) m_rewindTicks.resize(static_cast<size_t>(playerId) + 1, 0);
    m_rewindTicks[static_cast<size_t>(playerId)] = std::clamp(ticks, 0, kHistoryTicks - 1);
}

void Simulation::recordHistory() {
    m_history.resize(m_players.size());
    const size_t slot = m_tickCount % kHistoryTicks;
    for (const Player& p : m_players) {
        m_history[static_cast<size_t>(p.id)][slot] = {p.position, p.eyeHeight, p.alive};
    }
    ++m_tickCount;
}

bool Simulation::historicalTarget(const Player& target, int rewind, Player& out) const {
    if (rewind <= 0 || static_cast<size_t>(target.id) >= m_history.size() || static_cast<uint32_t>(rewind) >= m_tickCount) {
        return false;
    }
    const size_t slot = (m_tickCount - 1 - static_cast<uint32_t>(rewind)) % kHistoryTicks;
    const HitboxSample& sample = m_history[static_cast<size_t>(target.id)][slot];
    out.id = target.id;
    out.position = sample.position;
    out.eyeHeight = sample.eyeHeight;
    out.alive = sample.alive;
    return true;
}

std::vector<GameEvent> Simulation::takeEvents() {
    std::vector<GameEvent> events;
    events.swap(m_events);
    return events;
}

void Simulation::killSelf(int playerId) {
    if (playerId >= 0 && playerId < static_cast<int>(m_players.size())) {
        Player& p = m_players[static_cast<size_t>(playerId)];
        if (p.alive && m_gunGame.phase() == GunGame::Phase::Playing) {
            kill(nullptr, p, KillCause::Suicide);
        }
    }
}

const SpawnData* Simulation::chooseSpawn(const Player& player) {
    if (m_map.spawns.empty()) {
        return nullptr;
    }
    std::vector<std::pair<float, const SpawnData*>> scored;
    for (const SpawnData& spawn : m_map.spawns) {
        float nearest = 1e9f;
        for (const Player& other : m_players) {
            if (other.id != player.id && other.alive) {
                nearest = std::min(nearest, glm::distance(other.position, spawn.position));
            }
        }
        scored.emplace_back(nearest, &spawn);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const size_t pool = std::min<size_t>(3, scored.size());
    std::uniform_int_distribution<size_t> pick(0, pool - 1);
    return scored[pick(m_rng)].second;
}

void Simulation::spawn(Player& p) {
    const SpawnData* spawn = chooseSpawn(p);
    const glm::vec3 position = spawn ? spawn->position + glm::vec3(0.0f, 0.05f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    p.character->teleport(position);
    p.position = p.previousPosition = position;
    p.velocity = glm::vec3(0.0f);
    p.yaw = spawn ? glm::radians(spawn->yawDegrees) : 0.0f;
    p.pitch = 0.0f;
    p.viewReset = true;
    p.alive = true;
    p.health = kMaxHealth;
    p.spawnProtection = m_gunGame.rules().spawnProtectionSeconds;
    p.timeSinceDamage = 100.0f;
    p.ammo = weaponFor(p).magazine;
    p.fireCooldown = 0.3f;
    p.reloadTimer = 0.0f;
    p.meleeCooldown = 0.0f;
    p.viewKick = 0.0f;
    p.character->setCrouched(false);
    p.crouched = false;
    p.eyeHeight = kEyeHeight;
    p.airTime = 0.0f;
    p.fallSpeed = 0.0f;
    p.bot = BotState{};
    m_events.push_back({.type = GameEvent::Type::Spawned, .a = p.id});
}

void Simulation::tick(const std::vector<PlayerCommand>& commands) {
    const float dt = kTickDt;
    const bool playing = m_gunGame.phase() == GunGame::Phase::Playing;

    m_physics.update(dt);
    for (Player& p : m_players) {
        p.previousPosition = p.position;
    }

    for (Player& p : m_players) {
        PlayerCommand command =
            p.id < static_cast<int>(commands.size()) ? commands[static_cast<size_t>(p.id)] : PlayerCommand{};
        if (!p.alive) {
            if (playing) {
                p.respawnTimer -= dt;
                if (p.respawnTimer <= 0.0f) {
                    spawn(p);
                }
            }
            p.previousButtons = command.buttons;
            continue;
        }
        if (!playing) {
            command.forward = command.right = 0.0f;
            command.buttons = 0;
        }

        p.yaw = wrapAngle(command.yaw);
        p.pitch = std::clamp(command.pitch, -kMaxPitch, kMaxPitch);
        updateMovement(p, command);
        if (!p.alive) {
            p.previousButtons = command.buttons;
            continue; // Died from a fall this tick.
        }
        if (playing) {
            updateWeapon(p, command);
        }

        p.spawnProtection = std::max(0.0f, p.spawnProtection - dt);
        p.timeSinceDamage += dt;
        if (p.timeSinceDamage > kRegenDelay) {
            p.health = std::min(kMaxHealth, p.health + kRegenRate * dt);
        }
        p.viewKick -= p.viewKick * std::min(1.0f, 9.0f * dt);
        p.previousButtons = command.buttons;

        if (p.position.y < kKillHeight && p.alive && playing) {
            kill(nullptr, p, KillCause::World);
        }
    }

    m_gunGame.update(dt, m_players, m_events);
    recordHistory();
}

void Simulation::updateMovement(Player& p, const PlayerCommand& command) {
    const float dt = kTickDt;
    const WeaponDef& weapon = weaponFor(p);
    glm::vec3 wish = flatForward(p.yaw) * std::clamp(command.forward, -1.0f, 1.0f) +
                     flatRight(p.yaw) * std::clamp(command.right, -1.0f, 1.0f);
    const float wishLength = glm::length(wish);
    if (wishLength > 1.0f) {
        wish /= wishLength;
    }
    const bool wantsCrouch = (command.buttons & kButtonCrouch) != 0;
    if (wantsCrouch != p.crouched && p.character->setCrouched(wantsCrouch)) {
        p.crouched = wantsCrouch;
    }
    const float targetEye = p.crouched ? kCrouchEyeHeight : kEyeHeight;
    p.eyeHeight += (targetEye - p.eyeHeight) * std::min(1.0f, 14.0f * dt);

    const bool sprinting = (command.buttons & kButtonSprint) && command.forward > 0.1f && !p.aiming && !p.crouched;
    float speed = (sprinting ? kSprintSpeed : kWalkSpeed) * weapon.moveSpeedMultiplier;
    if (p.aiming) {
        speed *= kAdsSpeedFactor;
    }
    if (p.crouched) {
        speed *= kCrouchSpeedFactor;
    }
    wish *= speed;

    glm::vec3 velocity = p.velocity;
    glm::vec3 horizontal(velocity.x, 0.0f, velocity.z);
    const bool grounded = p.character->onGround() && velocity.y < 0.1f;
    if (grounded) {
        horizontal = moveTowards(horizontal, wish, kGroundAccel * dt);
        velocity.y = 0.0f;
        const bool jumpPressed = (command.buttons & kButtonJump) && !(p.previousButtons & kButtonJump);
        if (jumpPressed && !p.crouched) {
            velocity.y = kJumpSpeed;
            m_events.push_back({.type = GameEvent::Type::Jumped, .a = p.id, .start = p.position});
        }
    } else {
        horizontal = moveTowards(horizontal, wish, kAirAccel * dt);
        velocity.y += kGravity * dt;
    }
    velocity.x = horizontal.x;
    velocity.z = horizontal.z;

    const bool wasOnGround = p.onGround;
    const float verticalBefore = velocity.y;
    p.character->update(dt, velocity, glm::vec3(0.0f, kGravity, 0.0f));
    p.position = p.character->position();
    p.velocity = p.character->velocity();
    p.onGround = p.character->onGround();
    if (p.onGround && p.velocity.y < 0.0f) {
        p.velocity.y = 0.0f;
    }

    if (!p.onGround) {
        p.airTime += dt;
        p.fallSpeed = std::max(p.fallSpeed, -verticalBefore);
    } else if (!wasOnGround) {
        // Landing: event for audio and camera, damage for long falls.
        if (p.airTime > 0.15f) {
            m_events.push_back({.type = GameEvent::Type::Landed, .a = p.id, .start = p.position, .magnitude = p.fallSpeed});
        }
        if (p.fallSpeed > kFallDamageSpeed && m_gunGame.phase() == GunGame::Phase::Playing) {
            applyDamage(nullptr, p, (p.fallSpeed - kFallDamageSpeed) * kFallDamagePerMs, KillCause::World, false);
        }
        p.airTime = 0.0f;
        p.fallSpeed = 0.0f;
    }
}

void Simulation::updateWeapon(Player& p, const PlayerCommand& command) {
    const float dt = kTickDt;
    const WeaponDef& weapon = weaponFor(p);
    const uint32_t pressed = command.buttons & ~p.previousButtons;

    p.fireCooldown = std::max(0.0f, p.fireCooldown - dt);
    p.meleeCooldown = std::max(0.0f, p.meleeCooldown - dt);
    p.meleeAnimation = std::max(0.0f, p.meleeAnimation - dt);

    if (p.reloadTimer > 0.0f) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0.0f) {
            p.reloadTimer = 0.0f;
            p.ammo = weapon.magazine;
        }
    }

    const bool sprinting = (command.buttons & kButtonSprint) && command.forward > 0.1f;
    p.aiming = (command.buttons & kButtonAds) && !weapon.isMelee() && p.reloadTimer <= 0.0f && !sprinting;

    if ((pressed & kButtonMelee) || (weapon.isMelee() && (pressed & kButtonFire))) {
        if (p.meleeCooldown <= 0.0f) {
            melee(p);
        }
        return;
    }
    if (weapon.isMelee()) {
        return;
    }

    if ((pressed & kButtonReload) && p.reloadTimer <= 0.0f && p.ammo < weapon.magazine) {
        startReload(p, weapon);
        return;
    }

    const bool wantsFire = weapon.automatic ? (command.buttons & kButtonFire) != 0 : (pressed & kButtonFire) != 0;
    if (!wantsFire || p.fireCooldown > 0.0f || p.reloadTimer > 0.0f || p.meleeAnimation > 0.0f) {
        return;
    }
    if (p.ammo <= 0) {
        startReload(p, weapon);
        return;
    }
    fire(p, weapon);
}

void Simulation::fire(Player& p, const WeaponDef& weapon) {
    p.ammo--;
    p.fireCooldown = weapon.fireInterval();
    p.spawnProtection = 0.0f;

    const glm::vec3 origin = p.eyePosition();
    const glm::vec3 aim = viewDirection(p.yaw, p.pitch + p.viewKick);
    const glm::vec3 up = std::abs(aim.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 side = glm::normalize(glm::cross(aim, up));
    const glm::vec3 lift = glm::cross(side, aim);

    float spread = p.aiming ? weapon.spreadAdsDegrees : weapon.spreadHipDegrees;
    if (!p.aiming && glm::length(glm::vec2(p.velocity.x, p.velocity.z)) > 1.0f) {
        spread *= 1.4f;
    }
    if (!p.onGround) {
        spread *= 2.0f;
    }
    if (p.crouched) {
        spread *= 0.7f;
    }
    m_events.push_back({.type = GameEvent::Type::Fired, .a = p.id, .start = origin, .category = weapon.category});
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (int pellet = 0; pellet < weapon.pellets; ++pellet) {
        const float radius = std::tan(glm::radians(spread)) * std::sqrt(unit(m_rng));
        const float angle = glm::two_pi<float>() * unit(m_rng);
        const glm::vec3 dir = glm::normalize(aim + side * (radius * std::cos(angle)) + lift * (radius * std::sin(angle)));

        float worldDistance = kMaxRange;
        std::optional<RayHit> worldHit = m_physics.rayCast(origin, dir, kMaxRange, true);
        if (worldHit) {
            worldDistance = worldHit->distance;
        }
        Player* target = nullptr;
        float best = worldDistance;
        bool headshot = false;
        const int rewind = static_cast<size_t>(p.id) < m_rewindTicks.size() ? m_rewindTicks[static_cast<size_t>(p.id)] : 0;
        for (Player& other : m_players) {
            if (other.id == p.id || !other.alive) {
                continue;
            }
            bool head = false;
            // Lag compensation: test where the shooter saw the target.
            Player past;
            const bool rewound = historicalTarget(other, rewind, past);
            if (rewound && !past.alive) {
                continue;
            }
            const float t = rayVsPlayer(origin, dir, rewound ? past : other, head);
            if (t >= 0.0f && t < best) {
                best = t;
                target = &other;
                headshot = head;
            }
        }
        GameEvent shot{.type = GameEvent::Type::Shot, .a = p.id, .start = origin, .end = origin + dir * best};
        if (target) {
            shot.surface = 2;
            shot.normal = -dir;
        } else if (worldHit) {
            shot.surface = worldHit->dynamic ? 3 : 1;
            shot.normal = worldHit->normal;
            if (worldHit->dynamic) {
                m_physics.applyImpulse(worldHit->body, dir * weapon.damage * kBulletImpulseScale * 10.0f, worldHit->position);
            }
        }
        m_events.push_back(shot);
        if (target) {
            const float damage = weapon.damageAt(best) * (headshot ? weapon.headshotMultiplier : 1.0f);
            applyDamage(&p, *target, damage, KillCause::Weapon, headshot);
            if (!p.alive || m_gunGame.phase() != GunGame::Phase::Playing) {
                break;
            }
        }
    }
    p.viewKick += glm::radians(weapon.recoilDegrees) * (p.aiming ? 0.6f : 1.0f) * (p.crouched ? 0.75f : 1.0f);
    if (p.ammo == 0 && p.alive) {
        startReload(p, weapon);
    }
}

void Simulation::startReload(Player& p, const WeaponDef& weapon) {
    p.reloadTimer = weapon.reloadSeconds;
    m_events.push_back({.type = GameEvent::Type::Reload, .a = p.id, .start = p.eyePosition(), .category = weapon.category});
}

void Simulation::melee(Player& p) {
    m_events.push_back({.type = GameEvent::Type::MeleeSwing, .a = p.id, .start = p.eyePosition()});
    p.meleeCooldown = kMeleeCooldown;
    p.meleeAnimation = 0.3f;
    p.spawnProtection = 0.0f;

    const glm::vec3 eye = p.eyePosition();
    const glm::vec3 forward = viewDirection(p.yaw, p.pitch);
    Player* target = nullptr;
    float best = kMeleeReach;
    for (Player& other : m_players) {
        if (other.id == p.id || !other.alive) {
            continue;
        }
        const glm::vec3 toTarget = other.position + glm::vec3(0.0f, 1.0f, 0.0f) - eye;
        const float distance = glm::length(toTarget);
        if (distance >= best) {
            continue;
        }
        const bool inCone = glm::dot(toTarget / std::max(distance, 1e-4f), forward) > 0.6f || distance < 1.0f;
        if (inCone && lineOfSight(eye, other.position + glm::vec3(0.0f, 1.0f, 0.0f))) {
            best = distance;
            target = &other;
        }
    }
    if (target) {
        applyDamage(&p, *target, m_weapons.knife().damage, KillCause::Melee, false);
    }
}

void Simulation::applyDamage(Player* attacker, Player& victim, float damage, KillCause cause, bool headshot) {
    if (!victim.alive || victim.spawnProtection > 0.0f) {
        return;
    }
    victim.health -= damage;
    victim.timeSinceDamage = 0.0f;
    if (attacker) {
        m_events.push_back({.type = GameEvent::Type::Hit, .a = attacker->id, .b = victim.id, .headshot = headshot});
    }
    if (victim.health <= 0.0f) {
        kill(attacker, victim, cause);
    }
}

void Simulation::kill(Player* killer, Player& victim, KillCause cause) {
    victim.alive = false;
    victim.health = 0.0f;
    victim.deaths++;
    victim.respawnTimer = m_gunGame.rules().respawnDelaySeconds;
    victim.reloadTimer = 0.0f;
    victim.aiming = false;
    // Park the dead character far below the map so it does not block anyone.
    victim.character->teleport(glm::vec3(0.0f, -500.0f - 5.0f * victim.id, 0.0f));

    std::string weaponName;
    if (killer && killer != &victim) {
        weaponName = cause == KillCause::Melee ? m_weapons.knife().name : weaponFor(*killer).name;
    }
    m_events.push_back({.type = GameEvent::Type::Kill, .a = killer ? killer->id : -1, .b = victim.id,
                        .cause = cause, .weaponName = weaponName});
    m_gunGame.onKill(killer, victim, cause, m_events);
}

float Simulation::rayVsPlayer(const glm::vec3& origin, const glm::vec3& dir, const Player& target, bool& headshot) {
    const glm::vec3 head = target.position + glm::vec3(0.0f, target.eyeHeight, 0.0f);
    const float headT = raySphere(origin, dir, head, 0.24f);
    const float bodyTop = std::max(0.45f, target.eyeHeight - 0.32f);
    const float bodyT = rayCapsule(origin, dir, target.position + glm::vec3(0.0f, 0.4f, 0.0f),
                                   target.position + glm::vec3(0.0f, bodyTop, 0.0f), 0.4f);
    if (headT >= 0.0f && (bodyT < 0.0f || headT <= bodyT + 0.05f)) {
        headshot = true;
        return headT;
    }
    headshot = false;
    return bodyT;
}

bool Simulation::lineOfSight(const glm::vec3& from, const glm::vec3& to) const {
    const glm::vec3 delta = to - from;
    const float distance = glm::length(delta);
    if (distance < 1e-3f) {
        return true;
    }
    return !m_physics.rayCast(from, delta / distance, distance).has_value();
}

// --- Bots ------------------------------------------------------------------------------

void Simulation::computeBotCommands(std::vector<PlayerCommand>& commands) {
    commands.resize(m_players.size());
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float dt = kTickDt;

    for (Player& p : m_players) {
        if (!p.isBot) {
            continue;
        }
        PlayerCommand& command = commands[static_cast<size_t>(p.id)];
        command = PlayerCommand{.yaw = p.yaw, .pitch = p.pitch};
        if (!p.alive) {
            continue;
        }
        BotState& bot = p.bot;
        const WeaponDef& weapon = weaponFor(p);
        const glm::vec3 eye = p.eyePosition();

        // Target: nearest visible living opponent.
        Player* target = nullptr;
        float targetDistance = 70.0f;
        for (Player& other : m_players) {
            if (other.id == p.id || !other.alive) {
                continue;
            }
            const glm::vec3 chest = other.position + glm::vec3(0.0f, 1.2f, 0.0f);
            const float distance = glm::distance(eye, chest);
            if (distance < targetDistance && lineOfSight(eye, chest)) {
                targetDistance = distance;
                target = &other;
            }
        }

        float desiredYaw = p.yaw;
        float desiredPitch = 0.0f;
        if (target) {
            if (bot.targetId != target->id) {
                bot.targetId = target->id;
                bot.reactionTimer = 0.4f + 0.4f * unit(m_rng);
                bot.aimErrorYaw = glm::radians((unit(m_rng) - 0.5f) * 14.0f);
                bot.aimErrorPitch = glm::radians((unit(m_rng) - 0.5f) * 7.0f);
            }
            bot.reactionTimer -= dt;
            bot.aimErrorYaw *= 0.993f;
            bot.aimErrorPitch *= 0.993f;

            const glm::vec3 toTarget = target->position + glm::vec3(0.0f, 1.25f, 0.0f) - eye;
            desiredYaw = std::atan2(-toTarget.x, -toTarget.z) + bot.aimErrorYaw;
            desiredPitch = std::atan2(toTarget.y, glm::length(glm::vec2(toTarget.x, toTarget.z))) + bot.aimErrorPitch;

            const float aimError = std::abs(wrapAngle(desiredYaw - p.yaw)) + std::abs(desiredPitch - p.pitch);
            const bool aimed = aimError < 0.06f && bot.reactionTimer <= 0.0f;
            bot.fireToggle = !bot.fireToggle;

            if (weapon.isMelee()) {
                command.forward = 1.0f;
                command.buttons |= kButtonSprint;
                if (targetDistance < kMeleeReach && bot.fireToggle) {
                    command.buttons |= kButtonMelee;
                }
            } else {
                if (aimed && (weapon.automatic || bot.fireToggle)) {
                    command.buttons |= kButtonFire;
                }
                if (targetDistance < 2.0f && bot.fireToggle) {
                    command.buttons |= kButtonMelee;
                }
                const float preferred = std::clamp(weapon.rangeFull * 1.2f, 8.0f, 35.0f);
                command.forward = targetDistance > preferred ? 1.0f : (targetDistance < 4.0f ? -0.6f : 0.0f);
                if (targetDistance > 12.0f) {
                    command.buttons |= kButtonAds;
                }
            }
            bot.strafeTimer -= dt;
            if (bot.strafeTimer <= 0.0f) {
                bot.strafeDirection = unit(m_rng) < 0.5f ? -1.0f : 1.0f;
                bot.strafeTimer = 0.5f + unit(m_rng) * 0.9f;
            }
            command.right = bot.strafeDirection * (weapon.isMelee() ? 0.3f : 1.0f);
        } else {
            bot.targetId = -1;
            if (!bot.hasWaypoint || glm::distance(glm::vec2(p.position.x, p.position.z),
                                                  glm::vec2(bot.waypoint.x, bot.waypoint.z)) < 2.0f) {
                if (!m_map.spawns.empty()) {
                    std::uniform_int_distribution<size_t> pick(0, m_map.spawns.size() - 1);
                    bot.waypoint = m_map.spawns[pick(m_rng)].position;
                    bot.hasWaypoint = true;
                }
            }
            const glm::vec3 toWaypoint = bot.waypoint - p.position;
            desiredYaw = std::atan2(-toWaypoint.x, -toWaypoint.z);
            command.forward = 1.0f;
            command.buttons |= kButtonSprint;
        }

        // Obstacle avoidance (trees, rocks): steer toward the clearer side when moving forward
        // without a target to aim at, or while rushing with the knife.
        if (command.forward > 0.3f && (!target || weapon.isMelee())) {
            const glm::vec3 chest = p.position + glm::vec3(0.0f, 0.9f, 0.0f);
            auto blocked = [&](float offset) { return m_physics.rayCast(chest, flatForward(desiredYaw + offset), 2.4f, true).has_value(); };
            if (blocked(0.0f)) {
                const bool left = blocked(0.6f);
                const bool right = blocked(-0.6f);
                desiredYaw += !left ? 0.9f : (!right ? -0.9f : 1.8f);
                if (target) command.right = !left ? -0.8f : 0.8f;
            }
        }

        // Stuck detection: jump and pick a new waypoint.
        bot.stuckTimer += dt;
        if (bot.stuckTimer > 1.0f) {
            const bool stuck = glm::distance(p.position, bot.lastCheckPosition) < 0.6f && command.forward > 0.5f;
            if (stuck) {
                command.buttons |= kButtonJump;
                bot.hasWaypoint = false;
                bot.strafeDirection = -bot.strafeDirection;
            }
            bot.lastCheckPosition = p.position;
            bot.stuckTimer = 0.0f;
        }

        // Limited turn rate.
        const float maxTurn = 3.5f * dt;
        command.yaw = p.yaw + std::clamp(wrapAngle(desiredYaw - p.yaw), -maxTurn, maxTurn);
        command.pitch = p.pitch + std::clamp(desiredPitch - p.pitch, -maxTurn, maxTurn);
        if (p.previousButtons & kButtonJump) {
            command.buttons &= ~kButtonJump;
        }
    }
}

} // namespace origon
