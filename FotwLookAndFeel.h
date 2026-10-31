#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Palette + font shared by the whole UI
//==============================================================================
namespace Fotw
{
    inline const juce::Colour bg      { 0xff101010 };   // matte near-black graphite
    inline const juce::Colour panel   { 0xff181818 };   // module background
    inline const juce::Colour panelHi { 0xff242424 };   // raised surfaces (knob caps, idle buttons)
    inline const juce::Colour edge    { 0xff383838 };   // hard grey outlines
    inline const juce::Colour text    { 0xffb8b8b8 };
    inline const juce::Colour red     { 0xffff1f3d };   // neon red
    inline const juce::Colour green   { 0xff39ff14 };   // toxic green

    // Monospaced font, so every label looks like a stencilled machine marking.
    inline juce::Font mono (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
class FotwLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FotwLookAndFeel()
    {
        setColour (juce::Label::textColourId, Fotw::text);
        setColour (juce::Slider::textBoxTextColourId, Fotw::text);
    }

    // Every Label (including the value text under each knob) uses the mono font.
    juce::Font getLabelFont (juce::Label& l) override
    {
        return Fotw::mono (12.0f, l.getFont().isBold());
    }

    // A flat, hard-edged rotary knob: dark cap, thin track ring, neon value arc, square-ended pointer.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
        const float dia = juce::jmin (area.getWidth(), area.getHeight());
        const auto  b   = area.withSizeKeepingCentre (dia, dia);
        const auto  c   = b.getCentre();
        const float r   = dia * 0.5f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);

        // knob cap
        g.setColour (Fotw::panelHi);
        g.fillEllipse (b.reduced (r * 0.28f));
        g.setColour (Fotw::edge);
        g.drawEllipse (b.reduced (r * 0.28f), 2.0f);

        // background track + value arc (angle 0 = 12 o'clock, clockwise)
        const float arcR = r - 4.0f;
        const juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Fotw::edge);
        g.strokePath (track, stroke);

        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (value, stroke);

        // pointer line on the cap
        const float r0 = r * 0.22f, r1 = r * 0.66f;
        g.setColour (accent);
        g.drawLine (c.x + r0 * std::sin (angle), c.y - r0 * std::cos (angle),
                    c.x + r1 * std::sin (angle), c.y - r1 * std::cos (angle), 3.0f);
    }

    // Rectangular latching button: dark when off, solid neon with black text when on.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool /*down*/) override
    {
        const auto accent = b.findColour (juce::ToggleButton::tickColourId);
        const auto r      = b.getLocalBounds().toFloat().reduced (2.0f);
        const bool on     = b.getToggleState();

        g.setColour (on ? accent : Fotw::panelHi);
        g.fillRect (r);
        g.setColour (on ? accent : (over ? Fotw::text : Fotw::edge));
        g.drawRect (r, 2.0f);

        g.setColour (on ? juce::Colours::black : Fotw::text);
        g.setFont (Fotw::mono (14.0f, true));
        g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred);
    }
};
