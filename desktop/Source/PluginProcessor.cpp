#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <limits>

namespace {
using APVTS = juce::AudioProcessorValueTreeState;
juce::String indexed(const char* prefix, int index) { return juce::String(prefix) + juce::String(index + 1); }
}

APVTS::ParameterLayout BreakbeatProcessor::makeParameters() {
    APVTS::ParameterLayout layout;
    auto add = [&](const juce::String& id, const juce::String& name, float lo, float hi, float step, float initial) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), name,
                   juce::NormalisableRange<float>(lo, hi, step), initial));
    };
    add("complexity", "Complexity", 0, 100, 1, 50);
    add("bChance", "B chance", 0, 100, 1, 0);
    add("phraseBars", "Phrase bars", 0, 16, 1, 0);
    for (int i = 0; i < 2; ++i) {
        auto p = i == 0 ? "A" : "B";
        add(juce::String(p) + "Length", juce::String(p) + " length (bars)", .125f, 4, .125f, 1);
        add(juce::String(p) + "Volume", juce::String(p) + " volume", 0, 200, 1, 100);
        add(juce::String(p) + "Manual", juce::String(p) + " manual slices", 0, 1, 1, 0);
        for (int j = 0; j < 8; ++j)
            add(juce::String(p) + "Marker" + juce::String(j + 1), juce::String(p) + " slice " + juce::String(j + 1),
                0, 1000000, 1, j * 125000);
    }
    for (int i = 0; i < 8; ++i) {
        add(indexed("anchor", i), "Anchor " + juce::String(i + 1), 0, 100, 1, 0);
        static const char* names[] {"2x", "3x", "4x", "8x", "16x", "32x", "Push", "Drag"};
        add(indexed("retrig", i), juce::String("Retrig ") + names[i], 0, 100, 1, 0);
    }
    add("attack", "Attack (ms)", 0, 250, 1, 0);
    add("decay", "Decay (ms)", 0, 250, 1, 0);
    add("reverse", "Early end reverse", 0, 1, 1, 0);
    add("stretchChance", "Stretch chance", 0, 100, 1, 0);
    for (auto* name : {"Length", "Span", "Pitch"})
        for (auto* end : {"Min", "Max"})
            add(juce::String("stretch") + name + end, juce::String("Stretch ") + name + " " + end,
                0, 100, 1, juce::String(name) == "Pitch" ? 50 : 0);
    add("grainFx", "Grain repeat", 0, 100, 1, 0);
    add("grainCycle", "Grain cycle (ms)", 10, 120, 1, 40);
    add("pitchLock", "Pitch lock", 0, 1, 1, 0);
    return layout;
}

BreakbeatProcessor::BreakbeatProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "Breakbeat", makeParameters()) {
    formats.registerBasicFormats();
    loader = std::thread([this] { loaderLoop(); });
}
BreakbeatProcessor::~BreakbeatProcessor() {
    shuttingDown = true;
    requestReady.notify_one();
    if (loader.joinable()) loader.join();
}
void BreakbeatProcessor::prepareToPlay(double, int) { engine.reset(); }
bool BreakbeatProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
breakbeat::Settings BreakbeatProcessor::readSettings() const {
    auto v = [&](const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    breakbeat::Settings s;
    s.complexity = v("complexity") / 100;
    s.bChance = v("bChance") / 100;
    s.phraseBars = static_cast<int>(v("phraseBars"));
    for (int i = 0; i < 2; ++i) {
        auto p = i ? "B" : "A";
        s.lengthBars[i] = v(juce::String(p) + "Length");
        s.volume[i] = v(juce::String(p) + "Volume") / 100;
        s.sliceMode[i] = v(juce::String(p) + "Manual") > .5f;
        for (int j = 0; j < 8; ++j)
            s.markers[i][j] = static_cast<uint32_t>(v(juce::String(p) + "Marker" + juce::String(j + 1)));
    }
    for (int i = 0; i < 8; ++i) {
        s.anchors[i] = v(indexed("anchor", i)) / 100;
        s.retrig[i] = v(indexed("retrig", i)) / 100;
    }
    s.attackMs = static_cast<int>(v("attack"));
    s.decayMs = static_cast<int>(v("decay"));
    s.earlyReverse = v("reverse") > .5f;
    s.stretchChance = v("stretchChance") / 100;
    s.stretchLength = {static_cast<int>(v("stretchLengthMin")), static_cast<int>(v("stretchLengthMax"))};
    s.stretchSpan = {static_cast<int>(v("stretchSpanMin")), static_cast<int>(v("stretchSpanMax"))};
    s.stretchPitch = {static_cast<int>(v("stretchPitchMin")), static_cast<int>(v("stretchPitchMax"))};
    s.grainFx = static_cast<int>(v("grainFx"));
    s.grainCycleMs = static_cast<int>(v("grainCycle"));
    s.pitchLock = v("pitchLock") > .5f;
    return s;
}
void BreakbeatProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;
    auto settings = readSettings();
    breakbeat::Timing timing;
    timing.sampleRate = getSampleRate();
    timing.ppq = std::numeric_limits<double>::quiet_NaN();
    if (auto* playhead = getPlayHead()) {
        if (auto position = playhead->getPosition()) {
            timing.bpm = position->getBpm().orFallback(120.0);
            timing.ppq = position->getPpqPosition().orFallback(std::numeric_limits<double>::quiet_NaN());
            timing.playing = position->getIsPlaying();
        }
    }
    int cursor = 0;
    auto render = [&](int end) {
        int count = end - cursor;
        if (count <= 0) return;
        auto at = timing;
        if (std::isfinite(at.ppq)) at.ppq += cursor * at.bpm / (60.0 * at.sampleRate);
        engine.process(buffer.getWritePointer(0, cursor), buffer.getWritePointer(1, cursor), count, at, settings);
        cursor = end;
    };
    for (const auto metadata : midi) {
        auto message = metadata.getMessage();
        if (message.isNoteOn()) {
            int pad = message.getNoteNumber() - 36;
            if (pad >= 0 && pad < 16) {
                render(juce::jlimit(cursor, buffer.getNumSamples(), metadata.samplePosition));
                engine.triggerPad(pad, settings);
            }
        }
    }
    render(buffer.getNumSamples());
    shownSlice = engine.activeSlice();
    shownLoop = engine.activeLoop();
}
juce::AudioProcessorEditor* BreakbeatProcessor::createEditor() { return new BreakbeatEditor(*this); }

void BreakbeatProcessor::loadOnWorker(int loop, juce::String path) {
    auto file = juce::File(path);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->lengthInSamples < 8 || reader->lengthInSamples > 100000000) return;
    juce::AudioBuffer<float> buffer(2, static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true)) return;
    auto sample = std::make_shared<breakbeat::Sample>();
    sample->sampleRate = reader->sampleRate;
    sample->left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples());
    sample->right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + buffer.getNumSamples());
    if (shuttingDown) return;
    engine.setSample(loop, sample);
    std::atomic_store(&displayed[loop], std::shared_ptr<const breakbeat::Sample>(sample));
}
void BreakbeatProcessor::loaderLoop() {
    for (;;) {
        std::array<std::optional<juce::String>, 2> jobs;
        {
            std::unique_lock<std::mutex> lock(requestMutex);
            requestReady.wait(lock, [this] {
                return shuttingDown || pendingPaths[0].has_value() || pendingPaths[1].has_value();
            });
            if (shuttingDown) return;
            jobs.swap(pendingPaths);
        }
        for (int i = 0; i < 2; ++i)
            if (jobs[i] && !shuttingDown) loadOnWorker(i, *jobs[i]);
    }
}
void BreakbeatProcessor::loadSample(int loop, const juce::File& file) {
    if (loop < 0 || loop > 1) return;
    auto path = file.getFullPathName();
    {
        std::lock_guard<std::mutex> lock(pathMutex);
        paths[loop] = path;
    }
    {
        std::lock_guard<std::mutex> lock(requestMutex);
        pendingPaths[loop] = path;
    }
    requestReady.notify_one();
}
juce::String BreakbeatProcessor::samplePath(int loop) const {
    std::lock_guard<std::mutex> lock(pathMutex);
    return paths[loop];
}
std::shared_ptr<const breakbeat::Sample> BreakbeatProcessor::sampleForDisplay(int loop) const {
    return std::atomic_load(&displayed[juce::jlimit(0, 1, loop)]);
}
void BreakbeatProcessor::getStateInformation(juce::MemoryBlock& out) {
    auto tree = state.copyState();
    {
        std::lock_guard<std::mutex> lock(pathMutex);
        tree.setProperty("sampleA", paths[0], nullptr);
        tree.setProperty("sampleB", paths[1], nullptr);
    }
    if (auto xml = tree.createXml()) copyXmlToBinary(*xml, out);
}
void BreakbeatProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (!tree.isValid()) return;
        auto a = tree["sampleA"].toString();
        auto b = tree["sampleB"].toString();
        state.replaceState(tree);
        if (a.isNotEmpty()) loadSample(0, juce::File(a));
        if (b.isNotEmpty()) loadSample(1, juce::File(b));
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BreakbeatProcessor(); }
