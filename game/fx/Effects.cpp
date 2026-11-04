#include "fx/Effects.h"

#include "fx/PlayerModel.h"
#include "sim/Simulation.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace origon {
namespace {

constexpr float kTracerLifetime = 0.06f;
constexpr float kFlashLifetime = 0.05f;
constexpr size_t kMaxParticles = 1500;
constexpr size_t kMaxCasings = 40;

glm::mat4 segmentTransform(const glm::vec3& a, const glm::vec3& b, float thickness) {
    const glm::vec3 delta = b - a;
    const float length = glm::length(delta);
    const glm::vec3 z = length > 1e-4f ? delta / length : glm::vec3(0, 0, 1);
    const glm::vec3 up = std::abs(z.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 x = glm::normalize(glm::cross(up, z));
    const glm::vec3 y = glm::cross(z, x);
    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(x, 0.0f);
    basis[1] = glm::vec4(y, 0.0f);
    basis[2] = glm::vec4(z, 0.0f);
    return glm::translate(glm::mat4(1.0f), (a + b) * 0.5f) * basis * glm::scale(glm::mat4(1.0f), glm::vec3(thickness, thickness, length));
}

} // namespace

float Effects::random01() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(m_rng);
}

void Effects::clear() {
    m_particles.clear();
    m_tracers.clear();
    m_flashes.clear();
    m_debris.clear(); // Bodies are destroyed by PhysicsWorld::clearBodies().
}

void Effects::emit(const glm::vec3& position, const glm::vec3& normal, int count, const glm::vec3& color, float speed,
                   float size, float life, float gravity, bool emissive, float spread, float growth) {
    for (int i = 0; i < count && m_particles.size() < kMaxParticles; ++i) {
        glm::vec3 random(random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f);
        const glm::vec3 dir = glm::normalize(normal + random * spread + glm::vec3(0.0f, 1e-3f, 0.0f));
        const float lifeScale = 0.6f + 0.8f * random01();
        m_particles.push_back({position + normal * 0.03f, dir * speed * (0.4f + 0.8f * random01()), color,
                               size * (0.6f + 0.8f * random01()), growth, life * lifeScale, life * lifeScale, gravity, 2.0f,
                               emissive});
    }
}

void Effects::onEvents(const std::vector<GameEvent>& events, const Simulation& sim, PhysicsWorld& physics,
                       const glm::vec3& localMuzzle) {
    m_sim = &sim;
    const int local = sim.localPlayerId();
    const auto& players = sim.players();
    for (const GameEvent& e : events) {
        switch (e.type) {
        case GameEvent::Type::Shot: {
            const glm::vec3 dir = glm::normalize(e.end - e.start);
            glm::vec3 start = e.a == local ? localMuzzle : e.start + dir * 0.7f;
            if (glm::dot(e.end - start, dir) > 1.0f && random01() < 0.6f) {
                m_tracers.push_back({start, e.end, 0.0f});
            }
            if (e.surface == 1) {
                emit(e.end, e.normal, 5, glm::vec3(6.0f, 3.2f, 1.0f), 6.0f, 0.025f, 0.25f, -14.0f, true, 0.8f);
                emit(e.end, e.normal, 4, glm::vec3(0.45f, 0.42f, 0.38f), 1.2f, 0.08f, 0.6f, -1.0f, false, 0.6f, 0.35f);
            } else if (e.surface == 3) {
                emit(e.end, e.normal, 6, glm::vec3(0.4f, 0.26f, 0.12f), 3.5f, 0.05f, 0.7f, -12.0f, false, 0.9f);
            } else if (e.surface == 2) {
                emit(e.end, -dir, 7, glm::vec3(0.35f, 0.02f, 0.02f), 2.5f, 0.05f, 0.4f, -9.0f, false, 0.7f);
            }
            break;
        }
        case GameEvent::Type::Fired: {
            if (e.category == WeaponCategory::Melee) {
                break;
            }
            const Player& shooter = players[static_cast<size_t>(e.a)];
            const glm::vec3 forward = viewDirection(shooter.yaw, shooter.pitch);
            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            const glm::vec3 muzzle = e.a == local ? localMuzzle : e.start + forward * 0.8f + right * 0.1f - glm::vec3(0, 0.3f, 0);
            m_flashes.push_back({muzzle, 0.0f});
            emit(muzzle, forward, 2, glm::vec3(0.5f), 0.8f, 0.06f, 0.5f, 0.6f, false, 0.4f, 0.5f);
            // Ejected casing (a physics debris body).
            if (e.category != WeaponCategory::Shotgun && e.category != WeaponCategory::BoltRifle &&
                e.category != WeaponCategory::Sniper) {
                const size_t casings = std::count_if(m_debris.begin(), m_debris.end(), [](const Debris& d) { return !d.corpse; });
                if (casings < kMaxCasings) {
                    const glm::vec3 eject = muzzle - forward * 0.5f + right * 0.05f;
                    const glm::vec3 velocity = shooter.velocity + right * (2.0f + random01()) + glm::vec3(0, 2.0f + random01(), 0);
                    const glm::vec3 spin(random01() * 20.0f, random01() * 20.0f, random01() * 20.0f);
                    const glm::vec3 half(0.006f, 0.006f, 0.018f);
                    const BodyHandle body = physics.addDynamicBox(eject, half, glm::quat(1, 0, 0, 0), 0.02f, BodyLayer::Debris, velocity, spin);
                    if (body != kInvalidBody) {
                        m_debris.push_back({body, half * 2.0f, glm::vec3(0.75f, 0.55f, 0.2f), 3.0f, false, 0.0f, 0});
                    }
                }
            }
            break;
        }
        case GameEvent::Type::Kill: {
            const Player& victim = players[static_cast<size_t>(e.b)];
            if (e.b == local) {
                break; // No corpse for the first-person view.
            }
            glm::vec3 push = victim.velocity;
            if (e.a >= 0 && e.a != e.b) {
                const glm::vec3 away = victim.position - players[static_cast<size_t>(e.a)].position;
                if (glm::length(away) > 0.01f) {
                    push += glm::normalize(glm::vec3(away.x, 0.0f, away.z)) * 3.5f + glm::vec3(0, 1.0f, 0);
                }
            }
            const glm::vec3 half(0.24f, 0.86f, 0.16f);
            const glm::quat rotation = glm::angleAxis(victim.yaw, glm::vec3(0, 1, 0));
            const glm::vec3 spin(random01() * 4.0f - 2.0f, 0.0f, random01() * 4.0f - 2.0f);
            const BodyHandle body =
                physics.addDynamicBox(victim.position + glm::vec3(0, 0.9f, 0), half, rotation, 70.0f, BodyLayer::Debris, push, spin);
            if (body != kInvalidBody) {
                m_debris.push_back({body, half * 2.0f, victim.color, 7.0f, true, victim.eyeHeight, victim.tier});
            }
            break;
        }
        default:
            break;
        }
    }
}

void Effects::update(float dt, PhysicsWorld& physics) {
    for (Particle& p : m_particles) {
        p.life -= dt;
        p.velocity.y += p.gravity * dt;
        p.velocity *= std::max(0.0f, 1.0f - p.drag * dt);
        p.position += p.velocity * dt;
        p.size += p.growth * dt;
    }
    std::erase_if(m_particles, [](const Particle& p) { return p.life <= 0.0f; });
    for (Tracer& t : m_tracers) t.age += dt;
    std::erase_if(m_tracers, [](const Tracer& t) { return t.age >= kTracerLifetime; });
    for (Flash& f : m_flashes) f.age += dt;
    std::erase_if(m_flashes, [](const Flash& f) { return f.age >= kFlashLifetime; });
    for (Debris& d : m_debris) {
        d.life -= dt;
        if (d.life <= 0.0f) {
            physics.removeBody(d.body);
        }
    }
    std::erase_if(m_debris, [](const Debris& d) { return d.life <= 0.0f; });
}

void Effects::submit(Renderer& renderer, const PhysicsWorld& physics) const {
    for (const Particle& p : m_particles) {
        const float fade = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
        DrawItem item;
        // Sparks are small cubes; smoke, dust and chips are rounded puffs.
        item.mesh = p.emissive ? &renderer.cube() : &renderer.sphere();
        item.model = glm::translate(glm::mat4(1.0f), p.position) * glm::scale(glm::mat4(1.0f), glm::vec3(p.size * (p.emissive ? 1.0f : std::sqrt(fade))));
        item.castShadow = false;
        if (p.emissive) {
            item.material.albedo = glm::vec3(0.0f);
            item.material.emissive = p.color * fade;
        } else {
            item.material.albedo = p.color;
            item.material.roughness = 1.0f;
        }
        renderer.submit(item);
    }
    for (const Tracer& t : m_tracers) {
        const float fade = 1.0f - t.age / kTracerLifetime;
        DrawItem item;
        item.mesh = &renderer.cube();
        item.model = segmentTransform(t.start, t.end, 0.02f);
        item.material.albedo = glm::vec3(0.0f);
        item.material.emissive = glm::vec3(8.0f, 6.0f, 2.5f) * fade;
        item.castShadow = false;
        renderer.submit(item);
    }
    for (const Flash& f : m_flashes) {
        const float fade = 1.0f - f.age / kFlashLifetime;
        renderer.addLight({f.position, 9.0f, glm::vec3(1.0f, 0.7f, 0.35f), 30.0f * fade});
        DrawItem item;
        item.mesh = &renderer.sphere();
        item.model = glm::translate(glm::mat4(1.0f), f.position) * glm::scale(glm::mat4(1.0f), glm::vec3(0.12f + 0.1f * fade));
        item.material.albedo = glm::vec3(0.0f);
        item.material.emissive = glm::vec3(12.0f, 7.0f, 2.5f) * fade;
        item.castShadow = false;
        renderer.submit(item);
    }
    for (const Debris& d : m_debris) {
        const BodyTransform t = physics.transform(d.body);
        if (d.corpse) {
            PlayerPose pose;
            // The body box is centered; the humanoid is drawn from the feet.
            pose.rotation = t.rotation;
            pose.feet = t.position + t.rotation * glm::vec3(0.0f, -0.88f, 0.0f);
            pose.color = d.color * 0.8f;
            pose.eyeHeight = 1.62f;
            pose.weapon = m_sim ? &m_sim->gunGame().weaponForTier(d.weaponTier) : nullptr;
            submitPlayerModel(renderer, pose);
        } else {
            DrawItem item;
            item.mesh = &renderer.cube();
            item.model = glm::translate(glm::mat4(1.0f), t.position) * glm::mat4_cast(t.rotation) * glm::scale(glm::mat4(1.0f), d.size);
            item.material.albedo = d.color;
            item.material.metallic = 1.0f;
            item.material.roughness = 0.35f;
            item.castShadow = false;
            renderer.submit(item);
        }
    }
}

} // namespace origon
