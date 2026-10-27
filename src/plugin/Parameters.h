#pragma once

#include "Params.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace toneshaper::params
{

namespace id
{
    inline const juce::String amount { "amount" };
    inline const juce::String attack { "attack" };
    inline const juce::String release { "release" };
    inline const juce::String freqLimLo { "freqLimLo" };
    inline const juce::String freqLimHi { "freqLimHi" };
    inline const juce::String freqLimFlip { "freqLimFlip" };
    inline const juce::String loCut { "loCut" };
    inline const juce::String hiCut { "hiCut" };
    inline const juce::String squashCal { "squashCal" };
    inline const juce::String wetTrim { "wetTrim" };
    inline const juce::String mix { "mix" };
    inline const juce::String output { "output" };
    inline const juce::String stereoMode { "stereoMode" };
    inline const juce::String channelLink { "channelLink" };
    inline const juce::String channelFocus { "channelFocus" };
    inline const juce::String bypass { "bypass" };
    inline const juce::String quality { "quality" };
    inline const juce::String scEnabled { "scEnabled" };
    inline const juce::String scListen { "scListen" };
    inline const juce::String bandSolo { "bandSolo" };
    inline const juce::String bandSoloCh { "bandSoloCh" };
    inline const juce::String bandSoloBand { "bandSoloBand" };

    /** band: 0..3, channel: 0 = main/ch1, 1 = ch2 */
    inline juce::String bandOn (int band) { return "band" + juce::String (band + 1) + "On"; }
    inline juce::String bandFreq (int band, int ch) { return "freq" + juce::String (band + 1) + "Ch" + juce::String (ch + 1); }
    inline juce::String bandLevel (int band, int ch) { return "level" + juce::String (band + 1) + "Ch" + juce::String (ch + 1); }
}

/** Parameter ID <-> key used in the JSON preset format (section 10.7). */
struct PresetKey
{
    juce::String paramId;
    juce::String presetKey;
};

const std::vector<PresetKey>& presetKeys();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Caches the raw atomic pointers once, then builds EngineParams lock-free on the audio thread. */
class Reader
{
public:
    explicit Reader (juce::AudioProcessorValueTreeState& state);

    EngineParams read() const noexcept;

private:
    struct Band
    {
        std::atomic<float>* freq[2] {};
        std::atomic<float>* level[2] {};
        std::atomic<float>* on = nullptr;
    };

    std::atomic<float>* amount, * attack, * release, * freqLimLo, * freqLimHi, * freqLimFlip,
                      * loCut, * hiCut, * squashCal, * wetTrim, * mix, * output, * stereoMode,
                      * channelLink, * channelFocus, * bypass, * quality, * scEnabled, * scListen,
                      * bandSolo, * bandSoloCh, * bandSoloBand;
    Band bands[mapping::numBands];
};

} // namespace toneshaper::params
