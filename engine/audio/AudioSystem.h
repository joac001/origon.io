#pragma once

#include <glm/vec3.hpp>

#include <memory>
#include <string>
#include <vector>

namespace origon {

using SoundId = int;
constexpr SoundId kInvalidSound = -1;

struct PlayParams {
    float volume = 1.0f;
    float pitch = 1.0f;
    // 3D playback when spatial is true. Distances in meters.
    bool spatial = false;
    glm::vec3 position{0.0f};
    float minDistance = 2.0f;
    float maxDistance = 120.0f;
};

// Sound playback on top of miniaudio: in-memory mono buffers, a voice pool, 3D listener.
class AudioSystem {
public:
    static constexpr int kSampleRate = 48000;

    AudioSystem();
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool init();
    bool ready() const;

    // Takes a mono float buffer at kSampleRate.
    SoundId add(const std::string& name, std::vector<float> samples);
    SoundId find(const std::string& name) const;
    // Writes every sound as 16-bit PCM WAV into a folder (sound design review).
    void dumpWav(const std::string& folder) const;

    void play(SoundId id, const PlayParams& params = {});
    // Starts a looping sound on a dedicated voice. Returns a loop handle.
    int startLoop(SoundId id, float volume);
    void setLoopVolume(int loop, float volume);

    void setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
    void setMasterVolume(float volume);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace origon
