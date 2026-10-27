#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    const juce::Identifier presetNameProperty { "presetName" };

    // Loudness the squash thresholds of section 7.3 are designed around.
    constexpr float squashReferenceLufs = -18.0f;
}

ToneShaperProcessor::ToneShaperProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state (*this, nullptr, "ToneShaper", toneshaper::params::createLayout()),
      reader (state)
{
    state.state.setProperty (presetNameProperty, "Default", nullptr);

    setLatencySamples (toneshaper::mapping::latencySamplesFor (toneshaper::mapping::Quality::normal, 48000.0));

    startTimerHz (10);
}

ToneShaperProcessor::~ToneShaperProcessor()
{
    stopTimer();
}

bool ToneShaperProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto main = layouts.getMainOutputChannelSet();

    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != main)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);

        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void ToneShaperProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);

    setLatencySamples (toneshaper::mapping::latencySamplesFor (getQuality(), sampleRate));
}

void ToneShaperProcessor::process (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;

    auto params = reader.read();
    params.bypass = params.bypass || forceBypass;

    auto main = getBusBuffer (buffer, true, 0);
    const int numChannels = juce::jmin (main.getNumChannels(), 2);

    if (numChannels == 0 || main.getNumSamples() == 0)
        return;

    const float* sidechain[2] = { nullptr, nullptr };
    int numSidechain = 0;

    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        numSidechain = juce::jmin (sc.getNumChannels(), 2);

        for (int c = 0; c < numSidechain; ++c)
            sidechain[c] = sc.getReadPointer (c);
    }

    engine.process (main.getArrayOfWritePointers(), numChannels,
                    numSidechain > 0 ? sidechain : nullptr, numSidechain,
                    main.getNumSamples(), params);
}

void ToneShaperProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void ToneShaperProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Keep running so the bypassed output stays latency-compensated and un-bypassing is seamless.
    process (buffer, true);
}

toneshaper::mapping::Quality ToneShaperProcessor::getQuality() const
{
    const auto v = state.getRawParameterValue (toneshaper::params::id::quality)->load();
    return (toneshaper::mapping::Quality) juce::jlimit (0, 2, juce::roundToInt (v));
}

double ToneShaperProcessor::getTailLengthSeconds() const
{
    return currentSampleRate > 0.0 ? getLatencySamples() / currentSampleRate : 0.0;
}

void ToneShaperProcessor::timerCallback()
{
    // Quality changes alter the latency; report it from the message thread.
    const int latency = toneshaper::mapping::latencySamplesFor (getQuality(), currentSampleRate > 0.0 ? currentSampleRate : 48000.0);

    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

juce::AudioProcessorParameter* ToneShaperProcessor::getBypassParameter() const
{
    return state.getParameter (toneshaper::params::id::bypass);
}

// Programs map onto the factory presets so hosts' program menus work too.
int ToneShaperProcessor::getNumPrograms()
{
    return toneshaper::presets::factoryPresets().size();
}

void ToneShaperProcessor::setCurrentProgram (int index)
{
    loadFactoryPreset (index);
}

const juce::String ToneShaperProcessor::getProgramName (int index)
{
    const auto& list = toneshaper::presets::factoryPresets();
    return juce::isPositiveAndBelow (index, list.size()) ? list[index].name : juce::String();
}

void ToneShaperProcessor::loadFactoryPreset (int index)
{
    const auto& list = toneshaper::presets::factoryPresets();

    if (! juce::isPositiveAndBelow (index, list.size()))
        return;

    if (toneshaper::presets::apply (list[index].json, state))
    {
        currentProgram = index;
        setPresetName (list[index].name);
    }
}

bool ToneShaperProcessor::loadPresetFile (const juce::File& file)
{
    const auto json = toneshaper::presets::loadFromFile (file);

    if (! toneshaper::presets::apply (json, state))
        return false;

    setPresetName (json.getProperty ("name", file.getFileNameWithoutExtension()).toString());
    return true;
}

bool ToneShaperProcessor::savePresetFile (const juce::File& file)
{
    const auto name = file.getFileNameWithoutExtension();

    if (! toneshaper::presets::saveToFile (toneshaper::presets::capture (name, state), file))
        return false;

    setPresetName (name);
    return true;
}

juce::String ToneShaperProcessor::getPresetName() const
{
    return state.state.getProperty (presetNameProperty, "Default").toString();
}

void ToneShaperProcessor::setPresetName (const juce::String& name)
{
    state.state.setProperty (presetNameProperty, name, nullptr);

    if (onPresetChanged)
        onPresetChanged();
}

bool ToneShaperProcessor::matchWetTrimToInput()
{
    const float in = engine.getInputLufs();
    const float wetLufs = engine.getWetLufs();

    if (in < -70.0f || wetLufs < -70.0f)
        return false;

    auto* trim = state.getParameter (toneshaper::params::id::wetTrim);
    trim->beginChangeGesture();
    trim->setValueNotifyingHost (trim->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, in - wetLufs)));
    trim->endChangeGesture();
    return true;
}

bool ToneShaperProcessor::calibrateSquashFromInput()
{
    const float in = engine.getInputLufs();

    if (in < -70.0f)
        return false;

    auto* cal = state.getParameter (toneshaper::params::id::squashCal);
    cal->beginChangeGesture();
    cal->setValueNotifyingHost (cal->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, squashReferenceLufs - in)));
    cal->endChangeGesture();
    return true;
}

void ToneShaperProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ToneShaperProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));

            if (onPresetChanged)
                onPresetChanged();
        }
    }
}

juce::AudioProcessorEditor* ToneShaperProcessor::createEditor()
{
    return new ToneShaperEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ToneShaperProcessor();
}
