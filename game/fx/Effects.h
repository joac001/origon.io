#pragma once

#include "physics/PhysicsWorld.h"
#include "render/Renderer.h"
#include "sim/SimTypes.h"

#include <glm/vec3.hpp>

#include <random>
#include <vector>

namespace origon {

class Simulation;

// Cosmetic effects. Nothing here affects gameplay: debris bodies live on the Debris layer,
// which characters and bullets ignore.
class Effects {
public:
    // Removes all effects. Call before the physics world is rebuilt (bodies are destroyed there).
    void clear();
    void onEvents(const std::vector<GameEvent>& events, const Simulation& sim, PhysicsWorld& physics,
                  const glm::vec3& localMuzzle);
    void update(float dt, PhysicsWorld& physics);
    void submit(Renderer& renderer, const PhysicsWorld& physics) const;

private:
    struct Particle {
        glm::vec3 position;
        glm::vec3 velocity;
        glm::vec3 color;
        float size;
        float growth;
        float life;
        float maxLife;
        float gravity;
        float drag;
        bool emissive;
    };
    struct Tracer {
        glm::vec3 start;
        glm::vec3 end;
        float age;
    };
    struct Flash {
        glm::vec3 position;
        float age;
    };
    struct Debris {
        BodyHandle body;
        glm::vec3 size;
        glm::vec3 color;
        float life;
        bool corpse;
        float eyeHeight;
        int weaponTier;
    };

    void emit(const glm::vec3& position, const glm::vec3& normal, int count, const glm::vec3& color, float speed,
              float size, float life, float gravity, bool emissive, float spread = 1.0f, float growth = 0.0f);
    float random01();

    std::vector<Particle> m_particles;
    std::vector<Tracer> m_tracers;
    std::vector<Flash> m_flashes;
    std::vector<Debris> m_debris;
    std::mt19937 m_rng{99};
    const Simulation* m_sim = nullptr;
};

} // namespace origon
