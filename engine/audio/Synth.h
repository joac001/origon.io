#pragma once

#include <cstdint>
#include <vector>

// Procedural sound synthesis. Every sound in the prototype is generated from code at startup,
// so the game ships without audio files. All buffers are mono float at AudioSystem::kSampleRate.
namespace origon::synth {

using Buffer = std::vector<float>;

struct GunshotParams {
    float crackCutoff = 6000.0f; // Initial brightness of the crack (Hz).
    float crackDecay = 0.025f;   // Seconds.
    float thumpFrequency = 70.0f;
    float thumpDecay = 0.08f;
    float thumpGain = 0.8f;
    float tailCutoff = 900.0f;
    float tailDecay = 0.35f;
    float tailGain = 0.35f;
    float echoDelay = 0.11f; // Outdoor slap-back echo.
    float echoGain = 0.25f;
    float duration = 1.2f;
    uint32_t seed = 1;
};

Buffer gunshot(const GunshotParams& params);
Buffer swish(float duration, float lowHz, float highHz, uint32_t seed);
Buffer thud(float frequency, float decay, float noiseCutoff, float noiseGain, uint32_t seed);
Buffer click(float frequency, float ring, float noiseGain, uint32_t seed);
Buffer footstep(float brightness, uint32_t seed);
Buffer ding(const std::vector<float>& partials, float decay);
Buffer ricochet(uint32_t seed);
Buffer grunt(float pitch, uint32_t seed);
// Notes in Hz, each lasting noteDuration (the last one rings longer).
Buffer jingle(const std::vector<float>& notes, float noteDuration, float brightness);
Buffer windLoop(float seconds, uint32_t seed);
// Babbling creek: band-passed noise with random gurgles, seamless loop.
Buffer streamLoop(float seconds, uint32_t seed);
Buffer birdCall(uint32_t seed);

// Mixes b into a starting at offsetSeconds (a grows if needed).
void mixInto(Buffer& a, const Buffer& b, float offsetSeconds, float gain);
void normalize(Buffer& buffer, float peak);

} // namespace origon::synth
