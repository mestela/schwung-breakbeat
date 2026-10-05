#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

extern "C" {
#include "../../src/dsp/grain_stretch.h"
}

namespace breakbeat {

struct Sample {
    std::vector<float> left;
    std::vector<float> right;
    double sampleRate = 44100.0;
    int64_t frames() const { return static_cast<int64_t>(left.size()); }
    bool valid() const { return !left.empty() && right.size() == left.size(); }
};

struct Settings {
    float complexity = 0.5f;
    std::array<float, 8> anchors {};
    std::array<float, 8> retrig {};
    float bChance = 0.0f;
    int phraseBars = 0;
    std::array<float, 2> lengthBars {1.0f, 1.0f};
    std::array<float, 2> volume {1.0f, 1.0f};
    std::array<int, 2> sliceMode {};
    std::array<std::array<uint32_t, 8>, 2> markers {};
    int attackMs = 0;
    int decayMs = 0;
    bool earlyReverse = false;
    float stretchChance = 0.0f;
    std::array<int, 2> stretchLength {0, 0};
    std::array<int, 2> stretchSpan {0, 0};
    std::array<int, 2> stretchPitch {50, 50};
    int grainFx = 0;
    int grainCycleMs = 40;
    bool pitchLock = false;
};

struct Timing {
    double bpm = 120.0;
    double ppq = 0.0;
    double sampleRate = 44100.0;
    bool playing = false;
};

class Engine {
public:
    Engine();
    void reset();
    void setSample(int loop, std::shared_ptr<const Sample> sample);
    void triggerPad(int pad, const Settings& settings);
    void process(float* left, float* right, int frames,
                 const Timing& timing, const Settings& settings);
    int activeSlice() const { return currentSlice; }
    int activeLoop() const { return currentLoop; }

private:
    std::array<std::shared_ptr<const Sample>, 2> samples;
    std::array<std::array<uint32_t, 8>, 2> starts {};
    std::array<std::array<uint32_t, 8>, 2> lengths {};
    uint32_t rng = 0x4d595df4u;
    bb_grain_t grain {};
    double playPos = 0.0;
    double fallbackPpq = 0.0;
    int64_t lastSlot = -1;
    int64_t lastBar = -1;
    int currentLoop = 0;
    int currentSlice = 0;
    int retrigDivisions = 1;
    int retrigVariant = 0;
    int retrigCounter = 0;
    int stretchLength = 1;
    int stretchSpan = 1;
    int stretchRemaining = 0;
    float stretchPitchRatio = 1.0f;
    int64_t slotFrame = 0;
    int64_t envelopeFrame = 0;
    int64_t envelopeTotal = 1;
    double slotFrames = 1.0;
    double sourceRate = 1.0;
    std::shared_ptr<const Sample> playingSample;
    bool manualPad = false;
    bool active = false;
    bool wasPlaying = false;

    uint32_t nextRandom();
    float random01();
    int randomRange(int lo, int hi);
    void placeSlices(int loop, const Settings& settings);
    void startSlot(int64_t slot, int64_t bar, double slotFrames,
                   const Settings& settings);
    void startSlice(int position, double slotFrames, const Settings& settings);
    float read(const Sample& sample, int channel, double position) const;
    static int mapPercent(int value, int lo, int hi);
};

} // namespace breakbeat
