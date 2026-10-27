#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace toneshaper::ui
{

/** Live analyser: input spectrum, applied correction curve(s), tone target and processing
    range, with one draggable handle per tone band (x = frequency, y = level). */
class SpectrumDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    explicit SpectrumDisplay (ToneShaperProcessor& processor);
    ~SpectrumDisplay() override;

    /** Pulls the latest frame from the engine and repaints (called by the internal timer). */
    void refresh();

    /** Which channel's tone controls the handles edit (0 = main/ch1, 1 = ch2). */
    void setEditChannel (int channel);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct HandleAttachments
    {
        std::unique_ptr<juce::ParameterAttachment> freq, level;
    };

    void timerCallback() override;
    void attachHandles();

    juce::Rectangle<float> plotArea() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForGainDb (float db) const;
    float gainDbForY (float y) const;
    float yForLevelDb (float dbfs) const;

    juce::Point<float> handlePosition (int band) const;
    int handleAt (juce::Point<float> p) const;
    float param (const juce::String& pid) const;

    ToneShaperProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    int editChannel = 0;
    std::array<HandleAttachments, 4> handles;
    int dragging = -1, hovered = -1;

    toneshaper::DisplayFrame shown;
    bool hasData = false;
    float squashMeter = 0.0f;

    juce::Image background;
};

} // namespace toneshaper::ui
