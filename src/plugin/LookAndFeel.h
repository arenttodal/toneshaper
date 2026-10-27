#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace toneshaper::ui
{

namespace colours
{
    inline const juce::Colour background { 0xff121418 };
    inline const juce::Colour panel { 0xff1b1e24 };
    inline const juce::Colour panelEdge { 0xff2a2e37 };
    inline const juce::Colour text { 0xffd9dde6 };
    inline const juce::Colour textDim { 0xff7d8494 };
    inline const juce::Colour accent { 0xff3fd2c3 };
    inline const juce::Colour gain { 0xffffb454 };
    inline const juce::Colour gain2 { 0xff6fa8ff };
    inline const juce::Colour target { 0xffb48cff };
    inline const juce::Colour spectrum { 0xff8a9bb8 };
    inline const juce::Colour squash { 0xffff6b6b };

    /** One colour per tone band. */
    inline juce::Colour band (int b)
    {
        static const juce::Colour c[] { juce::Colour (0xffff7a59), juce::Colour (0xffffc145),
                                        juce::Colour (0xff4fd18b), juce::Colour (0xff59b7ff) };
        return c[juce::jlimit (0, 3, b)];
    }
}

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;

    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

} // namespace toneshaper::ui
