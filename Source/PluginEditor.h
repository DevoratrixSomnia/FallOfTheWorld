#pragma once

#include "PluginProcessor.h"
#include "FotwLookAndFeel.h"

//==============================================================================
// A labelled rotary knob that is automatically linked to one plugin parameter.
//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const char* paramID,
          const juce::String& name, juce::Colour accent)
    {
        label.setText (name.toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (Fotw::mono (12.0f, true));
        label.setColour (juce::Label::textColourId, Fotw::text);
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::textBoxTextColourId, accent);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);

        // Links the slider to the parameter (both directions, plus host automation and double-click reset).
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r);
    }

private:
    juce::Label  label;
    juce::Slider slider;   // declared BEFORE the attachment, so the attachment is destroyed first
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// The big central ENTROPY button.
//==============================================================================
class EntropyButton : public juce::Button, private juce::Timer
{
public:
    EntropyButton() : juce::Button ("ENTROPY") {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        const auto accent = over ? Fotw::red.brighter (0.25f) : Fotw::red;

        // base + flash (the flash decays after a click)
        g.setColour (juce::Colour (0xff0b0b0b));
        g.fillRect (b);
        g.setColour (Fotw::red.withAlpha (flash * 0.9f));
        g.fillRect (b);

        // diagonal warning-stripe hatching, clipped to the button
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (b.toNearestInt());
            g.setColour (Fotw::red.withAlpha (0.10f));
            for (float x = b.getX() - b.getHeight(); x < b.getRight(); x += 14.0f)
                g.drawLine (x, b.getBottom(), x + b.getHeight(), b.getY(), 1.0f);
        }

        // thick border + corner brackets
        g.setColour (accent);
        g.drawRect (b, down ? 7.0f : 5.0f);
        const float k = 26.0f;
        g.setColour (flash > 0.5f ? juce::Colours::black : Fotw::text);
        for (int cx = 0; cx < 2; ++cx)
            for (int cy = 0; cy < 2; ++cy)
            {
                const float px = cx ? b.getRight() - 16.0f : b.getX() + 16.0f;
                const float py = cy ? b.getBottom() - 16.0f : b.getY() + 16.0f;
                const float dx = cx ? -k : k, dy = cy ? -k : k;
                g.drawLine (px, py, px + dx, py, 2.0f);
                g.drawLine (px, py, px, py + dy, 2.0f);
            }

        // text
        const float off = down ? 2.0f : 0.0f;
        g.setColour (flash > 0.5f ? juce::Colours::black : juce::Colours::white);
        g.setFont (Fotw::mono (b.getHeight() * 0.15f, true));
        g.drawText ("ENTROPY", b.translated (off, off), juce::Justification::centred);

        g.setColour (flash > 0.5f ? juce::Colours::black : Fotw::text);
        g.setFont (Fotw::mono (11.0f));
        g.drawText ("RANDOMIZE  GRANULAR / FEEDBACK / DISTORTION",
                    b.withTrimmedTop (b.getHeight() * 0.62f).withHeight (16.0f), juce::Justification::centred);
        g.drawText ("OUTPUT + DRY/WET ARE SPARED",
                    b.withTrimmedTop (b.getHeight() * 0.62f + 18.0f).withHeight (16.0f), juce::Justification::centred);
    }

    void clicked() override { flash = 1.0f; startTimerHz (60); }

private:
    void timerCallback() override
    {
        flash *= 0.86f;
        if (flash < 0.01f) { flash = 0.0f; stopTimer(); }
        repaint();
    }

    float flash = 0.0f;
};

//==============================================================================
// Small live display: every living grain is a square. Horizontal = where in the
// memory it is reading (left = oldest, right = newest), vertical = its pitch.
//==============================================================================
class GrainField : public juce::Component
{
public:
    explicit GrainField (FallOfTheWorldProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0b0b));
        g.fillRect (r);

        g.setColour (Fotw::edge.withAlpha (0.6f));
        for (int i = 1; i < 4; ++i)
        {
            g.drawVerticalLine   ((int) (r.getX() + r.getWidth()  * (float) i / 4.0f), r.getY(), r.getBottom());
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
        }

        for (auto& v : proc.grainView)
        {
            const float x = v.x.load (std::memory_order_relaxed);
            if (x < 0.0f) continue;                                   // unused slot
            const float y = v.y.load (std::memory_order_relaxed);
            const float e = v.env.load (std::memory_order_relaxed);
            const float s = 3.0f + e * 7.0f;                         // louder part of the grain = bigger square
            g.setColour (Fotw::green.withAlpha (0.15f + 0.85f * e));
            g.fillRect (r.getX() + x * r.getWidth() - s * 0.5f, r.getY() + y * r.getHeight() - s * 0.5f, s, s);
        }

        g.setColour (Fotw::text.withAlpha (0.7f));
        g.setFont (Fotw::mono (10.0f));
        g.drawText ("OLD", getLocalBounds().reduced (4), juce::Justification::bottomLeft);
        g.drawText ("NOW", getLocalBounds().reduced (4), juce::Justification::bottomRight);
        g.drawText ("HI",  getLocalBounds().reduced (4), juce::Justification::topLeft);

        g.setColour (Fotw::edge);
        g.drawRect (r, 1.0f);
    }

private:
    FallOfTheWorldProcessor& proc;
};

//==============================================================================
class FallOfTheWorldEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FallOfTheWorldEditor (FallOfTheWorldProcessor&);
    ~FallOfTheWorldEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { grainField.repaint(); }

    struct Module { juce::Rectangle<int> bounds; juce::String title; juce::Colour accent; };
    void drawModule (juce::Graphics&, const Module&) const;

    // NOTE: members are created in this order and destroyed in reverse.
    // The look-and-feel is first, so it is destroyed last (after every component that uses it).
    FotwLookAndFeel laf;
    FallOfTheWorldProcessor& proc;

    // 1. input & buffer
    juce::ToggleButton freezeButton, reverseButton;
    Knob kBuffer;
    // 2. granular
    Knob kGrainSize, kDensity, kSpray, kPitchChaos;
    // 3. feedback & modulation
    Knob kPostPitch, kFeedback, kSwarm;
    // 4. character
    Knob kDegrade, kRuin, kDampen;
    // 5. master
    Knob kSpace, kDryWet, kOutput;

    EntropyButton entropy;
    GrainField grainField;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt, reverseAtt;

    std::vector<Module> modules;
    juce::Rectangle<int> headerArea, masterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FallOfTheWorldEditor)
};
