#include "LookAndFeel.h"

namespace toneshaper::ui
{

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.4f));
    setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
    setColour (juce::TextButton::buttonColourId, colours::panel);
    setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::background);
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::PopupMenu::headerTextColourId, colours::textDim);
    setColour (juce::TextEditor::backgroundColourId, colours::panel);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::CaretComponent::caretColourId, colours::accent);
    setColour (juce::AlertWindow::backgroundColourId, colours::panel);
    setColour (juce::AlertWindow::textColourId, colours::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float track = juce::jmax (2.5f, radius * 0.14f);
    const float arcRadius = radius - track * 0.5f;
    const auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = slider.isEnabled();

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::panelEdge);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters fill from the centre.
    const auto range = slider.getRange();
    const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float fromAngle = startAngle + zeroPos * (endAngle - startAngle);
    const float toAngle = startAngle + pos * (endAngle - startAngle);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         juce::jmin (fromAngle, toAngle), juce::jmax (fromAngle, toAngle), true);
    g.setColour (enabled ? fill : fill.withSaturation (0.0f).withAlpha (0.4f));
    g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobRadius = arcRadius - track * 1.3f;
    g.setColour (colours::panel.brighter (0.15f));
    g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));
    g.setColour (colours::panelEdge.brighter (0.2f));
    g.drawEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre), 1.0f);

    const auto tip = centre.getPointOnCircumference (knobRadius * 0.85f, toAngle);
    const auto base = centre.getPointOnCircumference (knobRadius * 0.3f, toAngle);
    g.setColour (enabled ? colours::text : colours::textDim);
    g.drawLine ({ base, tip }, juce::jmax (1.5f, radius * 0.06f));
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();
    const auto accent = button.findColour (juce::ToggleButton::tickColourId);

    g.setColour (on ? accent.withAlpha (0.9f) : colours::panel.brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (on ? accent : colours::panelEdge.brighter (0.2f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    g.setColour (on ? colours::background : (button.isEnabled() ? colours::text : colours::textDim));
    g.setFont (juce::FontOptions (juce::jmin (13.0f, bounds.getHeight() * 0.6f), juce::Font::bold));
    g.drawFittedText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred, 1);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& bgColour,
                                        bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bgColour;

    if (down)
        c = c.brighter (0.2f);
    else if (highlighted)
        c = c.brighter (0.08f);

    g.setColour (c);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (colours::panelEdge.brighter (0.2f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const float arrowX = (float) width - 14.0f, arrowY = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (arrowX - 4.0f, arrowY - 2.0f, arrowX + 4.0f, arrowY - 2.0f, arrowX, arrowY + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getLabelFont (juce::Label& label)
{
    return juce::FontOptions (juce::jmin (14.0f, (float) label.getHeight() * 0.85f));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (13.0f); }

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::FontOptions (juce::jmin (13.0f, (float) h * 0.6f), juce::Font::bold);
}

juce::Font LookAndFeel::getPopupMenuFont() { return juce::FontOptions (14.0f); }

juce::Slider::SliderLayout LookAndFeel::getSliderLayout (juce::Slider& slider)
{
    juce::Slider::SliderLayout layout;
    auto bounds = slider.getLocalBounds();

    if (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
    {
        layout.textBoxBounds = bounds.removeFromBottom (16);
        layout.sliderBounds = bounds;
    }
    else
    {
        layout.sliderBounds = bounds;
    }

    return layout;
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (juce::FontOptions (12.0f));
    l->setColour (juce::Label::textColourId, colours::textDim);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

} // namespace toneshaper::ui
