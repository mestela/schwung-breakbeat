#pragma once
#include "BreakbeatEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

class BreakbeatProcessor final : public juce::AudioProcessor {
public:
    BreakbeatProcessor();
    ~BreakbeatProcessor() override;
    const juce::String getName() const override { return "Schwung Breakbeat"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState state;
    void loadSample(int loop, const juce::File& file);
    juce::String samplePath(int loop) const;
    std::shared_ptr<const breakbeat::Sample> sampleForDisplay(int loop) const;
    int currentSlice() const { return shownSlice.load(); }
    int currentLoop() const { return shownLoop.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    breakbeat::Settings readSettings() const;
    breakbeat::Engine engine;
    juce::AudioFormatManager formats;
    std::array<std::shared_ptr<const breakbeat::Sample>, 2> displayed;
    std::array<juce::String, 2> paths;
    mutable std::mutex pathMutex;
    std::atomic<int> shownSlice {0}, shownLoop {0};
    std::atomic<bool> shuttingDown {false};
    std::thread loader;
    std::mutex requestMutex;
    std::condition_variable requestReady;
    std::array<std::optional<juce::String>, 2> pendingPaths;
    void loaderLoop();
    void loadOnWorker(int loop, juce::String path);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreakbeatProcessor)
};
