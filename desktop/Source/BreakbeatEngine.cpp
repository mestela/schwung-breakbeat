#include "BreakbeatEngine.h"
#include <algorithm>
#include <cmath>

extern "C" {
#include "../../src/dsp/slice_select.h"
#include "../../src/dsp/manual_slices.h"
}

namespace breakbeat {
namespace {
double clampBpm(double bpm) { return std::clamp(bpm, 20.0, 300.0); }
}

Engine::Engine() { reset(); }
void Engine::reset() {
    playPos = fallbackPpq = 0.0;
    lastSlot = lastBar = -1;
    currentLoop = currentSlice = 0;
    retrigDivisions = 1;
    retrigVariant = retrigCounter = 0;
    stretchLength = stretchSpan = 1;
    stretchRemaining = 0;
    stretchPitchRatio = 1.0f;
    slotFrame = envelopeFrame = 0;
    envelopeTotal = 1;
    manualPad = active = wasPlaying = false;
    playingSample.reset();
    bb_grain_reset(&grain);
}
void Engine::setSample(int loop, std::shared_ptr<const Sample> sample) {
    if (loop >= 0 && loop < 2) std::atomic_store(&samples[loop], std::move(sample));
}
uint32_t Engine::nextRandom() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
float Engine::random01() { return static_cast<float>(nextRandom() >> 8) / 16777216.0f; }
int Engine::randomRange(int lo, int hi) {
    if (hi < lo) std::swap(lo, hi);
    return lo + static_cast<int>(nextRandom() % static_cast<uint32_t>(hi - lo + 1));
}
int Engine::mapPercent(int value, int lo, int hi) {
    return lo + (std::clamp(value, 0, 100) * (hi - lo) + 50) / 100;
}
void Engine::placeSlices(int loop, const Settings& settings) {
    auto sample = std::atomic_load(&samples[loop]);
    auto count = sample && sample->valid() ? static_cast<uint32_t>(std::min<int64_t>(sample->frames(), UINT32_MAX)) : 0u;
    bb_place_slices(count, settings.sliceMode[loop], settings.markers[loop].data(),
                    starts[loop].data(), lengths[loop].data());
}
void Engine::triggerPad(int pad, const Settings& settings) {
    if (pad < 0 || pad >= 16) return;
    currentLoop = pad / 8;
    currentSlice = pad % 8;
    placeSlices(currentLoop, settings);
    playingSample = std::atomic_load(&samples[currentLoop]);
    if (!playingSample || !playingSample->valid()) return;
    manualPad = active = true;
    stretchLength = stretchSpan = 1;
    retrigDivisions = 1;
    playPos = starts[currentLoop][currentSlice];
    slotFrame = envelopeFrame = 0;
    envelopeTotal = static_cast<int64_t>(std::max(1.0, slotFrames));
    bb_grain_reset(&grain);
}
void Engine::startSlice(int position, double duration, const Settings& settings) {
    slice_inputs_t input {};
    input.beat_position = position;
    input.complexity = settings.complexity;
    std::copy(settings.anchors.begin(), settings.anchors.end(), input.anchors);
    currentSlice = slice_select_next(&input, [](void* ctx) -> float {
        return static_cast<Engine*>(ctx)->random01();
    }, this);
    slotFrames = duration;
    slotFrame = envelopeFrame = 0;
    retrigDivisions = 1;
    retrigVariant = retrigCounter = 0;
    stretchLength = stretchSpan = 1;
    stretchPitchRatio = 1.0f;
    if (random01() < settings.stretchChance) {
        stretchLength = randomRange(mapPercent(settings.stretchLength[0], 2, 16),
                                    mapPercent(settings.stretchLength[1], 2, 16));
        stretchSpan = randomRange(mapPercent(settings.stretchSpan[0], 1, 8),
                                  mapPercent(settings.stretchSpan[1], 1, 8));
        stretchRemaining = stretchSpan - 1;
        int semitones = randomRange(mapPercent(settings.stretchPitch[0], -12, 12),
                                    mapPercent(settings.stretchPitch[1], -12, 12));
        stretchPitchRatio = std::pow(2.0f, semitones / 12.0f);
    } else {
        stretchRemaining = 0;
        static constexpr int divisions[8] {2, 3, 4, 8, 16, 32, 2, 2};
        int fired[8];
        int count = 0;
        float triggersPerBar = 8.0f / std::max(0.125f, settings.lengthBars[currentLoop]);
        for (int i = 0; i < 8; ++i) {
            float p = std::clamp(settings.retrig[i], 0.0f, 1.0f);
            float chance = 1.0f - std::pow(1.0f - p, 1.0f / triggersPerBar);
            if (random01() < chance) fired[count++] = i;
        }
        if (count) {
            int selected = fired[nextRandom() % static_cast<uint32_t>(count)];
            retrigDivisions = divisions[selected];
            retrigVariant = selected == 6 ? 1 : selected == 7 ? 2 : 0;
        }
    }
    playPos = starts[currentLoop][currentSlice];
    sourceRate = playingSample ? ((playingSample->frames() / 8.0) * playingSample->sampleRate /
                  (std::max(1.0, duration) * 44100.0 * stretchLength)) : 1.0;
    envelopeTotal = static_cast<int64_t>(std::max(1.0, duration * stretchSpan));
    if (retrigDivisions > 1)
        envelopeTotal = static_cast<int64_t>(std::max(1.0, duration * (retrigVariant == 1 ? .25 : retrigVariant == 2 ? .75 : 1.0 / retrigDivisions)));
    bb_grain_reset(&grain);
    active = true;
}
void Engine::startSlot(int64_t slot, int64_t bar, double duration, const Settings& settings) {
    (void)bar;
    if (!std::atomic_load(&samples[currentLoop])) currentLoop = 0;
    placeSlices(currentLoop, settings);
    playingSample = std::atomic_load(&samples[currentLoop]);
    if (!playingSample || !playingSample->valid()) { active = false; return; }
    manualPad = false;
    if (stretchRemaining > 0) {
        --stretchRemaining;
        return;
    }
    startSlice(static_cast<int>(slot & 7), duration, settings);
}
float Engine::read(const Sample& sample, int channel, double position) const {
    if (position < 0.0 || position >= sample.frames()) return 0.0f;
    const auto& data = channel ? sample.right : sample.left;
    auto i = static_cast<size_t>(position);
    float frac = static_cast<float>(position - i);
    return data[i] + ((i + 1 < data.size() ? data[i + 1] : data[i]) - data[i]) * frac;
}
void Engine::process(float* left, float* right, int frames, const Timing& timing, const Settings& settings) {
    std::fill_n(left, frames, 0.0f);
    std::fill_n(right, frames, 0.0f);
    if (frames <= 0 || timing.sampleRate <= 0.0) return;
    double bpm = clampBpm(timing.bpm);
    double beatsPerSample = bpm / (60.0 * timing.sampleRate);
    double startPpq = std::isfinite(timing.ppq) ? timing.ppq : fallbackPpq;
    if (!timing.playing && wasPlaying) { lastSlot = -1; if (!manualPad) active = false; }
    if (timing.playing && !wasPlaying) lastSlot = lastBar = -1;
    wasPlaying = timing.playing;
    for (int i = 0; i < frames; ++i) {
        double ppq = startPpq + i * beatsPerSample;
        if (timing.playing) {
            int64_t bar = static_cast<int64_t>(std::floor(ppq / 4.0));
            if (bar != lastBar) {
                int wanted = settings.phraseBars > 0 && bar % settings.phraseBars == settings.phraseBars - 1 &&
                             random01() < settings.bChance ? 1 : 0;
                if (wanted != currentLoop) { stretchRemaining = 0; active = false; }
                currentLoop = wanted;
                lastBar = bar;
            }
            double durationBeats = std::max(.0625, static_cast<double>(settings.lengthBars[currentLoop]) * .5);
            int64_t slot = static_cast<int64_t>(std::floor(ppq / durationBeats));
            if (slot != lastSlot) {
                if (lastSlot >= 0 && slot != lastSlot + 1) { active = false; stretchRemaining = 0; }
                double newDuration = timing.sampleRate * 60.0 * durationBeats / bpm;
                startSlot(slot, bar, newDuration, settings);
                lastSlot = slot;
            }
        }
        if (!active || !playingSample || !playingSample->valid()) continue;
        auto& sample = *playingSample;
        uint32_t start = starts[currentLoop][currentSlice];
        uint32_t length = lengths[currentLoop][currentSlice];
        if (!length) continue;
        double rate = (sample.frames() / 8.0) * sample.sampleRate /
                      (std::max(1.0, slotFrames) * timing.sampleRate * stretchLength);
        bool grainOn = settings.pitchLock || settings.grainFx > 0 || stretchLength > 1;
        bool ordinary = !manualPad && retrigDivisions == 1 && stretchLength == 1;
        if (ordinary && playPos >= start + length && !settings.earlyReverse) continue;
        double pos = playPos;
        if (ordinary && settings.earlyReverse)
            pos = bb_reflected_frame(start, length, static_cast<float>(playPos));
        if (manualPad && playPos >= start + length)
            pos = playPos = start + std::fmod(playPos - start, static_cast<double>(length));
        float l = 0, r = 0;
        if (grainOn) {
            int cycle = static_cast<int>(settings.grainCycleMs * timing.sampleRate / 1000.0);
            auto frame = bb_grain_next(&grain, static_cast<float>(playPos), static_cast<float>(rate), cycle,
                                       settings.pitchLock || stretchLength > 1, stretchPitchRatio, settings.grainFx);
            auto wrap = [&](float p) {
                float distance = std::fmod(p - start, static_cast<float>(length));
                if (distance < 0) distance += length;
                return start + distance;
            };
            l = read(sample, 0, wrap(frame.current_pos)) * frame.current_gain;
            r = read(sample, 1, wrap(frame.current_pos)) * frame.current_gain;
            if (frame.previous_gain > 0) {
                l += read(sample, 0, wrap(frame.previous_pos)) * frame.previous_gain;
                r += read(sample, 1, wrap(frame.previous_pos)) * frame.previous_gain;
            }
        } else { l = read(sample, 0, pos); r = read(sample, 1, pos); }
        float gain = settings.volume[currentLoop];
        auto attack = static_cast<int64_t>(settings.attackMs * timing.sampleRate / 1000.0);
        auto decay = static_cast<int64_t>(settings.decayMs * timing.sampleRate / 1000.0);
        if (attack > 0 && envelopeFrame < attack) gain *= static_cast<float>(envelopeFrame) / attack;
        if (decay > 0) gain *= std::clamp(static_cast<float>(envelopeTotal - envelopeFrame) / decay, 0.0f, 1.0f);
        left[i] = l * gain;
        right[i] = r * gain;
        ++envelopeFrame;
        ++slotFrame;
        if (retrigDivisions > 1 && retrigCounter < retrigDivisions - 1) {
            double threshold = retrigVariant ? slotFrames * (retrigVariant == 1 ? .25 : .75)
                                              : length / static_cast<double>(retrigDivisions);
            bool due = retrigVariant ? slotFrame >= threshold : playPos - start >= threshold;
            if (due) {
                ++retrigCounter;
                playPos = start;
                envelopeFrame = 0;
                envelopeTotal = static_cast<int64_t>(std::max(1.0, slotFrames * (retrigVariant ?
                            (retrigVariant == 1 ? .75 : .25) : 1.0 / retrigDivisions)));
                bb_grain_reset(&grain);
            }
        }
        playPos += rate;
    }
    fallbackPpq = timing.playing ? startPpq + frames * beatsPerSample : 0.0;
}
} // namespace breakbeat
