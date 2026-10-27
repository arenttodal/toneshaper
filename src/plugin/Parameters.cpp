#include "Parameters.h"

namespace toneshaper::params
{

namespace
{
    constexpr int version = 1;

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";

        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    float parseHz (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const float v = t.getFloatValue();
        return t.contains ("k") ? v * 1000.0f : v;
    }

    juce::String formatSeconds (float seconds)
    {
        // Note: juce::String (float, 0) prints full precision, so round explicitly.
        if (seconds < 0.01f)
            return juce::String (seconds * 1000.0f, 1) + " ms";

        if (seconds < 1.0f)
            return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";

        return juce::String (seconds, 2) + " s";
    }

    juce::String formatDb (float db)
    {
        return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float start, float end, float norm) { return start * std::pow (end / start, norm); },
                 [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
    }

    using Attributes = juce::AudioParameterFloatAttributes;

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& pid, const juce::String& name,
                                                          juce::NormalisableRange<float> range, float def,
                                                          Attributes attributes = {})
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid, version }, name, range, def, attributes);
    }

    std::unique_ptr<juce::AudioParameterBool> makeBool (const juce::String& pid, const juce::String& name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid, version }, name, def);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const juce::String& pid, const juce::String& name,
                                                            const juce::StringArray& choices, int def)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid, version }, name, choices, def);
    }

    Attributes hzAttributes()
    {
        return Attributes().withStringFromValueFunction ([] (float v, int) { return formatHz (v); })
                           .withValueFromStringFunction ([] (const juce::String& t) { return parseHz (t); });
    }

    Attributes dbAttributes()
    {
        return Attributes().withStringFromValueFunction ([] (float v, int) { return formatDb (v); })
                           .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
    }

    Attributes percentAttributes()
    {
        return Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
                           .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
    }
}

const std::vector<PresetKey>& presetKeys()
{
    static const std::vector<PresetKey> keys = []
    {
        std::vector<PresetKey> k {
            { id::amount, "amount" },
            { id::attack, "attack" },
            { id::release, "release" },
            { id::freqLimLo, "freq lim lo" },
            { id::freqLimHi, "freq lim hi" },
            { id::freqLimFlip, "freq lim flip" },
            { id::loCut, "lo cut" },
            { id::hiCut, "hi cut" },
            { id::squashCal, "squash cal" },
            { id::wetTrim, "wet trim" },
            { id::mix, "mix" },
            { id::output, "output" },
            { id::stereoMode, "stereo mode" },
            { id::channelLink, "channel link" },
            { id::channelFocus, "channel focus" },
            { id::quality, "quality" },
            { id::scEnabled, "sc enabled" },
            { id::scListen, "sc listen" },
            { id::bandSolo, "band solo" },
            { id::bandSoloCh, "band solo ch" },
            { id::bandSoloBand, "band solo band" },
        };

        for (int b = 0; b < mapping::numBands; ++b)
        {
            const auto n = juce::String (b + 1);
            k.push_back ({ id::bandOn (b), "band " + n + " on" });
            k.push_back ({ id::bandFreq (b, 0), "freq " + n + " (main/ch1)" });
            k.push_back ({ id::bandLevel (b, 0), "level " + n + " (main/ch1)" });
            k.push_back ({ id::bandFreq (b, 1), "freq " + n + " (ch2)" });
            k.push_back ({ id::bandLevel (b, 1), "level " + n + " (ch2)" });
        }

        return k;
    }();

    return keys;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // 10.1 main
    layout.add (makeFloat (id::amount, "Amount", { 0.0f, 10.0f, 0.001f }, 4.0f,
                           Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })));
    layout.add (makeFloat (id::attack, "Attack", { 0.0f, 10.0f, 0.001f }, 1.5f,
                           Attributes().withStringFromValueFunction ([] (float v, int) { return formatSeconds (mapping::attackSeconds (v)); })));
    layout.add (makeFloat (id::release, "Release", { 0.0f, 8.0f, 0.001f }, 5.45f,
                           Attributes().withStringFromValueFunction ([] (float v, int) { return formatSeconds (mapping::releaseSeconds (v)); })));

    // 10.2 tone controls
    for (int b = 0; b < mapping::numBands; ++b)
    {
        const juce::String bandName = mapping::bandRanges[(size_t) b].name;
        layout.add (makeBool (id::bandOn (b), bandName + " On", true));

        for (int ch = 0; ch < 2; ++ch)
        {
            const juce::String suffix = ch == 0 ? "" : " Ch2";
            layout.add (makeFloat (id::bandFreq (b, ch), bandName + " Freq" + suffix, { 0.0f, 1.0f, 0.0001f },
                                   mapping::bandDefaultNorm (b),
                                   Attributes()
                                       .withStringFromValueFunction ([b] (float v, int) { return formatHz (mapping::bandNormToHz (b, v)); })
                                       .withValueFromStringFunction ([b] (const juce::String& t) { return mapping::bandHzToNorm (b, parseHz (t)); })));
            layout.add (makeFloat (id::bandLevel (b, ch), bandName + " Level" + suffix, { -10.0f, 10.0f, 0.001f }, 0.0f,
                                   Attributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 2); })));
        }
    }

    // 10.3 processing range and filters
    layout.add (makeFloat (id::freqLimLo, "Range Low", logRange (20.0f, 20000.0f), 20.0f, hzAttributes()));
    layout.add (makeFloat (id::freqLimHi, "Range High", logRange (20.0f, 20000.0f), 20000.0f, hzAttributes()));
    layout.add (makeBool (id::freqLimFlip, "Range Exclude", false));
    layout.add (makeFloat (id::loCut, "Low Cut", logRange (20.0f, 20000.0f), 20.0f,
                           Attributes().withStringFromValueFunction ([] (float v, int) { return v <= 20.5f ? juce::String ("Off") : formatHz (v); })
                                       .withValueFromStringFunction ([] (const juce::String& t) { return t.startsWithIgnoreCase ("off") ? 20.0f : parseHz (t); })));
    layout.add (makeFloat (id::hiCut, "High Cut", logRange (20.0f, 20000.0f), 20000.0f,
                           Attributes().withStringFromValueFunction ([] (float v, int) { return v >= 19990.0f ? juce::String ("Off") : formatHz (v); })
                                       .withValueFromStringFunction ([] (const juce::String& t) { return t.startsWithIgnoreCase ("off") ? 20000.0f : parseHz (t); })));

    // 10.4 output and calibration
    layout.add (makeFloat (id::squashCal, "Squash Cal", { -24.0f, 24.0f, 0.01f }, 0.0f, dbAttributes()));
    layout.add (makeFloat (id::wetTrim, "Wet Trim", { -24.0f, 24.0f, 0.01f }, 0.0f, dbAttributes()));
    layout.add (makeFloat (id::mix, "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes()));
    layout.add (makeFloat (id::output, "Output", { -24.0f, 24.0f, 0.01f }, 0.0f, dbAttributes()));

    // 10.5 stereo
    layout.add (makeChoice (id::stereoMode, "Stereo Mode", { "Linked", "M/S Split", "L/R Split" }, 0));
    layout.add (makeFloat (id::channelLink, "Channel Link", { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes()));
    layout.add (makeFloat (id::channelFocus, "Channel Focus", { 0.0f, 1.0f, 0.001f }, 0.5f,
                           Attributes().withStringFromValueFunction ([] (float v, int)
                           {
                               const int ch1 = juce::roundToInt (100.0f * std::min (1.0f, 2.0f * (1.0f - v)));
                               const int ch2 = juce::roundToInt (100.0f * std::min (1.0f, 2.0f * v));
                               return juce::String (ch1) + " / " + juce::String (ch2);
                           })));

    // 10.6 utility
    layout.add (makeBool (id::bypass, "Bypass", false));
    layout.add (makeChoice (id::quality, "Quality", { "Normal", "High", "Low Latency" }, 0));
    layout.add (makeBool (id::scEnabled, "Sidechain", false));
    layout.add (makeBool (id::scListen, "Sidechain Listen", false));
    layout.add (makeBool (id::bandSolo, "Band Solo", false));
    layout.add (makeChoice (id::bandSoloCh, "Band Solo Channel", { "Ch1", "Ch2" }, 0));
    layout.add (makeChoice (id::bandSoloBand, "Band Solo Band", { "Lows", "Lo-mids", "Hi-mids", "Highs" }, 0));

    return layout;
}

Reader::Reader (juce::AudioProcessorValueTreeState& s)
{
    auto get = [&s] (const juce::String& pid)
    {
        auto* p = s.getRawParameterValue (pid);
        jassert (p != nullptr);
        return p;
    };

    amount = get (id::amount);
    attack = get (id::attack);
    release = get (id::release);
    freqLimLo = get (id::freqLimLo);
    freqLimHi = get (id::freqLimHi);
    freqLimFlip = get (id::freqLimFlip);
    loCut = get (id::loCut);
    hiCut = get (id::hiCut);
    squashCal = get (id::squashCal);
    wetTrim = get (id::wetTrim);
    mix = get (id::mix);
    output = get (id::output);
    stereoMode = get (id::stereoMode);
    channelLink = get (id::channelLink);
    channelFocus = get (id::channelFocus);
    bypass = get (id::bypass);
    quality = get (id::quality);
    scEnabled = get (id::scEnabled);
    scListen = get (id::scListen);
    bandSolo = get (id::bandSolo);
    bandSoloCh = get (id::bandSoloCh);
    bandSoloBand = get (id::bandSoloBand);

    for (int b = 0; b < mapping::numBands; ++b)
    {
        bands[b].on = get (id::bandOn (b));

        for (int ch = 0; ch < 2; ++ch)
        {
            bands[b].freq[ch] = get (id::bandFreq (b, ch));
            bands[b].level[ch] = get (id::bandLevel (b, ch));
        }
    }
}

EngineParams Reader::read() const noexcept
{
    auto f = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto b = [&f] (const std::atomic<float>* a) { return f (a) >= 0.5f; };
    auto i = [&f] (const std::atomic<float>* a) { return juce::roundToInt (f (a)); };

    EngineParams p;
    p.amount = f (amount);
    p.attack = f (attack);
    p.release = f (release);

    for (int band = 0; band < mapping::numBands; ++band)
    {
        p.bandOn[(size_t) band] = b (bands[band].on);

        for (int ch = 0; ch < 2; ++ch)
        {
            p.bands[(size_t) ch][(size_t) band].freqNorm = f (bands[band].freq[ch]);
            p.bands[(size_t) ch][(size_t) band].level = f (bands[band].level[ch]);
        }
    }

    p.freqLimLo = f (freqLimLo);
    p.freqLimHi = f (freqLimHi);
    p.freqLimFlip = b (freqLimFlip);
    p.loCut = f (loCut);
    p.hiCut = f (hiCut);
    p.squashCal = f (squashCal);
    p.wetTrimDb = f (wetTrim);
    p.mix = f (mix) * 0.01f;
    p.outputDb = f (output);
    p.stereoMode = (StereoMode) juce::jlimit (0, 2, i (stereoMode));
    p.channelLink = f (channelLink) * 0.01f;
    p.channelFocus = f (channelFocus);
    p.bypass = b (bypass);
    p.quality = (mapping::Quality) juce::jlimit (0, 2, i (quality));
    p.scEnabled = b (scEnabled);
    p.scListen = b (scListen);
    p.bandSolo = b (bandSolo);
    p.bandSoloCh = i (bandSoloCh);
    p.bandSoloBand = i (bandSoloBand);
    return p;
}

} // namespace toneshaper::params
