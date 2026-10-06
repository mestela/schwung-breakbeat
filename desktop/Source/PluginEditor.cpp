#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace {
const juce::Colour background {0xffb5b7af}; // satin metal
const juce::Colour panel {0xff393e3c};      // recessed control bay
const juce::Colour inset {0xff102d38};      // blue-green sampler display
const juce::Colour hairline {0xff70756f};
const juce::Colour ink {0xffe8e9dd};
const juce::Colour muted {0xffaeb5aa};
const juce::Colour accent {0xffdf6145};     // status LED
const juce::Colour lcdText {0xff9ce4df};
const juce::Colour lcdAccent {0xfff4d454};
const juce::Colour lcdGrid {0xff31545d};
const juce::Colour waveColour {0xff63c4aa};
juce::FontOptions panelFont(float size, bool strong = false) {
   #if JUCE_MAC
    return {"Futura", size, strong ? juce::Font::bold : juce::Font::plain};
   #else
    return {juce::Font::getDefaultSansSerifFontName(), size,
            strong ? juce::Font::bold : juce::Font::plain};
   #endif
}
juce::FontOptions displayFont(float size) {
   #if JUCE_MAC
    return {"Geneva", size, juce::Font::plain};
   #else
    return {juce::Font::getDefaultSansSerifFontName(), size, juce::Font::plain};
   #endif
}
constexpr const char* pageNames[] {"PLAY", "ANCHOR", "SLICE", "RETRIG", "STRETCH", "MIX", "ENV"};
constexpr const char* pageHeadings[] {"PLAYBACK", "ANCHOR MEMORY", "SLICE EDIT", "RETRIGGER", "GRAIN STRETCH", "MIX / GRAIN", "AMPLITUDE ENVELOPE"};
constexpr const char* pageHints[] {
    "HOST CLOCK / 8 POSITIONS / CLICK WAVE TO EDIT", "NATURAL SLICE PROBABILITY",
    "DRAG START MARKERS / AUTO MANUAL", "REPEATS PER BAR",
    "LENGTH / SPAN / PITCH", "OUTPUT LEVEL / GRAIN ENGINE",
    "ATTACK / DECAY / REVERSE"
};
void drawScrew(juce::Graphics& g, float x, float y) {
    g.setColour(juce::Colour(0xff5c605d));
    g.fillEllipse(x - 9, y - 9, 18, 18);
    g.setColour(juce::Colour(0xffd6d7ce));
    g.fillEllipse(x - 7, y - 7, 14, 14);
    g.setColour(juce::Colour(0xff555a56));
    g.drawLine(x - 4, y + 2, x + 4, y - 2, 1.5f);
}
}

BreakbeatLookAndFeel::BreakbeatLookAndFeel() {
    setColour(juce::Slider::textBoxTextColourId, lcdAccent);
    setColour(juce::Slider::textBoxBackgroundColourId, inset);
    setColour(juce::Slider::textBoxOutlineColourId, lcdGrid);
    setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(0xff397785));
    setColour(juce::Label::textColourId, ink);
}
void BreakbeatLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float position, float startAngle, float endAngle,
                                            juce::Slider&) {
    float radius = std::min(width, height) * .34f;
    auto centre = juce::Point<float>(x + width * .5f, y + height * .5f);
    // Etched scale around a plastic encoder, rather than a software progress arc.
    for (int tick = 0; tick <= 10; ++tick) {
        float angle = startAngle + (endAngle - startAngle) * tick / 10.0f;
        auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));
        auto a = centre + direction * (radius + 7);
        auto b = centre + direction * (radius + (tick % 5 == 0 ? 15 : 12));
        g.setColour(tick <= static_cast<int>(position * 10.0f + .5f) ? ink : hairline);
        g.drawLine(a.x, a.y, b.x, b.y, tick % 5 == 0 ? 1.7f : 1.0f);
    }
    g.setColour(juce::Colour(0xff101514));
    g.fillEllipse(centre.x - radius - 2, centre.y - radius - 2, radius * 2 + 4, radius * 2 + 4);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe4e5dd), centre.x - radius, centre.y - radius,
                                         juce::Colour(0xff666c68), centre.x + radius, centre.y + radius, false));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);
    g.setColour(juce::Colour(0xff181e1c));
    g.fillEllipse(centre.x - radius + 4, centre.y - radius + 4, (radius - 4) * 2, (radius - 4) * 2);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff636a66), centre.x - radius + 7, centre.y - radius + 6,
                                         juce::Colour(0xff242a28), centre.x + radius - 6, centre.y + radius - 5, false));
    g.fillEllipse(centre.x - radius + 7, centre.y - radius + 7, (radius - 7) * 2, (radius - 7) * 2);
    float angle = startAngle + position * (endAngle - startAngle);
    auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));
    auto a = centre + direction * (radius - 18);
    auto b = centre + direction * (radius - 5);
    g.setColour(lcdAccent);
    g.drawLine(a.x, a.y, b.x, b.y, 2.8f);
}
void BreakbeatLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                const juce::Colour&, bool hover, bool down) {
    auto b = button.getLocalBounds().toFloat().reduced(1.0f);
    bool selected = button.getToggleState();
    g.setColour(juce::Colour(0xff555c57));
    g.fillRect(b.translated(1, 2));
    g.setGradientFill(juce::ColourGradient(selected || down ? juce::Colour(0xff777d76) : juce::Colour(0xffd7d8cf),
                                         b.getTopLeft(), hover ? juce::Colour(0xffabb0a8) : juce::Colour(0xff969b94),
                                         b.getBottomLeft(), false));
    g.fillRect(b);
    g.setColour(juce::Colour(0xffe1e2d9));
    g.drawLine(b.getX(), b.getY(), b.getRight(), b.getY(), 1);
    if (selected) {
        g.setColour(accent);
        g.fillEllipse(b.getX() + 5, b.getCentreY() - 2.5f, 5, 5);
    }
}
void BreakbeatLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) {
    g.setFont(panelFont(14.0f));
    g.setColour(juce::Colour(0xff202925));
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
    setSize(1080, 540);
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
    control.label->setFont(panelFont(13.0f));
    control.slider = std::make_unique<juce::Slider>();
    control.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    control.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 23);
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
    for (int i = 0; i < 7; ++i) tabs[i]->setBounds(59 + i * 138, 483, 130, 31);
    for (int i = 0; i < 2; ++i) {
        loadButtons[i]->setBounds(425 + i * 494, 107, 85, 27);
        editButtons[i]->setBounds(72 + i * 100, 209, 92, 29);
    }
    waveformBounds = page == 0 ? juce::Rectangle<int>(70, 231, 625, 158)
                               : juce::Rectangle<int>(70, 249, 940, 198);
    constexpr int contentX = 70, contentWidth = 940;
    for (auto& c : controls) {
        if (c.page == 0) {
            int x = c.column < 3 ? 711 + c.column * 101 : 762 + (c.column - 3) * 101;
            int y = c.column < 3 ? 226 : 337;
            c.slider->setBounds(x + 4, y, 93, 85);
            c.label->setBounds(x, y + 86, 101, 19);
        } else {
            int cellWidth = contentWidth / 4;
            int x = contentX + c.column * cellWidth;
            int y = 220 + c.row * 116;
            c.slider->setBounds(x + 27, y, cellWidth - 54, 98);
            c.label->setBounds(x + 3, y + 96, cellWidth - 6, 20);
        }
    }
    for (auto& s : switches) {
        if (s.page == 2) s.button->setBounds(340 + s.column * 110, 209, 102, 29);
        else s.button->setBounds(contentX + s.column * 235 + 65, 263 + s.row * 116, 106, 33);
    }
}
void BreakbeatEditor::paint(juce::Graphics& g) {
    g.fillAll(background);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd0d1c9), juce::Point<float>(30, 0),
                                         juce::Colour(0xff9fa39d), juce::Point<float>(30, 540), false));
    g.fillRect(30, 0, 1020, 540);
    g.setColour(juce::Colour(0xff666b66));
    g.fillRect(0, 0, 30, 540);
    g.fillRect(1050, 0, 30, 540);
    g.setColour(juce::Colour(0xffe4e4da));
    g.fillRect(30, 0, 1020, 2);
    g.setColour(juce::Colour(0xff60665f));
    g.fillRect(30, 537, 1020, 3);
    for (float x : {15.0f, 1065.0f}) for (float y : {31.0f, 509.0f}) drawScrew(g, x, y);
    g.setColour(juce::Colour(0xff252b29));
    g.setFont(panelFont(32.0f));
    g.drawText("BREAKBEAT", 65, 23, 310, 34, juce::Justification::left);
    g.setFont(panelFont(14.0f));
    g.drawText("SCHWUNG", 805, 24, 200, 19, juce::Justification::right);
    g.setFont(panelFont(10.0f));
    g.drawText("DIGITAL BREAK SAMPLER  /  BB-08", 730, 43, 275, 15, juce::Justification::right);
    g.setColour(juce::Colour(0xff788078));
    for (int x = 55; x < 1020; x += 5) g.drawVerticalLine(x, 66, 68);

    for (int i = 0; i < 2; ++i) {
        auto bounds = juce::Rectangle<float>(static_cast<float>(65 + i * 495), 81, 458, 72);
        g.setColour(i == editLoop ? lcdAccent : juce::Colour(0xff555d55));
        g.fillRect(bounds.expanded(2));
        g.setColour(inset);
        g.fillRect(bounds);
        g.setColour(lcdGrid);
        g.drawLine(bounds.getX(), bounds.getY(), bounds.getRight(), bounds.getY(), 2);
        g.drawLine(bounds.getX(), bounds.getY(), bounds.getX(), bounds.getBottom(), 2);
        g.setColour(i == processor.currentLoop() ? accent : lcdGrid);
        int x = static_cast<int>(bounds.getX());
        g.fillEllipse(x + 20, 125, 8, 8);
        g.setColour(lcdText);
        g.setFont(displayFont(17.0f));
        g.drawText(i ? "B>" : "A>", x + 15, 94, 38, 25, juce::Justification::left);
        g.setColour(lcdAccent);
        g.setFont(displayFont(16.0f));
        auto path = processor.samplePath(i);
        auto name = path.isEmpty() ? juce::String("NO SAMPLE") : juce::File(path).getFileName().toUpperCase();
        g.drawFittedText(name, x + 61, 94, 285, 25,
                         juce::Justification::centredLeft, 1);
        g.setColour(lcdText);
        g.setFont(displayFont(12.0f));
        auto sample = processor.sampleForDisplay(i);
        auto detail = sample && sample->valid()
            ? juce::String(sample->frames() / sample->sampleRate, 1) + " SEC   " + juce::String(static_cast<int>(sample->sampleRate)) + " HZ"
            : juce::String("WAV  AIFF  FLAC  MP3");
        g.drawText(detail, x + 61, 124, 290, 16, juce::Justification::left);
    }
    g.setColour(juce::Colour(0xff181d1c));
    g.fillRect(54, 177, 972, 286);
    g.setColour(juce::Colour(0xff767e77));
    g.fillRect(56, 179, 968, 282);
    g.setColour(panel);
    g.fillRect(59, 182, 962, 276);
    g.setColour(juce::Colour(0xffd5dbcf));
    g.setFont(panelFont(16.0f));
    g.drawText(pageHeadings[page], 70, 188, 360, 18, juce::Justification::left);
    g.setColour(muted);
    g.setFont(panelFont(10.0f));
    g.drawFittedText(pageHints[page], 498, 188, 512, 18, juce::Justification::centredRight, 1);
    g.setColour(juce::Colour(0xff5f6861));
    g.fillRect(69, 210, 942, 1);
    if (page == 0) {
        g.setColour(juce::Colour(0xff596660));
        g.fillRect(704, 220, 1, 226);
        g.setColour(muted);
        g.setFont(panelFont(10.5f));
        g.drawText(editLoop ? "B / SAMPLE & SLICE POSITION" : "A / SAMPLE & SLICE POSITION", 73, 392, 400, 18, juce::Justification::left);
        auto active = processor.currentSlice();
        for (int i = 0; i < 8; ++i) {
            auto box = juce::Rectangle<float>(73 + i * 78.0f, 417, 68, 28);
            g.setColour(i == active && processor.currentLoop() == editLoop ? lcdAccent : juce::Colour(0xff252b29));
            g.fillRect(box);
            g.setColour(i == active && processor.currentLoop() == editLoop ? inset : lcdText);
            g.setFont(panelFont(14.0f));
            g.drawText(juce::String(i + 1), box.toNearestInt(), juce::Justification::centred);
            g.setColour(i == active ? accent : juce::Colour(0xff657267));
            g.fillEllipse(box.getCentreX() - 2.5f, box.getY() - 8, 5, 5);
        }
    }
    g.setColour(juce::Colour(0xff5b625c));
    g.fillRect(54, 468, 972, 1);
    g.setColour(juce::Colour(0xff354037));
    g.setFont(panelFont(9.0f));
    g.drawText("MIDI 36-51  /  HOST SYNC", 69, 515, 300, 14, juce::Justification::left);
    g.drawText("SCHWUNG  BB-08", 835, 515, 175, 14, juce::Justification::right);
    if (page != 0 && page != 2) return;
    g.setColour(juce::Colour(0xff0a1b20));
    g.fillRect(waveformBounds.expanded(3));
    g.setColour(inset);
    g.fillRect(waveformBounds);
    auto sample = processor.sampleForDisplay(editLoop);
    if (!sample || !sample->valid()) {
        g.setColour(lcdText);
        g.setFont(displayFont(14.0f));
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
    g.setColour(lcdGrid);
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
        g.setColour(highlighted ? lcdAccent : lcdText);
        g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(waveformBounds.getBottom()));
        g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(x + 2), static_cast<float>(waveformBounds.getY() + 5), 20, 20), 2);
        g.setColour(inset);
        g.setFont(displayFont(12.0f));
        g.drawText(juce::String(j + 1), x + 2, waveformBounds.getY() + 5, 20, 20, juce::Justification::centred);
    }
    if (draggingMarker < 0 && processor.currentLoop() == editLoop) {
        int active = processor.currentSlice();
        auto id = juce::String(prefix) + juce::String(active + 1);
        float marker = processor.state.getRawParameterValue(id)->load() / 1000000.0f;
        int x = waveformBounds.getX() + static_cast<int>(marker * waveformBounds.getWidth());
        g.setColour(lcdAccent);
        g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(waveformBounds.getBottom()));
    }
    g.setColour(lcdText);
    g.setFont(displayFont(11.0f));
    g.drawText(draggingMarker >= 0 ? "FINE EDIT  /  RELEASE FOR FULL VIEW"
                                   : page == 0 ? "CLICK TO EDIT SLICE STARTS" : "DRAG A NUMBERED START MARKER",
               waveformBounds.getX() + 10, waveformBounds.getBottom() - 26, 350, 20, juce::Justification::left);
}
void BreakbeatEditor::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 2; ++i) {
        if (juce::Rectangle<int>(65 + i * 495, 81, 458, 72).contains(e.getPosition())) {
            setEditLoop(i);
            return;
        }
    }
    if (page == 0 && waveformBounds.contains(e.getPosition())) {
        setPage(2);
        return;
    }
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
