#pragma once

#include "audio/AudioSystem.h"
#include "sim/SimTypes.h"

#include <glm/vec3.hpp>

#include <functional>
#include <random>
#include <string>
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
    void dumpWav(const std::string& folder) const { m_audio.dumpWav(folder); }
    // 0..1: how close the listener is to water (creek loop volume).
    void setWaterProximity(float proximity);
    // Footstep surface: returns true when a position stands on a wooden structure (not terrain).
    void setSurfaceQuery(std::function<bool(const glm::vec3&)> onWood) { m_onWood = std::move(onWood); }

private:
    struct Pending {
        float delay;
        SoundId sound;
        PlayParams params;
    };

    void build();
    // Recorded variants assets/sounds/<name>_NN.flac (tools/sounds/build_sounds.py). Empty if none.
    std::vector<SoundId> loadRecorded(const std::string& name, float gain);
    SoundId pick(const std::vector<SoundId>& set);
    SoundId shotSound(WeaponCategory category) const;
    std::vector<SoundId> m_farShots; // Indexed by WeaponCategory.
    // Variant sets (recorded when available, else the synthesized sound).
    std::vector<std::vector<SoundId>> m_shotSets; // Indexed by WeaponCategory.
    std::vector<SoundId> m_farSet, m_boltSet, m_reloadSet, m_reloadPistolSet, m_casings, m_stepsWood, m_knives,
        m_bodyHits, m_lands;
    bool m_recordedReloads = false;
    bool m_recordedAmbience = false;
    int m_forestLoop = -1;
    SoundId m_forest = kInvalidSound;
    std::function<bool(const glm::vec3&)> m_onWood;
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
    int m_streamLoop = -1;
    SoundId m_stream = kInvalidSound;
    float m_waterProximity = 0.0f;
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
