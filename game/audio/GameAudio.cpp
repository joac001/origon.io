#include "audio/GameAudio.h"

#include "audio/Synth.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "sim/Simulation.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <filesystem>
#include <string>

namespace origon {

bool GameAudio::init() {
    if (!m_audio.init()) {
        return false;
    }
    const auto start = std::chrono::steady_clock::now();
    build();
    const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("Audio: sound bank ready in {:.0f} ms ({} recorded ambience)", ms, m_recordedAmbience ? "with" : "without");
    m_windLoop = m_audio.startLoop(m_wind, m_recordedAmbience ? 0.22f : 0.35f);
    m_streamLoop = m_audio.startLoop(m_stream, 0.0f);
    if (m_forest != kInvalidSound) m_forestLoop = m_audio.startLoop(m_forest, 0.4f);
    return true;
}

void GameAudio::build() {
    using namespace synth;
    auto add = [&](const std::string& name, Buffer buffer) { return m_audio.add(name, std::move(buffer)); };

    // Gunshots per category: brightness, body and tail define the character of each class.
    m_shotPistol = add("shot_pistol", gunshot({.crackCutoff = 7000, .crackDecay = 0.018f, .thumpFrequency = 95, .thumpDecay = 0.05f,
                                               .thumpGain = 0.6f, .tailCutoff = 1100, .tailDecay = 0.22f, .tailGain = 0.25f,
                                               .duration = 0.8f, .seed = 11}));
    m_shotSmg = add("shot_smg", gunshot({.crackCutoff = 6500, .crackDecay = 0.02f, .thumpFrequency = 85, .thumpDecay = 0.05f,
                                         .thumpGain = 0.7f, .tailCutoff = 1000, .tailDecay = 0.2f, .tailGain = 0.25f,
                                         .echoGain = 0.18f, .duration = 0.7f, .seed = 12}));
    m_shotRifle = add("shot_rifle", gunshot({.crackCutoff = 8000, .crackDecay = 0.022f, .thumpFrequency = 70, .thumpDecay = 0.07f,
                                             .thumpGain = 0.8f, .tailCutoff = 900, .tailDecay = 0.3f, .tailGain = 0.32f,
                                             .duration = 0.9f, .seed = 13}));
    m_shotLmg = add("shot_lmg", gunshot({.crackCutoff = 7000, .crackDecay = 0.026f, .thumpFrequency = 60, .thumpDecay = 0.08f,
                                         .thumpGain = 0.95f, .tailCutoff = 800, .tailDecay = 0.32f, .tailGain = 0.35f,
                                         .duration = 0.9f, .seed = 14}));
    m_shotSemi = add("shot_semi", gunshot({.crackCutoff = 9000, .crackDecay = 0.024f, .thumpFrequency = 65, .thumpDecay = 0.09f,
                                           .thumpGain = 0.9f, .tailCutoff = 850, .tailDecay = 0.45f, .tailGain = 0.4f,
                                           .duration = 1.3f, .seed = 15}));
    m_shotBolt = add("shot_bolt", gunshot({.crackCutoff = 10000, .crackDecay = 0.028f, .thumpFrequency = 55, .thumpDecay = 0.11f,
                                           .thumpGain = 1.0f, .tailCutoff = 750, .tailDecay = 0.6f, .tailGain = 0.45f,
                                           .echoDelay = 0.16f, .echoGain = 0.3f, .duration = 1.8f, .seed = 16}));
    m_shotSniper = add("shot_sniper", gunshot({.crackCutoff = 11000, .crackDecay = 0.03f, .thumpFrequency = 50, .thumpDecay = 0.12f,
                                               .thumpGain = 1.0f, .tailCutoff = 700, .tailDecay = 0.75f, .tailGain = 0.5f,
                                               .echoDelay = 0.2f, .echoGain = 0.35f, .duration = 2.2f, .seed = 17}));
    m_shotShotgun = add("shot_shotgun", gunshot({.crackCutoff = 5000, .crackDecay = 0.035f, .thumpFrequency = 48, .thumpDecay = 0.13f,
                                                 .thumpGain = 1.0f, .tailCutoff = 650, .tailDecay = 0.55f, .tailGain = 0.5f,
                                                 .echoDelay = 0.14f, .echoGain = 0.3f, .duration = 1.6f, .seed = 18}));

    // Distant variants, one per weapon category (same order as WeaponCategory).
    const char* names[] = {"pistol", "smg", "rifle", "lmg", "semi", "bolt", "sniper", "shotgun"};
    const std::pair<const char*, GunshotParams> farSources[] = {
        {names[0], {.crackCutoff = 7000, .crackDecay = 0.018f, .thumpFrequency = 95, .duration = 0.8f, .seed = 11}},
        {names[1], {.crackCutoff = 6500, .crackDecay = 0.02f, .thumpFrequency = 85, .duration = 0.7f, .seed = 12}},
        {names[2], {.crackCutoff = 8000, .crackDecay = 0.022f, .thumpFrequency = 70, .duration = 0.9f, .seed = 13}},
        {names[3], {.crackCutoff = 7000, .crackDecay = 0.026f, .thumpFrequency = 60, .duration = 0.9f, .seed = 14}},
        {names[4], {.crackCutoff = 9000, .crackDecay = 0.024f, .thumpFrequency = 65, .duration = 1.0f, .seed = 15}},
        {names[5], {.crackCutoff = 10000, .crackDecay = 0.028f, .thumpFrequency = 55, .duration = 1.0f, .seed = 16}},
        {names[6], {.crackCutoff = 11000, .crackDecay = 0.03f, .thumpFrequency = 50, .duration = 1.0f, .seed = 17}},
        {names[7], {.crackCutoff = 5000, .crackDecay = 0.035f, .thumpFrequency = 48, .duration = 1.0f, .seed = 18}},
    };
    for (const auto& [name, params] : farSources) {
        m_farShots.push_back(add(std::string("shot_far_") + name, distant(gunshot(params), params.seed)));
    }
    m_swish = add("knife_swish", swish(0.22f, 700.0f, 3200.0f, 21));
    m_jump = add("jump", swish(0.15f, 400.0f, 1200.0f, 22));
    m_land = add("land", thud(65.0f, 0.06f, 900.0f, 0.5f, 23));
    for (uint32_t i = 0; i < 4; ++i) m_steps.push_back(add("step_" + std::to_string(i), footstep(0.35f + 0.1f * static_cast<float>(i), 30 + i)));
    for (uint32_t i = 0; i < 3; ++i) m_impacts.push_back(add("impact_" + std::to_string(i), thud(180.0f + 60.0f * static_cast<float>(i), 0.015f, 4500.0f, 1.2f, 40 + i)));
    for (uint32_t i = 0; i < 3; ++i) m_ricochets.push_back(add("ricochet_" + std::to_string(i), ricochet(50 + i)));
    for (uint32_t i = 0; i < 2; ++i) m_hurts.push_back(add("hurt_" + std::to_string(i), grunt(130.0f + 25.0f * static_cast<float>(i), 60 + i)));
    m_bodyHit = add("body_hit", thud(140.0f, 0.04f, 700.0f, 0.8f, 70));
    m_propHit = add("prop_hit", thud(220.0f, 0.05f, 1600.0f, 0.6f, 71));
    m_hitMarker = add("hitmarker", click(3200.0f, 0.012f, 0.6f, 80));
    m_headshot = add("headshot", ding({1900.0f, 2850.0f, 4300.0f}, 0.18f));
    m_killPing = add("kill_ping", jingle({1046.5f, 1568.0f}, 0.06f, 0.2f));
    m_reloadOut = add("reload_out", click(1800.0f, 0.03f, 1.0f, 90));
    m_reloadIn = add("reload_in", click(1200.0f, 0.045f, 1.2f, 91));
    {
        Buffer bolt = click(1500.0f, 0.03f, 1.0f, 92);
        mixInto(bolt, click(1100.0f, 0.04f, 1.0f, 93), 0.16f, 1.0f);
        normalize(bolt, 0.8f);
        m_bolt = add("bolt", std::move(bolt));
    }
    m_promote = add("ui_promote", jingle({523.25f, 659.25f, 783.99f}, 0.07f, 0.4f));
    m_demote = add("ui_demote", jingle({392.0f, 311.13f, 246.94f}, 0.11f, 0.8f));
    m_final = add("ui_final", jingle({880.0f, 1174.66f, 880.0f, 1174.66f}, 0.12f, 0.6f));
    m_victory = add("ui_victory", jingle({523.25f, 659.25f, 783.99f, 1046.5f}, 0.13f, 0.5f));
    m_uiClick = add("ui_click", click(1000.0f, 0.01f, 0.3f, 94));
    m_wind = add("wind", windLoop(12.0f, 95));
    m_stream = add("stream", streamLoop(9.0f, 96));
    for (uint32_t i = 0; i < 5; ++i) m_birds.push_back(add("bird_" + std::to_string(i), birdCall(100 + i)));

    // Recorded CC0 sounds replace the synthesized ones where they exist (same WeaponCategory order).
    const char* shotNames[] = {"pistol", "smg", "rifle", "lmg", "semi", "bolt", "sniper", "shotgun"};
    const SoundId synthShots[] = {m_shotPistol, m_shotSmg, m_shotRifle, m_shotLmg, m_shotSemi, m_shotBolt, m_shotSniper, m_shotShotgun};
    for (size_t i = 0; i < std::size(shotNames); ++i) {
        std::vector<SoundId> set = loadRecorded(std::string("shot_") + shotNames[i], 1.0f);
        if (set.empty()) set.push_back(synthShots[i]);
        m_shotSets.push_back(std::move(set));
    }
    m_shotSets.push_back({m_swish}); // Melee.
    auto orSynth = [](std::vector<SoundId> set, SoundId fallback) {
        if (set.empty()) set.push_back(fallback);
        return set;
    };
    m_farSet = loadRecorded("shot_far", 1.0f);
    m_boltSet = orSynth(loadRecorded("bolt", 1.0f), m_bolt);
    m_reloadSet = loadRecorded("reload", 1.0f);
    m_reloadPistolSet = loadRecorded("reload_pistol", 1.0f);
    m_recordedReloads = !m_reloadSet.empty();
    m_casings = loadRecorded("casing", 1.0f);
    if (auto steps = loadRecorded("step_dirt", 1.0f); !steps.empty()) m_steps = std::move(steps);
    m_stepsWood = orSynth(loadRecorded("step_wood", 1.0f), m_steps.front());
    m_knives = orSynth(loadRecorded("knife", 1.0f), m_swish);
    m_bodyHits = orSynth(loadRecorded("body_hit", 1.0f), m_bodyHit);
    m_lands = orSynth(loadRecorded("land", 1.0f), m_land);
    if (auto forest = loadRecorded("ambience_forest", 1.0f); !forest.empty()) {
        m_forest = forest.front();
        m_recordedAmbience = true;
    }
    if (auto wind = loadRecorded("ambience_wind", 1.0f); !wind.empty()) m_wind = wind.front();
    if (auto stream = loadRecorded("ambience_stream", 1.0f); !stream.empty()) m_stream = stream.front();
}

std::vector<SoundId> GameAudio::loadRecorded(const std::string& name, float gain) {
    std::vector<SoundId> out;
    for (int i = 1; i < 100; ++i) {
        const std::string file = std::format("assets/sounds/{}_{:02d}.flac", name, i);
        const std::filesystem::path path = fs::resolve(file);
        if (!std::filesystem::exists(path)) break;
        const SoundId id = m_audio.addFile(std::format("{}_{:02d}", name, i), path.string(), gain);
        if (id != kInvalidSound) out.push_back(id);
    }
    return out;
}

SoundId GameAudio::pick(const std::vector<SoundId>& set) {
    if (set.empty()) return kInvalidSound;
    std::uniform_int_distribution<size_t> dist(0, set.size() - 1);
    return set[dist(m_rng)];
}

float GameAudio::jitter(float amount) {
    std::uniform_real_distribution<float> dist(-amount, amount);
    return 1.0f + dist(m_rng);
}

void GameAudio::setVolume(float master) { m_audio.setMasterVolume(master); }

void GameAudio::setAmbienceEnabled(bool enabled) {
    m_ambience = enabled;
    m_audio.setLoopVolume(m_windLoop, enabled ? (m_recordedAmbience ? 0.22f : 0.35f) : 0.0f);
    m_audio.setLoopVolume(m_forestLoop, enabled ? 0.4f : 0.0f);
}

void GameAudio::setWaterProximity(float proximity) {
    // Smooth so the creek fades in and out while walking.
    m_waterProximity += (std::clamp(proximity, 0.0f, 1.0f) - m_waterProximity) * 0.1f;
    m_audio.setLoopVolume(m_streamLoop, m_ambience ? m_waterProximity * m_waterProximity * 0.6f : 0.0f);
}

void GameAudio::playUi(const char* name) { m_audio.play(m_audio.find(name), {.volume = 0.6f}); }

void GameAudio::play(SoundId id, const PlayParams& params) { m_audio.play(id, params); }

void GameAudio::playAt(SoundId id, const glm::vec3& position, float volume, float pitchJitter, bool local) {
    PlayParams params;
    params.volume = volume;
    params.pitch = jitter(pitchJitter);
    if (!local) {
        params.spatial = true;
        params.position = position;
        params.minDistance = 3.0f;
        params.maxDistance = 250.0f;
    }
    m_audio.play(id, params);
}

void GameAudio::schedule(float delay, SoundId id, const PlayParams& params) { m_pending.push_back({delay, id, params}); }

SoundId GameAudio::shotSound(WeaponCategory category) const {
    switch (category) {
    case WeaponCategory::Pistol: return m_shotPistol;
    case WeaponCategory::Smg: return m_shotSmg;
    case WeaponCategory::AssaultRifle: return m_shotRifle;
    case WeaponCategory::Lmg: return m_shotLmg;
    case WeaponCategory::SemiAutoRifle: return m_shotSemi;
    case WeaponCategory::BoltRifle: return m_shotBolt;
    case WeaponCategory::Sniper: return m_shotSniper;
    case WeaponCategory::Shotgun: return m_shotShotgun;
    case WeaponCategory::Melee: return m_swish;
    }
    return m_shotSmg;
}

void GameAudio::onEvents(const std::vector<GameEvent>& events, const Simulation& sim) {
    const int local = sim.localPlayerId();
    const auto& players = sim.players();
    auto positionOf = [&](int id) {
        return id >= 0 && id < static_cast<int>(players.size()) ? players[static_cast<size_t>(id)].position : glm::vec3(0.0f);
    };
    std::uniform_int_distribution<size_t> pick3(0, 2);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (const GameEvent& e : events) {
        const bool isLocal = e.a == local;
        switch (e.type) {
        case GameEvent::Type::Fired: {
            const auto category = static_cast<size_t>(e.category);
            SoundId id = category < m_shotSets.size() ? pick(m_shotSets[category]) : shotSound(e.category);
            float volume = isLocal ? 0.55f : 1.0f;
            // Far shots: a distant recording (or the muffled synthesized variant) instead of the close crack.
            const float distance = glm::distance(e.start, m_listener);
            if (!isLocal && distance > 35.0f) {
                if (!m_farSet.empty()) {
                    id = pick(m_farSet);
                    volume = 1.0f;
                } else if (category < m_farShots.size()) {
                    id = m_farShots[category];
                    volume = 1.4f;
                }
            }
            playAt(id, e.start, volume, 0.04f, isLocal);
            const bool manual = e.category == WeaponCategory::BoltRifle || e.category == WeaponCategory::Sniper ||
                                e.category == WeaponCategory::Shotgun;
            if (manual) {
                PlayParams cycle{.volume = isLocal ? 0.45f : 0.6f, .pitch = jitter(0.03f), .spatial = !isLocal, .position = e.start};
                schedule(0.35f, pick(m_boltSet), cycle);
            } else if (!m_casings.empty() && e.category != WeaponCategory::Melee && distance < 25.0f &&
                       unit(m_rng) < (isLocal ? 0.7f : 0.4f)) {
                // Brass hitting the ground next to the shooter.
                PlayParams casing{.volume = isLocal ? 0.35f : 0.5f, .pitch = jitter(0.08f), .spatial = !isLocal,
                                  .position = e.start - glm::vec3(0.0f, 1.4f, 0.0f), .minDistance = 1.0f, .maxDistance = 20.0f};
                schedule(0.45f + unit(m_rng) * 0.2f, pick(m_casings), casing);
            }
            break;
        }
        case GameEvent::Type::Shot:
            if (e.surface == 1) {
                playAt(m_impacts[pick3(m_rng)], e.end, 0.5f, 0.1f, false);
                if (unit(m_rng) < 0.12f) {
                    playAt(m_ricochets[pick3(m_rng)], e.end, 0.35f, 0.05f, false);
                }
            } else if (e.surface == 3) {
                playAt(m_propHit, e.end, 0.7f, 0.15f, false);
            }
            break;
        case GameEvent::Type::Hit:
            if (e.a == local) {
                play(e.headshot ? m_headshot : m_hitMarker, {.volume = e.headshot ? 0.45f : 0.5f});
            }
            if (e.b == local) {
                std::uniform_int_distribution<size_t> pick2(0, m_hurts.size() - 1);
                play(m_hurts[pick2(m_rng)], {.volume = 0.5f, .pitch = jitter(0.06f)});
            } else {
                playAt(pick(m_bodyHits), positionOf(e.b) + glm::vec3(0, 1.2f, 0), 0.7f, 0.1f, false);
            }
            break;
        case GameEvent::Type::Kill:
            if (e.a == local && e.b != local) {
                play(m_killPing, {.volume = 0.4f});
            }
            break;
        case GameEvent::Type::Reload: {
            const bool bolt = e.category == WeaponCategory::BoltRifle || e.category == WeaponCategory::Sniper;
            PlayParams params{.volume = isLocal ? 0.5f : 0.6f, .pitch = jitter(0.03f), .spatial = !isLocal, .position = e.start,
                              .minDistance = 1.5f, .maxDistance = 30.0f};
            if (bolt) {
                play(pick(m_boltSet), params);
            } else if (m_recordedReloads) {
                // One recorded magazine change (pistols have their own).
                const bool pistol = e.category == WeaponCategory::Pistol && !m_reloadPistolSet.empty();
                play(pick(pistol ? m_reloadPistolSet : m_reloadSet), params);
                break;
            } else {
                play(m_reloadOut, params);
            }
            const WeaponDef& weapon = sim.weaponFor(players[static_cast<size_t>(e.a)]);
            schedule(weapon.reloadSeconds * 0.7f, bolt ? pick(m_boltSet) : m_reloadIn, params);
            break;
        }
        case GameEvent::Type::MeleeSwing:
            playAt(pick(m_knives), e.start, isLocal ? 0.5f : 0.7f, 0.08f, isLocal);
            break;
        case GameEvent::Type::Jumped:
            playAt(m_jump, e.start, isLocal ? 0.25f : 0.35f, 0.1f, isLocal);
            break;
        case GameEvent::Type::Landed: {
            const float volume = std::clamp(e.magnitude / 12.0f, 0.15f, 1.0f);
            playAt(pick(m_lands), e.start, isLocal ? volume * 0.6f : volume, 0.1f, isLocal);
            break;
        }
        case GameEvent::Type::Promoted:
            if (isLocal) play(m_promote, {.volume = 0.45f});
            break;
        case GameEvent::Type::Demoted:
            if (isLocal) play(m_demote, {.volume = 0.5f});
            break;
        case GameEvent::Type::FinalTier:
            play(m_final, {.volume = isLocal ? 0.5f : 0.3f});
            break;
        case GameEvent::Type::Victory:
            play(m_victory, {.volume = 0.6f});
            break;
        case GameEvent::Type::TimeUp:
        case GameEvent::Type::DryFire:
        case GameEvent::Type::Spawned:
            break;
        }
    }
}

void GameAudio::update(float dt, const Simulation& sim, const glm::vec3& listener, const glm::vec3& forward) {
    m_listener = listener;
    m_audio.setListener(listener, forward, glm::vec3(0, 1, 0));

    for (Pending& pending : m_pending) {
        pending.delay -= dt;
        if (pending.delay <= 0.0f) {
            m_audio.play(pending.sound, pending.params);
        }
    }
    std::erase_if(m_pending, [](const Pending& p) { return p.delay <= 0.0f; });

    // Footsteps from horizontal movement.
    const auto& players = sim.players();
    m_stepDistance.resize(players.size(), 0.0f);
    for (const Player& p : players) {
        float& distance = m_stepDistance[static_cast<size_t>(p.id)];
        const float speed = glm::length(glm::vec2(p.velocity.x, p.velocity.z));
        if (!p.alive || !p.onGround || speed < 1.0f) {
            distance = std::min(distance, 1.2f);
            continue;
        }
        distance += speed * dt;
        const float stride = speed > 5.5f ? 2.6f : 2.0f;
        if (distance >= stride) {
            distance = 0.0f;
            const bool local = p.id == sim.localPlayerId();
            float volume = std::clamp(speed / 6.0f, 0.25f, 1.0f) * (p.crouched ? 0.35f : 1.0f);
            const bool wood = m_onWood && m_onWood(p.position);
            const std::vector<SoundId>& set = wood ? m_stepsWood : m_steps;
            playAt(set[std::uniform_int_distribution<size_t>(0, set.size() - 1)(m_rng)], p.position,
                   local ? volume * 0.35f : volume * 0.8f, 0.08f, local);
        }
    }

    // Ambient birds around the listener.
    if (m_ambience && !m_recordedAmbience) {
        m_birdTimer -= dt;
        if (m_birdTimer <= 0.0f) {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            m_birdTimer = 4.0f + unit(m_rng) * 9.0f;
            const float angle = unit(m_rng) * glm::two_pi<float>();
            const float radius = 25.0f + unit(m_rng) * 40.0f;
            const glm::vec3 position = listener + glm::vec3(std::cos(angle) * radius, 12.0f + unit(m_rng) * 10.0f, std::sin(angle) * radius);
            std::uniform_int_distribution<size_t> pickBird(0, m_birds.size() - 1);
            playAt(m_birds[pickBird(m_rng)], position, 0.5f, 0.1f, false);
        }
    }
}

} // namespace origon
