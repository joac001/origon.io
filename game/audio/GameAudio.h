#pragma once

#include "audio/AudioSystem.h"
#include "sim/SimTypes.h"

#include <glm/vec3.hpp>

#include <random>
#include <vector>

namespace origon {

class Simulation;

// Game sound design: builds the synthesized sound bank and maps simulation events to sounds.
class GameAudio {
public:
    bool init();
    void onEvents(const std::vector<GameEvent>& events, const Simulation& sim);
    // Footsteps, ambience and delayed sounds. Call once per frame while playing.
    void update(float dt, const Simulation& sim, const glm::vec3& listener, const glm::vec3& forward);
    void setVolume(float master);
    void setAmbienceEnabled(bool enabled);
    void playUi(const char* name);

private:
    struct Pending {
        float delay;
        SoundId sound;
        PlayParams params;
    };

    void build();
    SoundId shotSound(WeaponCategory category) const;
    void play(SoundId id, const PlayParams& params);
    void playAt(SoundId id, const glm::vec3& position, float volume, float pitchJitter, bool local);
    void schedule(float delay, SoundId id, const PlayParams& params);
    float jitter(float amount);

    AudioSystem m_audio;
    std::mt19937 m_rng{7};
    std::vector<Pending> m_pending;
    std::vector<float> m_stepDistance;
    float m_birdTimer = 3.0f;
    int m_windLoop = -1;
    bool m_ambience = true;
    glm::vec3 m_listener{0.0f};

    // Sound ids.
    SoundId m_shotPistol = kInvalidSound, m_shotSmg = kInvalidSound, m_shotRifle = kInvalidSound;
    SoundId m_shotSemi = kInvalidSound, m_shotBolt = kInvalidSound, m_shotSniper = kInvalidSound;
    SoundId m_shotShotgun = kInvalidSound, m_shotLmg = kInvalidSound;
    SoundId m_swish = kInvalidSound, m_jump = kInvalidSound, m_land = kInvalidSound;
    std::vector<SoundId> m_steps, m_impacts, m_ricochets, m_birds, m_hurts;
    SoundId m_bodyHit = kInvalidSound, m_propHit = kInvalidSound;
    SoundId m_hitMarker = kInvalidSound, m_headshot = kInvalidSound, m_killPing = kInvalidSound;
    SoundId m_reloadOut = kInvalidSound, m_reloadIn = kInvalidSound, m_bolt = kInvalidSound;
    SoundId m_promote = kInvalidSound, m_demote = kInvalidSound, m_final = kInvalidSound, m_victory = kInvalidSound;
    SoundId m_uiClick = kInvalidSound, m_wind = kInvalidSound;
};

} // namespace origon
