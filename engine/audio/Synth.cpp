#include "audio/Synth.h"

#include "audio/AudioSystem.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace origon::synth {
namespace {

constexpr float kRate = static_cast<float>(AudioSystem::kSampleRate);
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed * 2654435761u + 0x9E3779B9u) {}
    float next() { // -1..1
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) / 2147483648.0f - 1.0f;
    }
    float unit() { return next() * 0.5f + 0.5f; }
};

// One-pole low-pass filter.
struct LowPass {
    float y = 0.0f;
    float process(float x, float cutoff) {
        const float a = 1.0f - std::exp(-kTwoPi * std::max(cutoff, 10.0f) / kRate);
        y += a * (x - y);
        return y;
    }
};

size_t samplesFor(float seconds) { return static_cast<size_t>(std::max(seconds, 0.0f) * kRate); }

float softClip(float x) { return std::tanh(x); }

} // namespace

void mixInto(Buffer& a, const Buffer& b, float offsetSeconds, float gain) {
    const size_t offset = samplesFor(offsetSeconds);
    if (a.size() < offset + b.size()) {
        a.resize(offset + b.size(), 0.0f);
    }
    for (size_t i = 0; i < b.size(); ++i) {
        a[offset + i] += b[i] * gain;
    }
}

void normalize(Buffer& buffer, float peak) {
    float maxValue = 0.0f;
    for (float s : buffer) maxValue = std::max(maxValue, std::abs(s));
    if (maxValue > 1e-6f) {
        const float scale = peak / maxValue;
        for (float& s : buffer) s *= scale;
    }
}

Buffer gunshot(const GunshotParams& p) {
    Buffer out(samplesFor(p.duration), 0.0f);
    Rng rng(p.seed);
    LowPass crack1, crack2, tail1, tail2;
    float phase = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float noise = rng.next();
        const float cutoff = p.crackCutoff * std::exp(-t / (p.crackDecay * 4.0f)) + 250.0f;
        const float crack = crack2.process(crack1.process(noise, cutoff), cutoff * 1.5f) * std::exp(-t / p.crackDecay) * 2.2f * p.crackGain;
        const float frequency = p.thumpFrequency * (1.0f + 2.5f * std::exp(-t / 0.012f));
        phase += kTwoPi * frequency / kRate;
        const float thump = std::sin(phase) * std::exp(-t / p.thumpDecay) * p.thumpGain;
        const float attack = std::min(1.0f, t / 0.01f);
        const float tail = tail2.process(tail1.process(noise, p.tailCutoff), p.tailCutoff) * std::exp(-t / p.tailDecay) *
                           p.tailGain * attack * 3.0f;
        out[i] = crack + thump + tail;
    }
    // Slap-back echo, darker than the direct sound.
    Buffer echo(out.size(), 0.0f);
    LowPass echoFilter;
    for (size_t i = 0; i < out.size(); ++i) {
        echo[i] = echoFilter.process(out[i], 1500.0f);
    }
    mixInto(out, echo, p.echoDelay, p.echoGain);
    mixInto(out, echo, p.echoDelay * 2.3f, p.echoGain * 0.4f);
    out.resize(samplesFor(p.duration));
    for (float& s : out) s = softClip(s * 1.2f);
    // Fade out the last 10 ms to avoid a click.
    const size_t fade = std::min<size_t>(out.size(), samplesFor(0.01f));
    for (size_t i = 0; i < fade; ++i) out[out.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
    normalize(out, 0.9f);
    return out;
}

Buffer swish(float duration, float lowHz, float highHz, uint32_t seed) {
    Buffer out(samplesFor(duration), 0.0f);
    Rng rng(seed);
    LowPass lp, hp;
    for (size_t i = 0; i < out.size(); ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(out.size());
        const float center = lowHz + (highHz - lowHz) * x;
        const float noise = rng.next();
        const float low = lp.process(noise, center * 1.4f);
        const float band = low - hp.process(low, center * 0.6f);
        const float envelope = std::sin(x * std::numbers::pi_v<float>);
        out[i] = band * envelope * envelope;
    }
    normalize(out, 0.8f);
    return out;
}

Buffer thud(float frequency, float decay, float noiseCutoff, float noiseGain, uint32_t seed) {
    Buffer out(samplesFor(decay * 6.0f + 0.02f), 0.0f);
    Rng rng(seed);
    LowPass lp;
    float phase = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        phase += kTwoPi * frequency * (1.0f + 0.6f * std::exp(-t / 0.01f)) / kRate;
        const float body = std::sin(phase) * std::exp(-t / decay);
        const float noise = lp.process(rng.next(), noiseCutoff) * std::exp(-t / (decay * 0.6f)) * noiseGain * 3.0f;
        out[i] = body + noise;
    }
    normalize(out, 0.85f);
    return out;
}

Buffer click(float frequency, float ring, float noiseGain, uint32_t seed) {
    Buffer out(samplesFor(ring * 6.0f + 0.01f), 0.0f);
    Rng rng(seed);
    LowPass lp;
    for (size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float noise = rng.next();
        const float high = noise - lp.process(noise, 2500.0f);
        const float tick = high * std::exp(-t / 0.004f) * noiseGain;
        const float tone = (std::sin(kTwoPi * frequency * t) + 0.5f * std::sin(kTwoPi * frequency * 2.7f * t)) *
                           std::exp(-t / ring) * 0.5f;
        out[i] = tick + tone;
    }
    normalize(out, 0.8f);
    return out;
}

Buffer footstep(float brightness, uint32_t seed) {
    Buffer out(samplesFor(0.16f), 0.0f);
    Rng rng(seed);
    LowPass lp1, lp2;
    float phase = 0.0f;
    const float cutoff = 900.0f + 1800.0f * brightness + 300.0f * rng.unit();
    for (size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        // Heel then toe: two short noise grains.
        const float grainA = std::exp(-t / 0.018f) * std::min(1.0f, t / 0.002f);
        const float t2 = t - 0.035f - 0.01f * rng.unit() * 0.0f;
        const float grainB = t2 > 0.0f ? std::exp(-t2 / 0.025f) * 0.6f : 0.0f;
        const float noise = lp2.process(lp1.process(rng.next(), cutoff), cutoff);
        phase += kTwoPi * 85.0f / kRate;
        out[i] = noise * (grainA + grainB) * 3.0f + std::sin(phase) * std::exp(-t / 0.03f) * 0.35f;
    }
    normalize(out, 0.7f);
    return out;
}

Buffer ding(const std::vector<float>& partials, float decay) {
    Buffer out(samplesFor(decay * 5.0f), 0.0f);
    for (size_t i = 0; i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        float sum = 0.0f;
        float gain = 1.0f;
        for (float f : partials) {
            sum += std::sin(kTwoPi * f * t) * gain * std::exp(-t / (decay * gain));
            gain *= 0.55f;
        }
        out[i] = sum * std::min(1.0f, t / 0.002f);
    }
    normalize(out, 0.7f);
    return out;
}

Buffer ricochet(uint32_t seed) {
    Rng rng(seed);
    const float start = 2600.0f + rng.unit() * 1600.0f;
    const float end = start * (0.35f + 0.2f * rng.unit());
    const float duration = 0.25f + 0.2f * rng.unit();
    Buffer out(samplesFor(duration), 0.0f);
    float phase = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(out.size());
        const float t = static_cast<float>(i) / kRate;
        const float frequency = start + (end - start) * x + 60.0f * std::sin(kTwoPi * 30.0f * t);
        phase += kTwoPi * frequency / kRate;
        out[i] = std::sin(phase) * (1.0f - x) * std::min(1.0f, t / 0.005f) * 0.6f + rng.next() * 0.04f * (1.0f - x);
    }
    normalize(out, 0.5f);
    return out;
}

Buffer grunt(float pitch, uint32_t seed) {
    Rng rng(seed);
    Buffer out(samplesFor(0.22f), 0.0f);
    LowPass formant1, formant2;
    float phase = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(out.size());
        const float frequency = pitch * (1.1f - 0.3f * x);
        phase += frequency / kRate;
        const float saw = 2.0f * (phase - std::floor(phase)) - 1.0f + rng.next() * 0.3f;
        const float voiced = formant2.process(formant1.process(saw, 700.0f), 1100.0f);
        const float envelope = std::sin(std::min(x * 1.3f, 1.0f) * std::numbers::pi_v<float>);
        out[i] = voiced * envelope;
    }
    normalize(out, 0.6f);
    return out;
}

Buffer jingle(const std::vector<float>& notes, float noteDuration, float brightness) {
    Buffer out;
    for (size_t n = 0; n < notes.size(); ++n) {
        const bool last = n + 1 == notes.size();
        const float length = last ? noteDuration * 3.5f : noteDuration * 1.6f;
        Buffer note(samplesFor(length), 0.0f);
        for (size_t i = 0; i < note.size(); ++i) {
            const float t = static_cast<float>(i) / kRate;
            const float f = notes[n];
            // Triangle-ish tone with optional brightness from odd harmonics.
            const float tone = std::sin(kTwoPi * f * t) + brightness * 0.3f * std::sin(kTwoPi * 3.0f * f * t) +
                               brightness * 0.12f * std::sin(kTwoPi * 5.0f * f * t);
            note[i] = tone * std::exp(-t / (length * 0.35f)) * std::min(1.0f, t / 0.004f);
        }
        mixInto(out, note, noteDuration * static_cast<float>(n), 1.0f);
    }
    normalize(out, 0.6f);
    return out;
}

Buffer windLoop(float seconds, uint32_t seed) {
    const float fadeSeconds = 1.0f;
    Buffer raw(samplesFor(seconds + fadeSeconds), 0.0f);
    Rng rng(seed);
    LowPass lp1, lp2, hp;
    for (size_t i = 0; i < raw.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float gust = 0.55f + 0.25f * std::sin(kTwoPi * 0.11f * t) + 0.2f * std::sin(kTwoPi * 0.037f * t + 1.3f);
        const float cutoff = 250.0f + 450.0f * gust;
        const float low = lp2.process(lp1.process(rng.next(), cutoff), cutoff);
        const float leaves = (rng.next() - hp.process(rng.next(), 3000.0f)) * 0.02f * gust * gust;
        raw[i] = low * gust * 2.5f + leaves;
    }
    // Crossfade the tail into the head so the loop is seamless.
    const size_t loopLength = samplesFor(seconds);
    const size_t fade = samplesFor(fadeSeconds);
    Buffer out(raw.begin(), raw.begin() + static_cast<long>(loopLength));
    for (size_t i = 0; i < fade; ++i) {
        const float w = static_cast<float>(i) / static_cast<float>(fade);
        out[i] = out[i] * w + raw[loopLength + i] * (1.0f - w);
    }
    normalize(out, 0.5f);
    return out;
}

Buffer streamLoop(float seconds, uint32_t seed) {
    const float fadeSeconds = 1.0f;
    Buffer raw(samplesFor(seconds + fadeSeconds), 0.0f);
    Rng rng(seed);
    LowPass lp1, lp2, hp;
    for (size_t i = 0; i < raw.size(); ++i) {
        const float noise = rng.next();
        const float low = lp2.process(lp1.process(noise, 1800.0f), 2200.0f);
        const float band = low - hp.process(low, 350.0f);
        raw[i] = band * 1.6f;
    }
    // Gurgles: short resonant chirps scattered through the loop.
    const int gurgles = static_cast<int>(seconds * 14.0f);
    for (int g = 0; g < gurgles; ++g) {
        const float start = rng.unit() * seconds;
        const float frequency = 300.0f + rng.unit() * 900.0f;
        const float length = 0.03f + rng.unit() * 0.06f;
        Buffer chirp(samplesFor(length), 0.0f);
        float phase = 0.0f;
        for (size_t i = 0; i < chirp.size(); ++i) {
            const float x = static_cast<float>(i) / static_cast<float>(chirp.size());
            phase += kTwoPi * frequency * (1.0f + 0.6f * x) / kRate;
            chirp[i] = std::sin(phase) * std::sin(x * std::numbers::pi_v<float>) * 0.25f;
        }
        mixInto(raw, chirp, start, 1.0f);
    }
    const size_t loopLength = samplesFor(seconds);
    const size_t fade = samplesFor(fadeSeconds);
    raw.resize(std::max(raw.size(), loopLength + fade), 0.0f);
    Buffer out(raw.begin(), raw.begin() + static_cast<long>(loopLength));
    for (size_t i = 0; i < fade; ++i) {
        const float w = static_cast<float>(i) / static_cast<float>(fade);
        out[i] = out[i] * w + raw[loopLength + i] * (1.0f - w);
    }
    normalize(out, 0.5f);
    return out;
}

Buffer distant(const Buffer& in, uint32_t seed) {
    Rng rng(seed);
    Buffer dry(in.size(), 0.0f);
    LowPass lp1, lp2;
    for (size_t i = 0; i < in.size(); ++i) dry[i] = lp2.process(lp1.process(in[i], 700.0f), 900.0f);
    Buffer out = dry;
    // Rolling echoes off hills and trees, each one darker and quieter.
    float delay = 0.18f;
    float gain = 0.55f;
    for (int e = 0; e < 7; ++e) {
        mixInto(out, dry, delay, gain);
        delay += 0.12f + rng.unit() * 0.18f;
        gain *= 0.62f;
    }
    LowPass smooth;
    for (float& s : out) s = smooth.process(s, 1200.0f);
    const size_t fade = std::min<size_t>(out.size(), samplesFor(0.2f));
    for (size_t i = 0; i < fade; ++i) out[out.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
    normalize(out, 0.8f);
    return out;
}

Buffer birdCall(uint32_t seed) {
    Rng rng(seed);
    Buffer out;
    const int chirps = 2 + static_cast<int>(rng.unit() * 4.0f);
    const float base = 2400.0f + rng.unit() * 2000.0f;
    float offset = 0.0f;
    for (int c = 0; c < chirps; ++c) {
        const float duration = 0.05f + rng.unit() * 0.08f;
        const float sweep = (rng.unit() - 0.3f) * 1800.0f;
        Buffer chirp(samplesFor(duration), 0.0f);
        float phase = 0.0f;
        for (size_t i = 0; i < chirp.size(); ++i) {
            const float x = static_cast<float>(i) / static_cast<float>(chirp.size());
            phase += kTwoPi * (base + sweep * x + 200.0f * std::sin(x * 40.0f)) / kRate;
            chirp[i] = std::sin(phase) * std::sin(x * std::numbers::pi_v<float>);
        }
        mixInto(out, chirp, offset, 1.0f);
        offset += duration + 0.03f + rng.unit() * 0.08f;
    }
    normalize(out, 0.4f);
    return out;
}

} // namespace origon::synth
