#include "BreakbeatEngine.h"
#include <cmath>
#include <iostream>
#include <memory>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " << #condition << " at line " << __LINE__ << '\n'; \
    return 1; }} while (false)

int main() {
    breakbeat::Engine engine;
    auto sample = std::make_shared<breakbeat::Sample>();
    sample->sampleRate = 8000;
    sample->left.resize(800);
    sample->right.resize(800);
    for (int i = 0; i < 800; ++i) sample->left[i] = sample->right[i] = (i % 100) / 100.0f;
    engine.setSample(0, sample);
    breakbeat::Settings settings;
    settings.complexity = 0;
    breakbeat::Timing timing {120, 0, 8000, true};
    float left[64], right[64];
    engine.process(left, right, 64, timing, settings);
    CHECK(engine.activeSlice() == 0);
    CHECK(left[0] == 0);
    CHECK(left[40] > 0.0f && right[40] == left[40]);
    settings.sliceMode[0] = 1;
    settings.markers[0][0] = 50000;
    settings.markers[0][1] = 150000;
    for (int i = 2; i < 8; ++i) settings.markers[0][i] = i * 125000;
    engine.triggerPad(0, settings);
    timing.playing = false;
    engine.process(left, right, 64, timing, settings);
    CHECK(std::abs(left[0] - 0.4f) < 0.001f); // manual start at frame 40
    settings.attackMs = 5;
    engine.triggerPad(0, settings);
    engine.process(left, right, 64, timing, settings);
    CHECK(left[0] == 0.0f);
    CHECK(left[20] > 0.0f);

    auto earlyEnd = [&](bool reverse) {
        breakbeat::Engine test;
        test.setSample(0, sample);
        breakbeat::Settings s;
        s.complexity = 0;
        s.sliceMode[0] = 1;
        s.markers[0][1] = 100000; // first slice ends before the grid slot
        for (int i = 2; i < 8; ++i) s.markers[0][i] = i * 125000;
        s.earlyReverse = reverse;
        float l[2000], r[2000];
        test.process(l, r, 2000, {120, 0, 8000, true}, s);
        return l[1800];
    };
    CHECK(earlyEnd(false) == 0.0f);
    CHECK(earlyEnd(true) > 0.0f);

    breakbeat::Engine phrase;
    auto other = std::make_shared<breakbeat::Sample>(*sample);
    std::fill(other->left.begin(), other->left.end(), .7f);
    std::fill(other->right.begin(), other->right.end(), .7f);
    phrase.setSample(0, sample);
    phrase.setSample(1, other);
    breakbeat::Settings phraseSettings;
    phraseSettings.phraseBars = 1;
    phraseSettings.bChance = 1;
    phraseSettings.complexity = 0;
    phrase.process(left, right, 64, {120, 0, 8000, true}, phraseSettings);
    CHECK(phrase.activeLoop() == 1 && left[0] == .7f);
    return 0;
}
