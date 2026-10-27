#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** JSON presets in the format of spec section 10.7:
    { "name": ..., "category": int, "params": { "<preset key>": value, ... }, "tags": [...] }
    Parameters missing from a preset are reset to their defaults. */
namespace toneshaper::presets
{

struct Preset
{
    juce::String name;
    juce::var json;
};

const juce::Array<Preset>& factoryPresets();

juce::File userPresetDirectory();
juce::Array<juce::File> userPresetFiles();

/** Applies a preset object to the state. Returns false if it isn't a preset. */
bool apply (const juce::var& preset, juce::AudioProcessorValueTreeState& state);

/** Captures the current state as a preset object. */
juce::var capture (const juce::String& name, juce::AudioProcessorValueTreeState& state);

bool saveToFile (const juce::var& preset, const juce::File& file);
juce::var loadFromFile (const juce::File& file);

} // namespace toneshaper::presets
