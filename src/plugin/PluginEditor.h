#pragma once

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "SpectrumDisplay.h"

#include <juce_audio_processors/juce_audio_processors.h>

class ToneShaperEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit ToneShaperEditor (ToneShaperProcessor&);
    ~ToneShaperEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct BandStrip
    {
        juce::Label title;
        juce::ToggleButton on, solo;
        Knob freq, level;
        std::unique_ptr<ButtonAttachment> onAttachment;
    };

    struct Group
    {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;

    void setupKnob (Knob&, const juce::String& label, const juce::String& paramId, juce::Colour colour);
    void setupToggle (juce::ToggleButton&, const juce::String& text, juce::Colour colour);
    void attachBandControls();
    void setEditChannel (int channel);
    void updateSoloButtons();
    void updateStereoControls();
    void layoutKnob (Knob&, juce::Rectangle<int> area);

    void showPresetMenu();
    void stepPreset (int delta);
    void savePresetAs();
    void openPresetFile();
    void refreshPresetName();

    ToneShaperProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    toneshaper::ui::LookAndFeel lnf;

    // Header
    juce::Label title;
    juce::TextButton presetButton, prevPreset { "<" }, nextPreset { ">" }, savePreset { "Save" };
    juce::ComboBox quality, stereoMode;
    juce::ToggleButton bypass;
    std::unique_ptr<ComboAttachment> qualityAttachment, stereoAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    toneshaper::ui::SpectrumDisplay display;

    // Tone strip
    std::array<BandStrip, 4> bands;
    juce::ToggleButton editCh1, editCh2;
    int editChannel = 0;
    int shownPresetGeneration = -1;

    // Main controls
    Knob amount, attack, release, squashCal, rangeLo, rangeHi, loCut, hiCut, link, focus, wetTrim, mix, output;
    juce::ToggleButton rangeFlip, sidechain, sidechainListen;
    juce::TextButton setSquash { "Set" }, setTrim { "Set" };
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    juce::Label meters, hint;

    std::vector<Group> groups;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneShaperEditor)
};
