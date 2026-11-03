#include "audio/AudioSystem.h"

#include "core/Log.h"

#include <miniaudio.h>

#include <array>

namespace origon {
namespace {

constexpr int kVoiceCount = 48;
constexpr int kLoopCount = 4;

struct Voice {
    ma_audio_buffer buffer{};
    ma_sound sound{};
    bool initialized = false;
};

struct SoundData {
    std::string name;
    std::vector<float> samples;
};

} // namespace

struct AudioSystem::Impl {
    ma_engine engine{};
    bool engineReady = false;
    std::vector<SoundData> sounds;
    std::array<Voice, kVoiceCount> voices;
    std::array<Voice, kLoopCount> loops;
    int nextVoice = 0;
    int nextLoop = 0;

    void release(Voice& voice) {
        if (voice.initialized) {
            ma_sound_uninit(&voice.sound);
            ma_audio_buffer_uninit(&voice.buffer);
            voice.initialized = false;
        }
    }

    bool bind(Voice& voice, const SoundData& data) {
        release(voice);
        const ma_audio_buffer_config config =
            ma_audio_buffer_config_init(ma_format_f32, 1, data.samples.size(), data.samples.data(), nullptr);
        if (ma_audio_buffer_init(&config, &voice.buffer) != MA_SUCCESS) {
            return false;
        }
        if (ma_sound_init_from_data_source(&engine, &voice.buffer, 0, nullptr,
                                           &voice.sound) != MA_SUCCESS) {
            ma_audio_buffer_uninit(&voice.buffer);
            return false;
        }
        voice.initialized = true;
        return true;
    }
};

AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>()) {}

AudioSystem::~AudioSystem() {
    if (m_impl->engineReady) {
        for (Voice& voice : m_impl->voices) m_impl->release(voice);
        for (Voice& voice : m_impl->loops) m_impl->release(voice);
        ma_engine_uninit(&m_impl->engine);
    }
}

bool AudioSystem::init() {
    ma_engine_config config = ma_engine_config_init();
    config.sampleRate = kSampleRate;
    config.listenerCount = 1;
    if (ma_engine_init(&config, &m_impl->engine) != MA_SUCCESS) {
        log::warn("Audio: no output device, sound disabled");
        return false;
    }
    m_impl->engineReady = true;
    log::info("Audio: miniaudio engine at {} Hz", ma_engine_get_sample_rate(&m_impl->engine));
    return true;
}

bool AudioSystem::ready() const { return m_impl->engineReady; }

SoundId AudioSystem::add(const std::string& name, std::vector<float> samples) {
    m_impl->sounds.push_back({name, std::move(samples)});
    return static_cast<SoundId>(m_impl->sounds.size()) - 1;
}

SoundId AudioSystem::find(const std::string& name) const {
    for (size_t i = 0; i < m_impl->sounds.size(); ++i) {
        if (m_impl->sounds[i].name == name) {
            return static_cast<SoundId>(i);
        }
    }
    return kInvalidSound;
}

void AudioSystem::play(SoundId id, const PlayParams& params) {
    if (!m_impl->engineReady || id < 0 || id >= static_cast<SoundId>(m_impl->sounds.size())) {
        return;
    }
    // Prefer a finished voice; otherwise steal the oldest (round robin).
    int chosen = -1;
    for (int i = 0; i < kVoiceCount; ++i) {
        const int index = (m_impl->nextVoice + i) % kVoiceCount;
        Voice& voice = m_impl->voices[static_cast<size_t>(index)];
        if (!voice.initialized || !ma_sound_is_playing(&voice.sound)) {
            chosen = index;
            break;
        }
    }
    if (chosen < 0) {
        chosen = m_impl->nextVoice;
    }
    m_impl->nextVoice = (chosen + 1) % kVoiceCount;

    Voice& voice = m_impl->voices[static_cast<size_t>(chosen)];
    if (!m_impl->bind(voice, m_impl->sounds[static_cast<size_t>(id)])) {
        return;
    }
    ma_sound_set_volume(&voice.sound, params.volume);
    ma_sound_set_pitch(&voice.sound, params.pitch);
    ma_sound_set_spatialization_enabled(&voice.sound, params.spatial ? MA_TRUE : MA_FALSE);
    if (params.spatial) {
        ma_sound_set_position(&voice.sound, params.position.x, params.position.y, params.position.z);
        ma_sound_set_attenuation_model(&voice.sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&voice.sound, params.minDistance);
        ma_sound_set_max_distance(&voice.sound, params.maxDistance);
        ma_sound_set_rolloff(&voice.sound, 1.0f);
        ma_sound_set_doppler_factor(&voice.sound, 0.0f);
    }
    ma_sound_start(&voice.sound);
}

int AudioSystem::startLoop(SoundId id, float volume) {
    if (!m_impl->engineReady || id < 0 || id >= static_cast<SoundId>(m_impl->sounds.size()) ||
        m_impl->nextLoop >= kLoopCount) {
        return -1;
    }
    const int loop = m_impl->nextLoop++;
    Voice& voice = m_impl->loops[static_cast<size_t>(loop)];
    if (!m_impl->bind(voice, m_impl->sounds[static_cast<size_t>(id)])) {
        return -1;
    }
    ma_sound_set_spatialization_enabled(&voice.sound, MA_FALSE);
    ma_sound_set_looping(&voice.sound, MA_TRUE);
    ma_sound_set_volume(&voice.sound, volume);
    ma_sound_start(&voice.sound);
    return loop;
}

void AudioSystem::setLoopVolume(int loop, float volume) {
    if (loop >= 0 && loop < kLoopCount && m_impl->loops[static_cast<size_t>(loop)].initialized) {
        ma_sound_set_volume(&m_impl->loops[static_cast<size_t>(loop)].sound, volume);
    }
}

void AudioSystem::setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_impl->engineReady) {
        return;
    }
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
}

void AudioSystem::setMasterVolume(float volume) {
    if (m_impl->engineReady) {
        ma_engine_set_volume(&m_impl->engine, volume);
    }
}

} // namespace origon
