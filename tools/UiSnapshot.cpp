// Developer tool: runs audio through the plugin processor and renders its editor to a PNG,
// so UI changes can be checked without a DAW or an audio device.
//   toneshaper_ui_snapshot <output.png> [amount] [stereoMode]

#include "Filters.h"
#include "PluginProcessor.h"
#include "SpectrumDisplay.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshot.png"));
    const float amount = argc > 2 ? (float) std::atof (argv[2]) : 6.0f;
    const int stereoMode = argc > 3 ? std::atoi (argv[3]) : 0;

    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    auto& p = dynamic_cast<ToneShaperProcessor&> (*proc);
    const double fs = 48000.0;
    const int block = 512;
    p.setPlayConfigDetails (2, 2, fs, block);
    p.prepareToPlay (fs, block);

    auto set = [&p] (const juce::String& id, float v)
    {
        auto* param = p.getState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    };

    set ("amount", amount);
    set ("level1Ch1", 3.0f);
    set ("level4Ch1", -2.0f);
    set ("stereoMode", (float) stereoMode);
    set ("freqLimLo", 40.0f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    juce::Component* display = nullptr;

    for (auto* c : editor->getChildren())
        if (dynamic_cast<toneshaper::ui::SpectrumDisplay*> (c) != nullptr)
            display = c;

    // Pink-ish noise with a resonance at 2.5 kHz and a boomy 200 Hz region.
    juce::Random rng (42);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    float pinkL = 0.0f, pinkR = 0.0f;
    auto peak = [fs] (double f0, double q, double gainDb)
    {
        const double a = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
        const double alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha / a;
        toneshaper::Biquad b;
        b.b0 = (float) ((1.0 + alpha * a) / a0);
        b.b1 = b.a1 = (float) (-2.0 * std::cos (w0) / a0);
        b.b2 = (float) ((1.0 - alpha * a) / a0);
        b.a2 = (float) ((1.0 - alpha / a) / a0);
        return b;
    };

    auto peakL = peak (2500.0, 3.0, 12.0), peakR = peakL;
    auto boomL = peak (200.0, 1.0, 6.0), boomR = boomL;

    for (int i = 0; i < (int) (fs * 3) / block; ++i)
    {
        for (int n = 0; n < block; ++n)
        {
            pinkL = 0.98f * pinkL + 0.2f * (rng.nextFloat() * 2.0f - 1.0f);
            pinkR = 0.98f * pinkR + 0.2f * (rng.nextFloat() * 2.0f - 1.0f);
            buffer.setSample (0, n, boomL.process (peakL.process (0.3f * pinkL)));
            buffer.setSample (1, n, boomR.process (peakR.process (0.3f * pinkR)));
        }

        p.processBlock (buffer, midi);

        if (display != nullptr)
            static_cast<toneshaper::ui::SpectrumDisplay*> (display)->refresh();
    }

    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::FileOutputStream stream (out);
    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    editor.reset();
    return 0;
}
