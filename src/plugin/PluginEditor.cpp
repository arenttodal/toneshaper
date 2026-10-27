#include "PluginEditor.h"

#include "Presets.h"

namespace ids = toneshaper::params::id;
namespace colours = toneshaper::ui::colours;

namespace
{
    constexpr int editorWidth = 1040;
    constexpr int editorHeight = 690;
    constexpr int knobWidth = 66;

    void setParam (juce::AudioProcessorValueTreeState& state, const juce::String& pid, float value)
    {
        if (auto* p = state.getParameter (pid))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }
}

ToneShaperEditor::ToneShaperEditor (ToneShaperProcessor& p)
    : AudioProcessorEditor (p), processor (p), state (p.getState()), display (p)
{
    // Header ---------------------------------------------------------------------------------
    title.setText ("TONESHAPER", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (title);

    presetButton.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    savePreset.onClick = [this] { savePresetAs(); };

    for (auto* b : { &presetButton, &prevPreset, &nextPreset, &savePreset })
        addAndMakeVisible (b);

    quality.addItemList ({ "Normal", "High", "Low Latency" }, 1);
    stereoMode.addItemList ({ "Linked", "M/S Split", "L/R Split" }, 1);
    addAndMakeVisible (quality);
    addAndMakeVisible (stereoMode);
    qualityAttachment = std::make_unique<ComboAttachment> (state, ids::quality, quality);
    stereoAttachment = std::make_unique<ComboAttachment> (state, ids::stereoMode, stereoMode);
    quality.setTooltip ("Quality: FFT size / latency");

    setupToggle (bypass, "Bypass", colours::squash);
    bypassAttachment = std::make_unique<ButtonAttachment> (state, ids::bypass, bypass);

    addAndMakeVisible (display);

    // Tone strip -----------------------------------------------------------------------------
    for (int b = 0; b < 4; ++b)
    {
        auto& s = bands[(size_t) b];
        const auto colour = colours::band (b);

        s.title.setText (juce::String (b + 1) + "  " + toneshaper::mapping::bandRanges[(size_t) b].name, juce::dontSendNotification);
        s.title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        s.title.setColour (juce::Label::textColourId, colour);
        addAndMakeVisible (s.title);

        setupToggle (s.on, "On", colour);
        s.onAttachment = std::make_unique<ButtonAttachment> (state, ids::bandOn (b), s.on);

        setupToggle (s.solo, "Solo", colour);
        s.solo.setClickingTogglesState (false);
        s.solo.onClick = [this, b]
        {
            const bool soloing = state.getRawParameterValue (ids::bandSolo)->load() >= 0.5f;
            const int current = juce::roundToInt (state.getRawParameterValue (ids::bandSoloBand)->load());

            if (soloing && current == b)
            {
                setParam (state, ids::bandSolo, 0.0f);
            }
            else
            {
                setParam (state, ids::bandSoloBand, (float) b);
                setParam (state, ids::bandSoloCh, (float) editChannel);
                setParam (state, ids::bandSolo, 1.0f);
            }

            updateSoloButtons();
        };

        setupKnob (s.freq, "Freq", ids::bandFreq (b, 0), colour);
        setupKnob (s.level, "Level", ids::bandLevel (b, 0), colour);
    }

    setupToggle (editCh1, "Mid", colours::gain);
    setupToggle (editCh2, "Side", colours::gain2);
    editCh1.setRadioGroupId (1001);
    editCh2.setRadioGroupId (1001);
    editCh1.setToggleState (true, juce::dontSendNotification);
    editCh1.onClick = [this] { if (editCh1.getToggleState()) setEditChannel (0); };
    editCh2.onClick = [this] { if (editCh2.getToggleState()) setEditChannel (1); };

    // Main controls --------------------------------------------------------------------------
    setupKnob (amount, "Amount", ids::amount, colours::accent);
    amount.slider.setTooltip ("0-7: tonal shaping (level independent). 7-10: adds squash (frequency-dependent compression).");
    setupKnob (attack, "Attack", ids::attack, colours::accent);
    setupKnob (release, "Release", ids::release, colours::accent);
    setupKnob (squashCal, "Cal", ids::squashCal, colours::squash);
    setupKnob (rangeLo, "Low", ids::freqLimLo, colours::target);
    setupKnob (rangeHi, "High", ids::freqLimHi, colours::target);
    setupKnob (loCut, "Low Cut", ids::loCut, colours::spectrum);
    setupKnob (hiCut, "High Cut", ids::hiCut, colours::spectrum);
    setupKnob (link, "Link", ids::channelLink, colours::gain2);
    setupKnob (focus, "Focus", ids::channelFocus, colours::gain2);
    setupKnob (wetTrim, "Wet Trim", ids::wetTrim, colours::gain);
    setupKnob (mix, "Mix", ids::mix, colours::gain);
    setupKnob (output, "Output", ids::output, colours::gain);

    amount.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    amount.label.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    setupToggle (rangeFlip, "Exclude", colours::target);
    setupToggle (sidechain, "Sidechain", colours::accent);
    setupToggle (sidechainListen, "Listen", colours::accent);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ids::freqLimFlip, rangeFlip));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ids::scEnabled, sidechain));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ids::scListen, sidechainListen));

    setSquash.setTooltip ("Calibrate the squash threshold from the input loudness (play audio first)");
    setTrim.setTooltip ("Match the wet level to the input loudness (play audio first)");
    setSquash.onClick = [this]
    {
        if (! processor.calibrateSquashFromInput())
            hint.setText ("Play some audio first, then press Set.", juce::dontSendNotification);
    };
    setTrim.onClick = [this]
    {
        if (! processor.matchWetTrimToInput())
            hint.setText ("Play some audio first, then press Set.", juce::dontSendNotification);
    };
    addAndMakeVisible (setSquash);
    addAndMakeVisible (setTrim);

    meters.setFont (juce::FontOptions (12.0f));
    meters.setColour (juce::Label::textColourId, colours::textDim);
    meters.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (meters);

    hint.setInterceptsMouseClicks (false, false);
    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, colours::textDim);
    hint.setText ("Drag the numbered handles to shape the target. Double-click to reset.", juce::dontSendNotification);
    addAndMakeVisible (hint);

    refreshPresetName();
    updateStereoControls();
    updateSoloButtons();

    // Set last so every child (and every slider's text box) is rebuilt with it.
    setLookAndFeel (&lnf);
    setSize (editorWidth, editorHeight);
    startTimerHz (10);
}

ToneShaperEditor::~ToneShaperEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ToneShaperEditor::setupKnob (Knob& k, const juce::String& label, const juce::String& paramId, juce::Colour colour)
{
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.setDoubleClickReturnValue (true, (double) state.getParameter (paramId)->convertFrom0to1 (state.getParameter (paramId)->getDefaultValue()));
    addAndMakeVisible (k.slider);

    k.label.setText (label, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f));
    k.label.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<SliderAttachment> (state, paramId, k.slider);
}

void ToneShaperEditor::setupToggle (juce::ToggleButton& b, const juce::String& text, juce::Colour colour)
{
    b.setButtonText (text);
    b.setColour (juce::ToggleButton::tickColourId, colour);
    addAndMakeVisible (b);
}

void ToneShaperEditor::attachBandControls()
{
    for (int b = 0; b < 4; ++b)
    {
        auto& s = bands[(size_t) b];

        for (auto* k : { &s.freq, &s.level })
        {
            const auto pid = k == &s.freq ? ids::bandFreq (b, editChannel) : ids::bandLevel (b, editChannel);
            k->attachment.reset();
            k->attachment = std::make_unique<SliderAttachment> (state, pid, k->slider);
            auto* param = state.getParameter (pid);
            k->slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
        }
    }
}

void ToneShaperEditor::setEditChannel (int channel)
{
    if (channel == editChannel)
        return;

    editChannel = channel;
    attachBandControls();
    display.setEditChannel (channel);
    repaint();
}

void ToneShaperEditor::updateSoloButtons()
{
    const bool soloing = state.getRawParameterValue (ids::bandSolo)->load() >= 0.5f;
    const int current = juce::roundToInt (state.getRawParameterValue (ids::bandSoloBand)->load());

    for (int b = 0; b < 4; ++b)
        bands[(size_t) b].solo.setToggleState (soloing && current == b, juce::dontSendNotification);
}

void ToneShaperEditor::updateStereoControls()
{
    const int mode = juce::roundToInt (state.getRawParameterValue (ids::stereoMode)->load());
    const bool split = mode != 0;

    if (editCh1.isVisible() != split)
    {
        editCh1.setVisible (split);
        editCh2.setVisible (split);
        resized();
    }

    link.slider.setEnabled (split);
    focus.slider.setEnabled (split);

    editCh1.setButtonText (mode == 1 ? "Mid" : "Left");
    editCh2.setButtonText (mode == 1 ? "Side" : "Right");

    if (! split && editChannel != 0)
    {
        editCh1.setToggleState (true, juce::dontSendNotification);
        setEditChannel (0);
    }
}

void ToneShaperEditor::timerCallback()
{
    auto& engine = processor.getEngine();
    const float in = engine.getInputLufs();
    const float wetLufs = engine.getWetLufs();
    auto fmt = [] (float v) { return v <= -99.0f ? juce::String ("--") : juce::String (v, 1); };

    juce::String text = "In " + fmt (in) + " LUFS   Wet " + fmt (wetLufs) + " LUFS\n";

    if (state.getRawParameterValue (ids::amount)->load() > 7.0f)
        text << "Squash " << juce::String (engine.getSquashMeterDb(), 1) << " dB\n";
    else
        text << "Squash off (amount <= 7)\n";

    const auto q = (toneshaper::mapping::Quality) juce::jlimit (0, 2, quality.getSelectedItemIndex());
    const double sr = processor.getCurrentSampleRate() > 0.0 ? processor.getCurrentSampleRate() : 48000.0;
    const int latency = toneshaper::mapping::latencySamplesFor (q, sr);
    text << "Latency " << latency << " smp (" << juce::String (1000.0 * latency / sr, 1) << " ms)";

    meters.setText (text, juce::dontSendNotification);

    sidechainListen.setEnabled (sidechain.getToggleState());

    if (processor.getPresetGeneration() != shownPresetGeneration)
        refreshPresetName();

    updateStereoControls();
    updateSoloButtons();
}

// Presets ------------------------------------------------------------------------------------

void ToneShaperEditor::refreshPresetName()
{
    shownPresetGeneration = processor.getPresetGeneration();
    presetButton.setButtonText (processor.getPresetName());
}

void ToneShaperEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    juce::PopupMenu factory;
    const auto& list = toneshaper::presets::factoryPresets();

    for (int i = 0; i < list.size(); ++i)
        factory.addItem (1 + i, list[i].name, true, list[i].name == processor.getPresetName());

    menu.addSubMenu ("Factory", factory);

    const auto userFiles = toneshaper::presets::userPresetFiles();
    juce::PopupMenu user;

    for (int i = 0; i < userFiles.size(); ++i)
        user.addItem (1000 + i, userFiles[i].getFileNameWithoutExtension());

    menu.addSubMenu ("User", user, ! userFiles.isEmpty());
    menu.addSeparator();
    menu.addItem (2, "Save preset as...");
    menu.addItem (3, "Open preset file...");
    menu.addItem (4, "Show user preset folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe = juce::Component::SafePointer<ToneShaperEditor> (this), userFiles] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result >= 1000)
                                safe->processor.loadPresetFile (userFiles[result - 1000]);
                            else if (result == 2)
                                safe->savePresetAs();
                            else if (result == 3)
                                safe->openPresetFile();
                            else if (result == 4)
                            {
                                auto dir = toneshaper::presets::userPresetDirectory();
                                dir.createDirectory();
                                dir.revealToUser();
                            }
                            else
                                safe->processor.loadFactoryPreset (result - 1);
                        });
}

void ToneShaperEditor::stepPreset (int delta)
{
    const auto& list = toneshaper::presets::factoryPresets();
    int index = -1;

    for (int i = 0; i < list.size(); ++i)
        if (list[i].name == processor.getPresetName())
            index = i;

    index = index < 0 ? 0 : (index + delta + list.size()) % list.size();
    processor.loadFactoryPreset (index);
}

void ToneShaperEditor::savePresetAs()
{
    auto dir = toneshaper::presets::userPresetDirectory();
    dir.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save preset", dir.getChildFile (processor.getPresetName() + ".json"), "*.json");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<ToneShaperEditor> (this)] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (safe != nullptr && file != juce::File())
                                  safe->processor.savePresetFile (file.withFileExtension ("json"));
                          });
}

void ToneShaperEditor::openPresetFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Open preset", toneshaper::presets::userPresetDirectory(), "*.json");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<ToneShaperEditor> (this)] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (safe != nullptr && file.existsAsFile() && ! safe->processor.loadPresetFile (file))
                                  safe->hint.setText ("That file is not a ToneShaper preset.", juce::dontSendNotification);
                          });
}

// Layout -------------------------------------------------------------------------------------

void ToneShaperEditor::layoutKnob (Knob& k, juce::Rectangle<int> area)
{
    k.label.setBounds (area.removeFromTop (16));
    k.slider.setBounds (area);
}

void ToneShaperEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    for (const auto& group : groups)
    {
        const auto r = group.bounds.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        if (group.title.isNotEmpty())
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (group.title.toUpperCase(), group.bounds.reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft);
        }
    }
}

void ToneShaperEditor::resized()
{
    groups.clear();
    auto area = getLocalBounds().reduced (12, 8);

    // Header
    {
        auto header = area.removeFromTop (34);
        title.setBounds (header.removeFromLeft (170));
        bypass.setBounds (header.removeFromRight (80).reduced (0, 4));
        header.removeFromRight (8);
        stereoMode.setBounds (header.removeFromRight (120).reduced (0, 4));
        header.removeFromRight (8);
        quality.setBounds (header.removeFromRight (120).reduced (0, 4));

        auto presets = header.withSizeKeepingCentre (380, 26);
        prevPreset.setBounds (presets.removeFromLeft (28));
        savePreset.setBounds (presets.removeFromRight (56));
        nextPreset.setBounds (presets.removeFromRight (28));
        presetButton.setBounds (presets.reduced (4, 0));
    }

    area.removeFromTop (6);
    display.setBounds (area.removeFromTop (300));
    hint.setBounds (display.getBounds().withTrimmedLeft (40).withTrimmedBottom (20).removeFromBottom (16).removeFromLeft (520));
    area.removeFromTop (8);

    // Tone strip
    {
        auto strip = area.removeFromTop (124);
        auto editArea = strip.removeFromRight (70);
        const int bandWidth = strip.getWidth() / 4;

        for (int b = 0; b < 4; ++b)
        {
            auto box = strip.removeFromLeft (bandWidth).reduced (b == 0 ? 0 : 4, 0).withTrimmedRight (b == 3 ? 4 : 0);
            groups.push_back ({ {}, box });
            auto inner = box.reduced (8, 6);
            auto top = inner.removeFromTop (22);
            auto& s = bands[(size_t) b];
            s.solo.setBounds (top.removeFromRight (44));
            top.removeFromRight (4);
            s.on.setBounds (top.removeFromRight (36));
            s.title.setBounds (top);
            inner.removeFromTop (2);
            const int half = inner.getWidth() / 2;
            layoutKnob (s.freq, inner.removeFromLeft (half).withSizeKeepingCentre (knobWidth + 20, inner.getHeight()));
            layoutKnob (s.level, inner.withSizeKeepingCentre (knobWidth + 20, inner.getHeight()));
        }

        if (editCh1.isVisible())
            groups.push_back ({ "Edit", editArea.reduced (2, 0) });
        auto e = editArea.reduced (8, 0).withTrimmedTop (12).withSizeKeepingCentre (54, 60);
        editCh1.setBounds (e.removeFromTop (26));
        e.removeFromTop (8);
        editCh2.setBounds (e.removeFromTop (26));
    }

    area.removeFromTop (8);

    // Main controls: Amount on the left, two rows of groups on the right.
    {
        auto bottom = area;
        auto amountBox = bottom.removeFromLeft (150);
        groups.push_back ({ "Amount", amountBox });
        amount.label.setVisible (false);
        amount.slider.setBounds (amountBox.reduced (14, 24).withTrimmedTop (6));

        bottom.removeFromLeft (8);
        const int rowHeight = (bottom.getHeight() - 8) / 2;
        auto row1 = bottom.removeFromTop (rowHeight);
        bottom.removeFromTop (8);
        auto row2 = bottom;

        auto addGroup = [this] (juce::Rectangle<int>& row, const juce::String& name, int width)
        {
            auto box = row.removeFromLeft (width);
            row.removeFromLeft (8);
            groups.push_back ({ name, box });
            return box.reduced (8, 4).withTrimmedTop (14);
        };

        auto knobSlot = [] (juce::Rectangle<int>& r) { return r.removeFromLeft (knobWidth + 10); };

        {
            auto g = addGroup (row1, "Dynamics", 2 * (knobWidth + 10) + 16);
            layoutKnob (attack, knobSlot (g));
            layoutKnob (release, knobSlot (g));
        }
        {
            auto g = addGroup (row1, "Squash", (knobWidth + 10) + 70);
            layoutKnob (squashCal, knobSlot (g));
            setSquash.setBounds (g.withSizeKeepingCentre (50, 24));
        }
        {
            auto g = addGroup (row1, "Processing Range", 2 * (knobWidth + 10) + 90);
            layoutKnob (rangeLo, knobSlot (g));
            layoutKnob (rangeHi, knobSlot (g));
            rangeFlip.setBounds (g.withSizeKeepingCentre (72, 24));
        }
        {
            auto g = addGroup (row1, "Output Filters", row1.getWidth());
            layoutKnob (loCut, knobSlot (g));
            layoutKnob (hiCut, knobSlot (g));
        }
        {
            auto g = addGroup (row2, "Stereo", 2 * (knobWidth + 10) + 16);
            layoutKnob (link, knobSlot (g));
            layoutKnob (focus, knobSlot (g));
        }
        {
            auto g = addGroup (row2, "Output", 3 * (knobWidth + 10) + 70);
            layoutKnob (wetTrim, knobSlot (g));
            setTrim.setBounds (g.removeFromLeft (60).withSizeKeepingCentre (50, 24));
            layoutKnob (mix, knobSlot (g));
            layoutKnob (output, knobSlot (g));
        }
        {
            auto g = addGroup (row2, "Sidechain", 110);
            auto col = g.withSizeKeepingCentre (90, 60);
            sidechain.setBounds (col.removeFromTop (26));
            col.removeFromTop (8);
            sidechainListen.setBounds (col.removeFromTop (26));
        }
        {
            auto g = addGroup (row2, "Meters", row2.getWidth());
            meters.setBounds (g);
        }
    }

    repaint();
}
