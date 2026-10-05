#include "PluginEditor.h"

namespace {
constexpr const char* pageNames[] {"Main", "Anchors", "Slicing", "Retrig", "Stretch", "Master", "Envelope"};
}

BreakbeatEditor::BreakbeatEditor(BreakbeatProcessor& p) : AudioProcessorEditor(&p), processor(p) {
    for (int i = 0; i < 7; ++i) {
        tabs[i] = std::make_unique<juce::TextButton>(pageNames[i]);
        tabs[i]->onClick = [this, i] { setPage(i); };
        addAndMakeVisible(*tabs[i]);
    }
    for (int i = 0; i < 2; ++i) {
        loadButtons[i] = std::make_unique<juce::TextButton>(i ? "Load B..." : "Load A...");
        loadButtons[i]->onClick = [this, i] { openSample(i); };
        addAndMakeVisible(*loadButtons[i]);
        editButtons[i] = std::make_unique<juce::TextButton>(i ? "Edit B" : "Edit A");
        editButtons[i]->onClick = [this, i] { setEditLoop(i); };
        addAndMakeVisible(*editButtons[i]);
    }
    addSlider(0, "complexity", "Complexity", 0, 0);
    addSlider(0, "bChance", "B chance", 1, 0);
    addSlider(0, "phraseBars", "Phrase bars", 2, 0);
    addSlider(0, "ALength", "A length / bars", 0, 1);
    addSlider(0, "BLength", "B length / bars", 1, 1);
    for (int i = 0; i < 8; ++i) {
        addSlider(1, "anchor" + juce::String(i + 1), "Position " + juce::String(i + 1), i % 4, i / 4);
        static const char* names[] {"2x", "3x", "4x", "8x", "16x", "32x", "Push", "Drag"};
        addSlider(3, "retrig" + juce::String(i + 1), names[i], i % 4, i / 4);
    }
    addSlider(2, "AManual", "A manual mode", 0, 0);
    addSlider(2, "BManual", "B manual mode", 1, 0);
    addSlider(4, "stretchChance", "Chance", 0, 0);
    addSlider(4, "stretchLengthMin", "Length min", 0, 1);
    addSlider(4, "stretchLengthMax", "Length max", 1, 1);
    addSlider(4, "stretchSpanMin", "Span min", 2, 1);
    addSlider(4, "stretchSpanMax", "Span max", 3, 1);
    addSlider(4, "stretchPitchMin", "Pitch min", 0, 2);
    addSlider(4, "stretchPitchMax", "Pitch max", 1, 2);
    addSlider(5, "AVolume", "A volume", 0, 0);
    addSlider(5, "BVolume", "B volume", 1, 0);
    addSlider(5, "grainFx", "Grain repeat", 0, 1);
    addSlider(5, "grainCycle", "Grain cycle", 1, 1);
    addSlider(5, "pitchLock", "Pitch lock", 2, 1);
    addSlider(6, "attack", "Attack / ms", 0, 0);
    addSlider(6, "decay", "Decay / ms", 1, 0);
    addSlider(6, "reverse", "Early end reverse", 2, 0);
    setSize(980, 660);
    setPage(0);
    startTimerHz(20);
}
BreakbeatEditor::~BreakbeatEditor() { stopTimer(); }
void BreakbeatEditor::addSlider(int group, const juce::String& id, const juce::String& name, int column, int row) {
    Control control;
    control.page = group;
    control.column = column;
    control.row = row;
    control.label = std::make_unique<juce::Label>(id, name);
    control.label->setJustificationType(juce::Justification::centredLeft);
    control.slider = std::make_unique<juce::Slider>();
    control.slider->setSliderStyle(juce::Slider::LinearHorizontal);
    control.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 24);
    control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, id, *control.slider);
    addAndMakeVisible(*control.label);
    addAndMakeVisible(*control.slider);
    controls.push_back(std::move(control));
}
void BreakbeatEditor::setPage(int p) {
    page = p;
    for (auto& c : controls) { c.label->setVisible(c.page == page); c.slider->setVisible(c.page == page); }
    for (int i = 0; i < 2; ++i) editButtons[i]->setVisible(page == 2);
    resized();
    repaint();
}
void BreakbeatEditor::setEditLoop(int loop) { editLoop = loop; repaint(); }
float BreakbeatEditor::markerFraction(int marker) const {
    auto id = juce::String(editLoop ? "BMarker" : "AMarker") + juce::String(marker + 1);
    return processor.state.getRawParameterValue(id)->load() / 1000000.0f;
}
float BreakbeatEditor::zoomWidth() const {
    auto sample = processor.sampleForDisplay(editLoop);
    if (!sample || !sample->valid()) return 1.0f;
    return std::min(1.0f, static_cast<float>(sample->sampleRate / sample->frames() * .5));
}
void BreakbeatEditor::openSample(int loop) {
    chooser = std::make_unique<juce::FileChooser>("Choose a break or sound", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<BreakbeatEditor>(this), loop](const juce::FileChooser& c) {
            if (safe && c.getResult().existsAsFile()) safe->processor.loadSample(loop, c.getResult());
        });
}
void BreakbeatEditor::resized() {
    auto area = getLocalBounds().reduced(24);
    auto header = area.removeFromTop(40);
    for (int i = 0; i < 7; ++i) tabs[i]->setBounds(header.removeFromLeft(i == 6 ? header.getWidth() : 130).reduced(3));
    area.removeFromTop(14);
    auto loaders = area.removeFromTop(42);
    loadButtons[0]->setBounds(loaders.removeFromLeft(150).reduced(3));
    loadButtons[1]->setBounds(loaders.removeFromLeft(150).reduced(3));
    if (page == 2) {
        editButtons[0]->setBounds(loaders.removeFromLeft(110).reduced(3));
        editButtons[1]->setBounds(loaders.removeFromLeft(110).reduced(3));
    }
    waveformBounds = {32, 264, getWidth() - 64, 330};
    for (auto& c : controls) {
        int x = 36 + c.column * 232;
        int y = 122 + c.row * 114;
        c.label->setBounds(x, y, 210, 26);
        c.slider->setBounds(x, y + 28, 210, 42);
    }
}
void BreakbeatEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff161922));
    g.setColour(juce::Colours::white);
    g.setFont(24);
    g.drawText("SCHWUNG  /  BREAKBEAT", 30, getHeight() - 44, 500, 30, juce::Justification::centredLeft);
    if (page != 2) return;
    g.setColour(juce::Colour(0xff222935));
    g.fillRoundedRectangle(waveformBounds.toFloat(), 8);
    auto sample = processor.sampleForDisplay(editLoop);
    if (!sample || !sample->valid()) {
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Load a sample to edit its eight slice starts", waveformBounds, juce::Justification::centred);
        return;
    }
    const auto& data = sample->left;
    if (draggingMarker < 0 && (cachedWaveform != sample.get() || cachedWidth != waveformBounds.getWidth())) {
        cachedWaveform = sample.get();
        cachedWidth = waveformBounds.getWidth();
        waveformPeaks.assign(static_cast<size_t>(cachedWidth), 0.0f);
        for (int x = 0; x < cachedWidth; ++x) {
            size_t begin = static_cast<size_t>(x) * data.size() / static_cast<size_t>(cachedWidth);
            size_t end = static_cast<size_t>(x + 1) * data.size() / static_cast<size_t>(cachedWidth);
            for (size_t j = begin; j < end; ++j)
                waveformPeaks[static_cast<size_t>(x)] = std::max(waveformPeaks[static_cast<size_t>(x)], std::abs(data[j]));
        }
    }
    auto mid = waveformBounds.getCentreY();
    g.setColour(juce::Colour(0xff6dd6c1));
    for (int x = 0; x < waveformBounds.getWidth(); ++x) {
        float peak;
        if (draggingMarker >= 0) {
            float fraction = markerFraction(draggingMarker) +
                (x / static_cast<float>(waveformBounds.getWidth()) - .5f) * zoomWidth();
            int index = static_cast<int>(fraction * data.size());
            int window = std::max(1, static_cast<int>(zoomWidth() * data.size() / waveformBounds.getWidth()));
            peak = 0;
            for (int j = std::max(0, index); j < std::min(static_cast<int>(data.size()), index + window); ++j)
                peak = std::max(peak, std::abs(data[static_cast<size_t>(j)]));
        } else peak = waveformPeaks[static_cast<size_t>(x)];
        int extent = static_cast<int>(peak * (waveformBounds.getHeight() / 2 - 10));
        g.drawVerticalLine(waveformBounds.getX() + x, static_cast<float>(mid - extent), static_cast<float>(mid + extent));
    }
    auto prefix = editLoop ? "BMarker" : "AMarker";
    for (int j = 0; j < 8; ++j) {
        float marker = markerFraction(j);
        int x = draggingMarker >= 0
            ? waveformBounds.getCentreX() + static_cast<int>((marker - markerFraction(draggingMarker)) * waveformBounds.getWidth() / zoomWidth())
            : waveformBounds.getX() + static_cast<int>(marker * waveformBounds.getWidth());
        if (x < waveformBounds.getX() || x > waveformBounds.getRight()) continue;
        g.setColour(j == draggingMarker ? juce::Colours::orange : juce::Colours::white);
        g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(waveformBounds.getBottom()));
        g.drawText(juce::String(j + 1), x + 3, waveformBounds.getY() + 4, 20, 20, juce::Justification::left);
    }
    if (draggingMarker < 0 && processor.currentLoop() == editLoop) {
        int active = processor.currentSlice();
        auto id = juce::String(prefix) + juce::String(active + 1);
        float marker = processor.state.getRawParameterValue(id)->load() / 1000000.0f;
        int x = waveformBounds.getX() + static_cast<int>(marker * waveformBounds.getWidth());
        g.setColour(juce::Colours::orange);
        g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(waveformBounds.getBottom()));
    }
    g.setColour(juce::Colours::white);
    g.drawText(draggingMarker >= 0 ? "Fine edit — release to see the full waveform"
                                   : (editLoop ? "B slice starts — drag a line" : "A slice starts — drag a line"),
               waveformBounds.getX(), waveformBounds.getY() - 28, 400, 24, juce::Justification::left);
}
void BreakbeatEditor::mouseDown(const juce::MouseEvent& e) {
    if (page != 2 || !waveformBounds.contains(e.getPosition())) return;
    auto prefix = editLoop ? "BMarker" : "AMarker";
    int nearest = -1, distance = 100000;
    for (int j = 0; j < 8; ++j) {
        int x = waveformBounds.getX() + static_cast<int>(markerFraction(j) * waveformBounds.getWidth());
        if (std::abs(x - e.x) < distance) { distance = std::abs(x - e.x); nearest = j; }
    }
    if (distance < 28) {
        draggingMarker = nearest;
        dragStartX = e.x;
        dragStartFraction = markerFraction(nearest);
        repaint();
    }
}
void BreakbeatEditor::mouseDrag(const juce::MouseEvent& e) { if (draggingMarker >= 0) moveMarker(e); }
void BreakbeatEditor::mouseUp(const juce::MouseEvent&) { draggingMarker = -1; repaint(); }
void BreakbeatEditor::moveMarker(const juce::MouseEvent& e) {
    auto id = juce::String(editLoop ? "BMarker" : "AMarker") + juce::String(draggingMarker + 1);
    if (auto* parameter = processor.state.getParameter(id)) {
        float fraction = dragStartFraction + (e.x - dragStartX) * zoomWidth() /
                                               static_cast<float>(waveformBounds.getWidth());
        auto sample = processor.sampleForDisplay(editLoop);
        float gap = sample && sample->valid() ? 1.0f / sample->frames() : .000001f;
        float minimum = draggingMarker ? markerFraction(draggingMarker - 1) + gap : 0.0f;
        float maximum = draggingMarker < 7 ? markerFraction(draggingMarker + 1) - gap : 1.0f;
        fraction = juce::jlimit(minimum, std::max(minimum, maximum), fraction);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(fraction);
        parameter->endChangeGesture();
        repaint();
    }
}
void BreakbeatEditor::timerCallback() { if (page == 2) repaint(); }
