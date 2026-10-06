#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace {
const juce::Colour background {0xff252620};
const juce::Colour panel {0xff30312a};
const juce::Colour inset {0xff1d1f1b};
const juce::Colour hairline {0xff494a40};
const juce::Colour ink {0xffece9dc};
const juce::Colour muted {0xffaaa99b};
const juce::Colour accent {0xffdcb05e};
const juce::Colour waveColour {0xffa7bea9};
constexpr const char* pageNames[] {"Play", "Anchors", "Slices", "Retrig", "Stretch", "Mix", "Envelope"};
constexpr const char* pageHeadings[] {"PLAYBACK", "ANCHORS", "SLICE STARTS", "RETRIGGERS", "STRETCH", "MIX / GRAIN", "ENVELOPE"};
constexpr const char* pageHints[] {
    "The break follows the host transport.", "Keep the natural slice at each position.",
    "Drag a marker for precise timing.", "Chance per bar. Rates compete when several fire.",
    "Hold, repitch and scatter slices.", "Level and grain behaviour.",
    "Shape the start and end of each slice."
};
}

BreakbeatLookAndFeel::BreakbeatLookAndFeel() {
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxBackgroundColourId, inset);
    setColour(juce::Slider::textBoxOutlineColourId, hairline);
    setColour(juce::Slider::textBoxHighlightColourId, accent);
    setColour(juce::Label::textColourId, ink);
}
void BreakbeatLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float position, float startAngle, float endAngle,
                                            juce::Slider&) {
    float radius = std::min(width, height) * .38f;
    auto centre = juce::Point<float>(x + width * .5f, y + height * .5f);
    juce::Path track, value;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0, startAngle, endAngle, true);
    value.addCentredArc(centre.x, centre.y, radius, radius, 0, startAngle,
                        startAngle + position * (endAngle - startAngle), true);
    g.setColour(hairline);
    g.strokePath(track, juce::PathStrokeType(5.0f));
    g.setColour(accent);
    g.strokePath(value, juce::PathStrokeType(5.0f));
    auto face = juce::Rectangle<float>(centre.x - radius + 10, centre.y - radius + 10,
                                       (radius - 10) * 2, (radius - 10) * 2);
    g.setColour(panel.brighter(.12f));
    g.fillEllipse(face);
    g.setColour(hairline);
    g.drawEllipse(face, 1);
    float angle = startAngle + position * (endAngle - startAngle);
    auto dot = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (radius - 14);
    g.setColour(ink);
    g.fillEllipse(dot.x - 2.5f, dot.y - 2.5f, 5, 5);
}
void BreakbeatLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                const juce::Colour&, bool hover, bool down) {
    auto b = button.getLocalBounds().toFloat().reduced(.5f);
    bool selected = button.getToggleState();
    g.setColour(selected ? accent : hover ? panel.brighter(.16f) : panel);
    g.fillRoundedRectangle(b, 4);
    g.setColour(selected ? accent : hairline);
    g.drawRoundedRectangle(b, 4, 1);
    if (down) { g.setColour(ink.withAlpha(.12f)); g.fillRoundedRectangle(b, 4); }
}
void BreakbeatLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) {
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.setColour(button.getToggleState() ? inset : ink);
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(5), juce::Justification::centred, 1);
}

BreakbeatEditor::BreakbeatEditor(BreakbeatProcessor& p) : AudioProcessorEditor(&p), processor(p) {
    setLookAndFeel(&skin);
    for (int i = 0; i < 7; ++i) {
        tabs[i] = std::make_unique<juce::TextButton>(pageNames[i]);
        tabs[i]->onClick = [this, i] { setPage(i); };
        addAndMakeVisible(*tabs[i]);
    }
    for (int i = 0; i < 2; ++i) {
        loadButtons[i] = std::make_unique<juce::TextButton>("LOAD...");
        loadButtons[i]->onClick = [this, i] { openSample(i); };
        addAndMakeVisible(*loadButtons[i]);
        editButtons[i] = std::make_unique<juce::TextButton>(i ? "EDIT B" : "EDIT A");
        editButtons[i]->onClick = [this, i] { setEditLoop(i); };
        addAndMakeVisible(*editButtons[i]);
    }
    addDial(0, "complexity", "COMPLEXITY", 0, 0);
    addDial(0, "bChance", "B CHANCE", 1, 0);
    addDial(0, "phraseBars", "PHRASE", 2, 0);
    addDial(0, "ALength", "A LENGTH", 3, 0);
    addDial(0, "BLength", "B LENGTH", 4, 0);
    for (int i = 0; i < 8; ++i) {
        addDial(1, "anchor" + juce::String(i + 1), "POSITION " + juce::String(i + 1), i % 4, i / 4);
        static const char* names[] {"2X", "3X", "4X", "8X", "16X", "32X", "PUSH", "DRAG"};
        addDial(3, "retrig" + juce::String(i + 1), names[i], i % 4, i / 4);
    }
    addSwitch(2, "AManual", "A MANUAL", 0, 0);
    addSwitch(2, "BManual", "B MANUAL", 1, 0);
    addDial(4, "stretchChance", "CHANCE", 0, 0);
    addDial(4, "stretchLengthMin", "LENGTH MIN", 1, 0);
    addDial(4, "stretchLengthMax", "LENGTH MAX", 2, 0);
    addDial(4, "stretchSpanMin", "SPAN MIN", 3, 0);
    addDial(4, "stretchSpanMax", "SPAN MAX", 0, 1);
    addDial(4, "stretchPitchMin", "PITCH MIN", 1, 1);
    addDial(4, "stretchPitchMax", "PITCH MAX", 2, 1);
    addDial(5, "AVolume", "A LEVEL", 0, 0);
    addDial(5, "BVolume", "B LEVEL", 1, 0);
    addDial(5, "grainFx", "GRAIN REPEAT", 2, 0);
    addDial(5, "grainCycle", "GRAIN CYCLE", 3, 0);
    addSwitch(5, "pitchLock", "PITCH LOCK", 0, 1);
    addDial(6, "attack", "ATTACK / MS", 0, 0);
    addDial(6, "decay", "DECAY / MS", 1, 0);
    addSwitch(6, "reverse", "EARLY REVERSE", 2, 0);
    setSize(960, 600);
    setPage(0);
    setEditLoop(0);
    startTimerHz(15);
}
BreakbeatEditor::~BreakbeatEditor() { stopTimer(); setLookAndFeel(nullptr); }
void BreakbeatEditor::addDial(int group, const juce::String& id, const juce::String& name, int column, int row) {
    Control control;
    control.page = group;
    control.column = column;
    control.row = row;
    control.label = std::make_unique<juce::Label>(id, name);
    control.label->setJustificationType(juce::Justification::centred);
    control.label->setFont(juce::FontOptions(11.5f, juce::Font::bold));
    control.slider = std::make_unique<juce::Slider>();
    control.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    control.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 24);
    control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, id, *control.slider);
    addAndMakeVisible(*control.label);
    addAndMakeVisible(*control.slider);
    controls.push_back(std::move(control));
}
void BreakbeatEditor::addSwitch(int group, const juce::String& id, const juce::String& name, int column, int row) {
    Switch control;
    control.page = group; control.column = column; control.row = row;
    control.button = std::make_unique<juce::TextButton>(name);
    control.button->setClickingTogglesState(true);
    control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, id, *control.button);
    addAndMakeVisible(*control.button);
    switches.push_back(std::move(control));
}
void BreakbeatEditor::setPage(int p) {
    page = p;
    for (int i = 0; i < 7; ++i) tabs[i]->setToggleState(i == p, juce::dontSendNotification);
    for (auto& c : controls) { c.label->setVisible(c.page == page); c.slider->setVisible(c.page == page); }
    for (auto& s : switches) s.button->setVisible(s.page == page);
    for (int i = 0; i < 2; ++i) editButtons[i]->setVisible(page == 2);
    resized();
    repaint();
}
void BreakbeatEditor::setEditLoop(int loop) {
    editLoop = loop;
    for (int i = 0; i < 2; ++i) editButtons[i]->setToggleState(i == loop, juce::dontSendNotification);
    repaint();
}
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
    for (int i = 0; i < 7; ++i) tabs[i]->setBounds(20, 112 + i * 48, 140, 38);
    for (int i = 0; i < 2; ++i) {
        loadButtons[i]->setBounds(452 + i * 371, 94, 91, 32);
        editButtons[i]->setBounds(205 + i * 106, 215, 98, 32);
    }
    waveformBounds = {205, 300, 717, 222};
    constexpr int contentX = 195, contentWidth = 735;
    for (auto& c : controls) {
        int columns = c.page == 0 ? 5 : 4;
        int cellWidth = contentWidth / columns;
        int x = contentX + c.column * cellWidth;
        int y = 218 + c.row * 156;
        c.slider->setBounds(x + 12, y, cellWidth - 24, 114);
        c.label->setBounds(x + 2, y + 114, cellWidth - 4, 23);
    }
    for (auto& s : switches) {
        if (s.page == 2) s.button->setBounds(460 + s.column * 118, 215, 108, 32);
        else s.button->setBounds(contentX + s.column * 180 + 24, 246 + s.row * 156, 146, 42);
    }
}
void BreakbeatEditor::paint(juce::Graphics& g) {
    g.fillAll(background);
    g.setColour(inset);
    g.fillRect(0, 0, 180, getHeight());
    g.setColour(hairline);
    g.drawVerticalLine(179, 0, static_cast<float>(getHeight()));
    g.setColour(accent);
    g.fillRect(20, 27, 5, 24);
    g.setColour(ink);
    g.setFont(juce::FontOptions(21.0f, juce::Font::bold));
    g.drawText("BREAKBEAT", 35, 24, 340, 32, juce::Justification::left);
    g.setColour(muted);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("SCHWUNG", 22, 76, 130, 22, juce::Justification::left);
    g.drawText("DESKTOP  /  V0.1", 20, getHeight() - 35, 140, 18, juce::Justification::left);
    for (int i = 0; i < 2; ++i) {
        auto bounds = juce::Rectangle<float>(static_cast<float>(195 + i * 371), 75, 364, 72);
        g.setColour(panel);
        g.fillRoundedRectangle(bounds, 5);
        g.setColour(hairline);
        g.drawRoundedRectangle(bounds, 5, 1);
        g.setColour(i == processor.currentLoop() ? accent : muted);
        g.setFont(juce::FontOptions(25.0f, juce::Font::bold));
        int x = static_cast<int>(bounds.getX());
        g.drawText(i ? "B" : "A", x + 13, 91, 38, 36, juce::Justification::centred);
        g.setColour(ink);
        g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
        auto path = processor.samplePath(i);
        auto name = path.isEmpty() ? juce::String("No sample loaded") : juce::File(path).getFileName();
        g.drawFittedText(name, x + 58, 88, 190, 25,
                         juce::Justification::centredLeft, 1);
        g.setColour(muted);
        g.setFont(juce::FontOptions(11.0f));
        auto sample = processor.sampleForDisplay(i);
        auto detail = sample && sample->valid()
            ? juce::String(sample->frames() / sample->sampleRate, 1) + " sec  /  " + juce::String(static_cast<int>(sample->sampleRate)) + " Hz"
            : juce::String("WAV  AIFF  FLAC  MP3");
        g.drawText(detail, x + 58, 116, 190, 17, juce::Justification::left);
    }
    g.setColour(accent);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(pageHeadings[page], 201, 184, 220, 22, juce::Justification::left);
    g.setColour(muted);
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(pageHints[page], 425, 184, 500, 22, juce::Justification::centredRight, 1);
    g.setColour(hairline);
    g.fillRect(205, 207, 717, 1);
    if (page == 0) {
        g.setColour(muted);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("EIGHT POSITIONS", 205, 416, 200, 18, juce::Justification::left);
        auto active = processor.currentSlice();
        for (int i = 0; i < 8; ++i) {
            auto box = juce::Rectangle<float>(205 + i * 90.5f, 448, 82, 52);
            g.setColour(i == active ? accent : panel);
            g.fillRoundedRectangle(box, 4);
            g.setColour(i == active ? inset : ink);
            g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
            g.drawText(juce::String(i + 1), box.toNearestInt(), juce::Justification::centred);
        }
    }
    g.setColour(hairline);
    g.fillRect(195, 549, 735, 1);
    g.setColour(muted);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("MIDI 36-51  /  A1-A8  B1-B8", 205, 558, 380, 19, juce::Justification::left);
    g.drawText("HOST SYNC", 755, 558, 167, 19, juce::Justification::right);
    if (page != 2) return;
    g.setColour(inset);
    g.fillRoundedRectangle(waveformBounds.toFloat(), 5);
    auto sample = processor.sampleForDisplay(editLoop);
    if (!sample || !sample->valid()) {
        g.setColour(muted);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText("LOAD A SAMPLE TO EDIT ITS EIGHT STARTS", waveformBounds, juce::Justification::centred);
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
    g.setColour(hairline);
    for (int j = 1; j < 8 && draggingMarker < 0; ++j) {
        int x = waveformBounds.getX() + j * waveformBounds.getWidth() / 8;
        g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(waveformBounds.getBottom()));
    }
    g.drawHorizontalLine(mid, static_cast<float>(waveformBounds.getX()), static_cast<float>(waveformBounds.getRight()));
    g.setColour(waveColour);
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
        int extent = static_cast<int>(peak * (waveformBounds.getHeight() / 2 - 12));
        g.drawVerticalLine(waveformBounds.getX() + x, static_cast<float>(mid - extent), static_cast<float>(mid + extent));
    }
    auto prefix = editLoop ? "BMarker" : "AMarker";
    for (int j = 0; j < 8; ++j) {
        float marker = markerFraction(j);
        int x = draggingMarker >= 0
            ? waveformBounds.getCentreX() + static_cast<int>((marker - markerFraction(draggingMarker)) * waveformBounds.getWidth() / zoomWidth())
            : waveformBounds.getX() + static_cast<int>(marker * waveformBounds.getWidth());
        if (x < waveformBounds.getX() || x > waveformBounds.getRight()) continue;
        bool highlighted = j == draggingMarker || (draggingMarker < 0 && processor.currentLoop() == editLoop && processor.currentSlice() == j);
        g.setColour(highlighted ? accent : ink);
        g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(waveformBounds.getBottom()));
        g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(x + 2), static_cast<float>(waveformBounds.getY() + 5), 20, 20), 2);
        g.setColour(inset);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String(j + 1), x + 2, waveformBounds.getY() + 5, 20, 20, juce::Justification::centred);
    }
    if (draggingMarker < 0 && processor.currentLoop() == editLoop) {
        int active = processor.currentSlice();
        auto id = juce::String(prefix) + juce::String(active + 1);
        float marker = processor.state.getRawParameterValue(id)->load() / 1000000.0f;
        int x = waveformBounds.getX() + static_cast<int>(marker * waveformBounds.getWidth());
        g.setColour(accent);
        g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(waveformBounds.getBottom()));
    }
    g.setColour(muted);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(draggingMarker >= 0 ? "FINE EDIT  /  RELEASE FOR FULL VIEW"
                                   : "DRAG A NUMBERED START MARKER",
               waveformBounds.getX() + 10, waveformBounds.getBottom() - 26, 350, 20, juce::Justification::left);
}
void BreakbeatEditor::mouseDown(const juce::MouseEvent& e) {
    if (page != 2 || !waveformBounds.contains(e.getPosition())) return;
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
        if (e.x != dragStartX) {
            auto mode = juce::String(editLoop ? "BManual" : "AManual");
            if (auto* manual = processor.state.getParameter(mode))
                if (manual->getValue() < .5f) manual->setValueNotifyingHost(1.0f);
        }
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
void BreakbeatEditor::timerCallback() { repaint(); }
