#pragma once

#include "Parameters.h"
#include "ToneShaperEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

class ToneShaperProcessor final : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    ToneShaperProcessor();
    ~ToneShaperProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    toneshaper::ToneShaperEngine& getEngine() noexcept { return engine; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    // Presets ---------------------------------------------------------------------------------
    void loadFactoryPreset (int index);
    bool loadPresetFile (const juce::File& file);
    bool savePresetFile (const juce::File& file);
    juce::String getPresetName() const;
    std::function<void()> onPresetChanged; // message thread

    // "Set" buttons (sections 7.3 and 9.3) --------------------------------------------------
    /** Matches the wet signal's short-term loudness to the input. Returns false without a
        usable measurement (play some audio first). */
    bool matchWetTrimToInput();

    /** Calibrates the squash threshold from the input's short-term loudness. */
    bool calibrateSquashFromInput();

private:
    void timerCallback() override;
    toneshaper::mapping::Quality getQuality() const;
    void process (juce::AudioBuffer<float>& buffer, bool forceBypass);
    void setPresetName (const juce::String& name);

    juce::AudioProcessorValueTreeState state;
    toneshaper::params::Reader reader;
    toneshaper::ToneShaperEngine engine;
    double currentSampleRate = 0.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneShaperProcessor)
};
