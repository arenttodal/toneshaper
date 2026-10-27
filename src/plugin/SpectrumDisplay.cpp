#include "SpectrumDisplay.h"

#include "LookAndFeel.h"

namespace toneshaper::ui
{

namespace
{
    constexpr float minHz = 20.0f, maxHz = 20000.0f;
    constexpr float gainRangeDb = 18.0f;
    constexpr float levelTopDb = -6.0f, levelBottomDb = -96.0f;
    constexpr float handleRadius = 9.0f;

    float levelToDbInverse (float db)
    {
        // Inverse of mapping::bandLevelToDb.
        const float m = std::pow (juce::jlimit (0.0f, 1.0f, std::abs (db) / mapping::maxBandGainDb), 1.0f / 1.5f) * 10.0f;
        return db < 0.0f ? -m : m;
    }
}

SpectrumDisplay::SpectrumDisplay (ToneShaperProcessor& p)
    : processor (p), state (p.getState())
{
    attachHandles();
    startTimerHz (30);
}

SpectrumDisplay::~SpectrumDisplay()
{
    stopTimer();
}

void SpectrumDisplay::setEditChannel (int channel)
{
    if (channel != editChannel)
    {
        editChannel = channel;
        attachHandles();
        repaint();
    }
}

void SpectrumDisplay::attachHandles()
{
    for (int b = 0; b < 4; ++b)
    {
        auto& h = handles[(size_t) b];
        h.freq = std::make_unique<juce::ParameterAttachment> (*state.getParameter (params::id::bandFreq (b, editChannel)),
                                                              [this] (float) { repaint(); });
        h.level = std::make_unique<juce::ParameterAttachment> (*state.getParameter (params::id::bandLevel (b, editChannel)),
                                                               [this] (float) { repaint(); });
    }
}

float SpectrumDisplay::param (const juce::String& pid) const
{
    return state.getRawParameterValue (pid)->load();
}

juce::Rectangle<float> SpectrumDisplay::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedRight (34.0f)
                                     .withTrimmedTop (8.0f).withTrimmedBottom (18.0f);
}

float SpectrumDisplay::xForHz (float hz) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
}

float SpectrumDisplay::hzForX (float x) const
{
    const auto a = plotArea();
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float SpectrumDisplay::yForGainDb (float db) const
{
    const auto a = plotArea();
    return a.getCentreY() - a.getHeight() * 0.5f * juce::jlimit (-1.2f, 1.2f, db / gainRangeDb);
}

float SpectrumDisplay::gainDbForY (float y) const
{
    const auto a = plotArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * gainRangeDb;
}

float SpectrumDisplay::yForLevelDb (float dbfs) const
{
    const auto a = plotArea();
    const float t = (juce::jlimit (levelBottomDb, levelTopDb, dbfs) - levelBottomDb) / (levelTopDb - levelBottomDb);
    return a.getBottom() - t * a.getHeight();
}

juce::Point<float> SpectrumDisplay::handlePosition (int band) const
{
    const float hz = mapping::bandNormToHz (band, param (params::id::bandFreq (band, editChannel)));
    const float db = mapping::bandLevelToDb (param (params::id::bandLevel (band, editChannel)));
    return { xForHz (hz), yForGainDb (db) };
}

int SpectrumDisplay::handleAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = handleRadius * 1.8f;

    for (int b = 0; b < 4; ++b)
    {
        const float d = handlePosition (b).getDistanceFrom (p);

        if (d < bestDist)
        {
            bestDist = d;
            best = b;
        }
    }

    return best;
}

void SpectrumDisplay::resized()
{
    // Static grid, drawn once per size.
    const float scale = 2.0f;
    background = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) (getWidth() * scale)),
                              juce::jmax (1, (int) (getHeight() * scale)), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto a = plotArea();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setFont (juce::FontOptions (10.0f));

    for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float x = xForHz (hz);
        g.setColour (colours::panelEdge);
        g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
        g.setColour (colours::textDim);
        const auto label = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, a.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centred);
    }

    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yForGainDb ((float) db);
        g.setColour (db == 0 ? colours::panelEdge.brighter (0.35f) : colours::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (colours::gain.withAlpha (0.6f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (a.getRight() + 3.0f, y - 7.0f, 30.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    for (float db = -90.0f; db <= -10.0f; db += 20.0f)
    {
        g.setColour (colours::spectrum.withAlpha (0.6f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (0.0f, yForLevelDb (db) - 7.0f, a.getX() - 4.0f, 14.0f),
                    juce::Justification::centredRight);
    }
}

void SpectrumDisplay::timerCallback()
{
    refresh();
}

void SpectrumDisplay::refresh()
{
    auto& buffer = processor.getEngine().getDisplayBuffer();

    if (buffer.fetch())
    {
        const auto& f = buffer.readBuffer();

        if (f.valid)
        {
            if (! hasData)
                shown = f;

            // Ease the spectrum for readability; curves follow the engine directly.
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < DisplayFrame::numPoints; ++i)
                {
                    auto& s = shown.inputDb[(size_t) c][(size_t) i];
                    s += 0.35f * (f.inputDb[(size_t) c][(size_t) i] - s);
                }

            shown.gainDb = f.gainDb;
            shown.targetDb = f.targetDb;
            shown.numCurves = f.numCurves;
            squashMeter = f.squashMeterDb;
            hasData = true;
        }
    }

    repaint();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    const auto a = plotArea();
    g.saveState();
    g.reduceClipRegion (a.toNearestInt());

    // Processing range: dim what is not processed.
    {
        const float lo = std::min (param (params::id::freqLimLo), param (params::id::freqLimHi));
        const float hi = std::max (param (params::id::freqLimLo), param (params::id::freqLimHi));
        const bool flip = param (params::id::freqLimFlip) >= 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.35f));

        if (flip)
        {
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xForHz (lo), a.getY(), xForHz (hi), a.getBottom()));
        }
        else
        {
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (a.getX(), a.getY(), xForHz (lo), a.getBottom()));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xForHz (hi), a.getY(), a.getRight(), a.getBottom()));
        }
    }

    auto curvePath = [&] (const std::array<float, DisplayFrame::numPoints>& data, auto yFn)
    {
        juce::Path p;

        for (int i = 0; i < DisplayFrame::numPoints; ++i)
        {
            const float x = xForHz (DisplayFrame::frequencyAt (i));
            const float y = yFn (data[(size_t) i]);

            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }

        return p;
    };

    if (hasData)
    {
        const int curves = juce::jlimit (1, 2, shown.numCurves);

        // Input spectrum.
        for (int c = 0; c < curves; ++c)
        {
            auto p = curvePath (shown.inputDb[(size_t) c], [this] (float v) { return yForLevelDb (v); });
            auto fill = p;
            fill.lineTo (a.getRight(), a.getBottom());
            fill.lineTo (a.getX(), a.getBottom());
            fill.closeSubPath();
            const auto col = c == 0 ? colours::spectrum : colours::gain2;
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.28f), 0.0f, a.getY(),
                                                     col.withAlpha (0.04f), 0.0f, a.getBottom(), false));
            g.fillPath (fill);
            g.setColour (col.withAlpha (0.55f));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }

        // Tone target (dashed) for the channel being edited.
        {
            const int c = juce::jmin (editChannel, curves - 1);
            auto p = curvePath (shown.targetDb[(size_t) c], [this] (float v) { return yForGainDb (v); });
            juce::Path dashed;
            const float dashes[] { 5.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
            g.setColour (colours::target.withAlpha (0.8f));
            g.fillPath (dashed);
        }

        // Applied correction.
        for (int c = curves - 1; c >= 0; --c)
        {
            auto p = curvePath (shown.gainDb[(size_t) c], [this] (float v) { return yForGainDb (v); });
            const auto col = c == 0 ? colours::gain : colours::gain2;
            g.setColour (col.withAlpha (0.25f));
            g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    g.restoreState();

    // Band handles.
    for (int b = 0; b < 4; ++b)
    {
        const auto pos = handlePosition (b);
        const bool on = param (params::id::bandOn (b)) >= 0.5f;
        const auto col = colours::band (b);
        const float r = handleRadius * (b == hovered || b == dragging ? 1.15f : 1.0f);
        const auto circle = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pos);

        g.setColour (col.withAlpha (on ? 0.25f : 0.08f));
        g.fillEllipse (circle.expanded (4.0f));
        g.setColour (on ? col : col.withAlpha (0.35f));
        g.fillEllipse (circle);
        g.setColour (colours::background);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), circle, juce::Justification::centred);

        if (b == hovered || b == dragging)
        {
            const auto& fp = *state.getParameter (params::id::bandFreq (b, editChannel));
            const auto& lp = *state.getParameter (params::id::bandLevel (b, editChannel));
            const auto text = juce::String (mapping::bandRanges[(size_t) b].name) + "  " + fp.getCurrentValueAsText()
                              + "  " + lp.getCurrentValueAsText();
            auto box = juce::Rectangle<float> (150.0f, 18.0f).withCentre (pos.translated (0.0f, -22.0f));
            box = box.constrainedWithin (a);
            g.setColour (colours::background.withAlpha (0.85f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (text, box, juce::Justification::centred);
        }
    }

    // Squash meter (only meaningful above amount 7).
    if (param (params::id::amount) > 7.0f)
    {
        const auto meter = juce::Rectangle<float> (a.getRight() - 14.0f, a.getY() + 6.0f, 8.0f, a.getHeight() * 0.5f);
        g.setColour (colours::background.withAlpha (0.7f));
        g.fillRoundedRectangle (meter, 2.0f);
        const float t = juce::jlimit (0.0f, 1.0f, squashMeter / 12.0f);
        g.setColour (colours::squash);
        g.fillRoundedRectangle (meter.withTop (meter.getBottom() - t * meter.getHeight()), 2.0f);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("SQ " + juce::String (squashMeter, 1), juce::Rectangle<float> (meter.getX() - 50.0f, meter.getBottom() + 2.0f, 58.0f, 12.0f),
                    juce::Justification::centredRight);
    }
}

void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);

    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position);

    if (dragging >= 0)
    {
        handles[(size_t) dragging].freq->beginGesture();
        handles[(size_t) dragging].level->beginGesture();
    }
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;

    const float hz = hzForX (e.position.x);
    const float db = juce::jlimit (-mapping::maxBandGainDb, mapping::maxBandGainDb, gainDbForY (e.position.y));

    handles[(size_t) dragging].freq->setValueAsPartOfGesture (mapping::bandHzToNorm (dragging, hz));
    handles[(size_t) dragging].level->setValueAsPartOfGesture (levelToDbInverse (db));
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        handles[(size_t) dragging].freq->endGesture();
        handles[(size_t) dragging].level->endGesture();
    }

    dragging = -1;
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);

    if (h >= 0)
        handles[(size_t) h].level->setValueAsCompleteGesture (0.0f);
}

} // namespace toneshaper::ui
