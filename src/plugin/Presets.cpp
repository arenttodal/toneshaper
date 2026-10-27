#include "Presets.h"

#include "Parameters.h"

namespace toneshaper::presets
{

namespace
{
    // Values are in parameter storage units (section 10): band frequencies are normalised 0..1,
    // levels -10..+10, mix and link in percent, choices as indices.
    const char* const factoryJson = R"json([
  { "name": "Default", "category": 0, "params": {}, "tags": ["factory presets"] },

  { "name": "Gentle Balance", "category": 0,
    "params": { "amount": 3.0, "attack": 2.0, "release": 5.5 },
    "tags": ["factory presets", "general"] },

  { "name": "Vocal Smooth", "category": 1,
    "params": { "amount": 5.5, "attack": 1.2, "release": 4.5,
                "level 2 (main/ch1)": -2.5, "level 3 (main/ch1)": -1.5, "freq lim lo": 120.0 },
    "tags": ["factory presets", "vocal"] },

  { "name": "Vocal Presence", "category": 1,
    "params": { "amount": 4.5, "attack": 1.5, "release": 4.0,
                "level 3 (main/ch1)": 2.5, "level 1 (main/ch1)": -2.0, "lo cut": 70.0 },
    "tags": ["factory presets", "vocal"] },

  { "name": "Acoustic Guitar Bloom", "category": 2,
    "params": { "amount": 4.471, "attack": 2.523, "release": 5.0,
                "level 2 (main/ch1)": -3.0, "level 4 (main/ch1)": 2.0 },
    "tags": ["factory presets", "guitar"] },

  { "name": "Bass Definition", "category": 2,
    "params": { "amount": 5.0, "attack": 2.0, "release": 5.0, "quality": 1,
                "level 1 (main/ch1)": 2.0, "level 2 (main/ch1)": -3.0, "freq lim hi": 3000.0 },
    "tags": ["factory presets", "bass"] },

  { "name": "De-Box Lo-Mids", "category": 2,
    "params": { "amount": 6.0, "attack": 2.0, "release": 5.0,
                "freq 2 (main/ch1)": 0.502, "level 2 (main/ch1)": -5.0 },
    "tags": ["factory presets", "guitar", "drums"] },

  { "name": "Bright Air", "category": 0,
    "params": { "amount": 4.0, "level 4 (main/ch1)": 5.0, "level 2 (main/ch1)": -1.0 },
    "tags": ["factory presets", "general"] },

  { "name": "Mix Bus Polish", "category": 3,
    "params": { "amount": 3.5, "attack": 3.0, "release": 6.0, "mix": 80.0 },
    "tags": ["factory presets", "mix bus"] },

  { "name": "Drum Bus Squash", "category": 3,
    "params": { "amount": 8.5, "attack": 1.0, "release": 3.0, "mix": 70.0, "lo cut": 30.0 },
    "tags": ["factory presets", "drums", "squash"] },

  { "name": "Full Squash", "category": 3,
    "params": { "amount": 10.0, "attack": 1.5, "release": 4.0, "squash cal": 3.0, "mix": 60.0 },
    "tags": ["factory presets", "squash"] },

  { "name": "Mastering Subtle", "category": 4,
    "params": { "amount": 2.5, "attack": 3.5, "release": 6.5, "quality": 1,
                "stereo mode": 1, "channel link": 80.0, "channel focus": 0.4 },
    "tags": ["factory presets", "mastering"] },

  { "name": "Side Air (M/S)", "category": 4,
    "params": { "amount": 5.0, "stereo mode": 1, "channel link": 50.0, "channel focus": 0.75,
                "level 4 (ch2)": 4.0, "level 1 (ch2)": -3.0 },
    "tags": ["factory presets", "mastering", "stereo"] },

  { "name": "Tracking (Low Latency)", "category": 0,
    "params": { "amount": 4.0, "attack": 1.0, "release": 4.0, "quality": 2 },
    "tags": ["factory presets", "tracking"] }
])json";
}

const juce::Array<Preset>& factoryPresets()
{
    static const juce::Array<Preset> presets = []
    {
        juce::Array<Preset> list;
        const auto parsed = juce::JSON::parse (factoryJson);

        if (auto* arr = parsed.getArray())
            for (const auto& p : *arr)
                list.add ({ p.getProperty ("name", "Untitled").toString(), p });

        jassert (! list.isEmpty());
        return list;
    }();

    return presets;
}

juce::File userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Audio/Presets/Arent Todal/ToneShaper");
}

juce::Array<juce::File> userPresetFiles()
{
    auto files = userPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();
    return files;
}

bool apply (const juce::var& preset, juce::AudioProcessorValueTreeState& state)
{
    const auto values = preset.getProperty ("params", juce::var());

    if (! values.isObject())
        return false;

    for (const auto& key : params::presetKeys())
    {
        auto* param = state.getParameter (key.paramId);

        if (param == nullptr)
            continue;

        const auto v = values.getProperty (key.presetKey, juce::var());
        const float normalised = v.isVoid() ? param->getDefaultValue()
                                            : param->convertTo0to1 ((float) static_cast<double> (v));

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalised);
        param->endChangeGesture();
    }

    return true;
}

juce::var capture (const juce::String& name, juce::AudioProcessorValueTreeState& state)
{
    auto* values = new juce::DynamicObject();

    for (const auto& key : params::presetKeys())
        if (auto* param = state.getParameter (key.paramId))
            values->setProperty (key.presetKey, (double) param->convertFrom0to1 (param->getValue()));

    auto* root = new juce::DynamicObject();
    root->setProperty ("name", name);
    root->setProperty ("category", 0);
    root->setProperty ("params", juce::var (values));
    root->setProperty ("tags", juce::Array<juce::var> { "user" });
    return juce::var (root);
}

bool saveToFile (const juce::var& preset, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (preset, false));
}

juce::var loadFromFile (const juce::File& file)
{
    return juce::JSON::parse (file);
}

} // namespace toneshaper::presets
