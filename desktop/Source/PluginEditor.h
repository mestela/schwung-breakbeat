#pragma once
#include "PluginProcessor.h"

class BreakbeatEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit BreakbeatEditor(BreakbeatProcessor&);
    ~BreakbeatEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    void setPage(int);
    void setEditLoop(int);
    void moveMarker(const juce::MouseEvent&);
    void openSample(int);
    void addSlider(int page, const juce::String& id, const juce::String& label,
                   int column, int row);
    BreakbeatProcessor& processor;
    int page = 0;
    int editLoop = 0;
    int draggingMarker = -1;
    int dragStartX = 0;
    float dragStartFraction = 0.0f;
    std::array<std::unique_ptr<juce::TextButton>, 7> tabs;
    std::array<std::unique_ptr<juce::TextButton>, 2> loadButtons;
    std::array<std::unique_ptr<juce::TextButton>, 2> editButtons;
    struct Control {
        int page = 0, column = 0, row = 0;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::vector<Control> controls;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> waveformBounds;
    const breakbeat::Sample* cachedWaveform = nullptr;
    int cachedWidth = 0;
    std::vector<float> waveformPeaks;
    float markerFraction(int marker) const;
    float zoomWidth() const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreakbeatEditor)
};
