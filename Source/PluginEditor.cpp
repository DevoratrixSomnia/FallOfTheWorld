#include "PluginEditor.h"

//==============================================================================
FallOfTheWorldEditor::FallOfTheWorldEditor (FallOfTheWorldProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      kBuffer     (p.apvts, ParamIDs::bufferSize, "Buffer Size", Fotw::green),
      kGrainSize  (p.apvts, ParamIDs::grainSize,  "Grain Size",  Fotw::red),
      kDensity    (p.apvts, ParamIDs::density,    "Density",     Fotw::red),
      kSpray      (p.apvts, ParamIDs::spray,      "Spray",       Fotw::red),
      kPitchChaos (p.apvts, ParamIDs::pitchChaos, "Pitch Chaos", Fotw::red),
      kPostPitch  (p.apvts, ParamIDs::postPitch,  "Post-Pitch",  Fotw::green),
      kFeedback   (p.apvts, ParamIDs::feedback,   "Feedback",    Fotw::green),
      kSwarm      (p.apvts, ParamIDs::swarm,      "Swarm",       Fotw::green),
      kDegrade    (p.apvts, ParamIDs::degrade,    "Degrade",     Fotw::red),
      kRuin       (p.apvts, ParamIDs::ruin,       "Ruin",        Fotw::red),
      kDampen     (p.apvts, ParamIDs::dampen,     "Dampen",      Fotw::red),
      kSpace      (p.apvts, ParamIDs::space,      "Space",       Fotw::green),
      kDryWet     (p.apvts, ParamIDs::dryWet,     "Dry/Wet",     Fotw::green),
      kOutput     (p.apvts, ParamIDs::output,     "Output",      Fotw::green),
      grainField  (p)
{
    setLookAndFeel (&laf);

    // Toggle buttons: neon colour when switched on, linked to their parameters.
    freezeButton.setButtonText ("Freeze");
    freezeButton.setColour (juce::ToggleButton::tickColourId, Fotw::green);
    reverseButton.setButtonText ("Reverse");
    reverseButton.setColour (juce::ToggleButton::tickColourId, Fotw::green);
    freezeAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamIDs::freeze,  freezeButton);
    reverseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamIDs::reverse, reverseButton);

    for (auto* c : std::initializer_list<juce::Component*> {
             &freezeButton, &reverseButton, &kBuffer,
             &kGrainSize, &kDensity, &kSpray, &kPitchChaos,
             &kPostPitch, &kFeedback, &kSwarm,
             &kDegrade, &kRuin, &kDampen,
             &kSpace, &kDryWet, &kOutput,
             &entropy, &grainField })
        addAndMakeVisible (c);

    // Click -> randomise the parameters (the processor does the work).
    entropy.onClick = [this] { proc.randomizeEntropy(); };

    setSize (1040, 760);
    startTimerHz (30);   // repaint the grain display 30x per second
}

FallOfTheWorldEditor::~FallOfTheWorldEditor()
{
    setLookAndFeel (nullptr);   // must happen before `laf` is destroyed
}

//==============================================================================
void FallOfTheWorldEditor::drawModule (juce::Graphics& g, const Module& m) const
{
    const auto r = m.bounds.toFloat();

    g.setColour (Fotw::panel);
    g.fillRect (r);
    g.setColour (Fotw::edge);
    g.drawRect (r, 2.0f);

    // title tab, hard-edged, in the module's accent colour
    g.setFont (Fotw::mono (12.0f, true));
    const int tabW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), m.title) + 20;
    const auto tab = juce::Rectangle<float> (r.getX(), r.getY(), (float) tabW, 20.0f);
    g.setColour (m.accent);
    g.fillRect (tab);
    g.setColour (juce::Colours::black);
    g.drawText (m.title, tab, juce::Justification::centred);

    // rivets in the corners
    g.setColour (Fotw::edge);
    for (auto p : { juce::Point<float> (r.getRight() - 8.0f, r.getY() + 8.0f),
                    juce::Point<float> (r.getX() + 8.0f, r.getBottom() - 8.0f),
                    juce::Point<float> (r.getRight() - 8.0f, r.getBottom() - 8.0f) })
        g.fillRect (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
}

void FallOfTheWorldEditor::paint (juce::Graphics& g)
{
    g.fillAll (Fotw::bg);

    // faint scanlines for a dead-CRT / concrete texture
    g.setColour (juce::Colours::white.withAlpha (0.018f));
    for (int y = 0; y < getHeight(); y += 4)
        g.drawHorizontalLine (y, 0.0f, (float) getWidth());

    // header
    g.setColour (Fotw::red);
    g.setFont (Fotw::mono (30.0f, true));
    g.drawText ("FALL OF THE WORLD", headerArea.withTrimmedBottom (6), juce::Justification::centredLeft);
    g.setColour (Fotw::text);
    g.setFont (Fotw::mono (12.0f));
    g.drawText ("GRANULAR DESTRUCTOR  //  v0.1", headerArea.withTrimmedBottom (6), juce::Justification::centredRight);
    g.setColour (Fotw::red);
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 4, headerArea.getWidth(), 3);

    for (const auto& m : modules)
        drawModule (g, m);

    // small engraved notes inside the master strip
    g.setColour (Fotw::text.withAlpha (0.7f));
    g.setFont (Fotw::mono (11.0f));
    g.drawText ("SPACE: LONG-TAIL DIFFUSER",   masterArea.withTrimmedTop (30).reduced (18, 0), juce::Justification::centredLeft);
    g.drawText ("LIMITER: BRICKWALL @ 0 dBFS", masterArea.withTrimmedTop (30).reduced (18, 0), juce::Justification::centredRight);
}

//==============================================================================
void FallOfTheWorldEditor::resized()
{
    modules.clear();

    auto area = getLocalBounds().reduced (12);
    headerArea = area.removeFromTop (52);
    area.removeFromTop (8);
    masterArea = area.removeFromBottom (150);
    area.removeFromBottom (10);

    auto leftCol   = area.removeFromLeft (316);
    auto rightCol  = area.removeFromRight (316);
    auto centerCol = area.reduced (10, 0);

    // The usable area inside a module box (below the title tab, with padding).
    auto contentOf = [] (juce::Rectangle<int> r) { return r.withTrimmedTop (28).reduced (8); };

    // Splits a rectangle into 3 equal columns and places 3 knobs.
    auto placeThree = [] (juce::Rectangle<int> c, Knob& a, Knob& b, Knob& d)
    {
        const int w = c.getWidth() / 3;
        a.setBounds (c.removeFromLeft (w));
        b.setBounds (c.removeFromLeft (w));
        d.setBounds (c);
    };

    // ---------------- left column: INPUT / BUFFER, GRANULAR ----------------
    {
        auto inputRect = leftCol.removeFromTop (170);
        leftCol.removeFromTop (10);
        auto granRect = leftCol;

        modules.push_back ({ inputRect, "01 INPUT / BUFFER", Fotw::green });
        modules.push_back ({ granRect,  "02 GRANULAR ENGINE", Fotw::red });

        auto c = contentOf (inputRect);
        auto toggles = c.removeFromLeft (108);
        freezeButton.setBounds  (toggles.removeFromTop (toggles.getHeight() / 2).reduced (0, 4));
        reverseButton.setBounds (toggles.reduced (0, 4));
        kBuffer.setBounds (c);

        auto g = contentOf (granRect);
        auto row1 = g.removeFromTop (g.getHeight() / 2);
        kGrainSize.setBounds (row1.removeFromLeft (row1.getWidth() / 2));
        kDensity.setBounds   (row1);
        kSpray.setBounds      (g.removeFromLeft (g.getWidth() / 2));
        kPitchChaos.setBounds (g);
    }

    // ---------------- right column: FEEDBACK, CHARACTER, GRAIN FIELD ----------------
    {
        auto fbRect = rightCol.removeFromTop (170);
        rightCol.removeFromTop (10);
        auto chRect = rightCol.removeFromTop (170);
        rightCol.removeFromTop (10);
        auto fieldRect = rightCol;

        modules.push_back ({ fbRect,    "03 FEEDBACK / MOD", Fotw::green });
        modules.push_back ({ chRect,    "04 CHARACTER",      Fotw::red });
        modules.push_back ({ fieldRect, "GRAIN FIELD",       Fotw::green });

        placeThree (contentOf (fbRect), kPostPitch, kFeedback, kSwarm);
        placeThree (contentOf (chRect), kDegrade, kRuin, kDampen);
        grainField.setBounds (contentOf (fieldRect));
    }

    // ---------------- centre: the massive ENTROPY button ----------------
    {
        const int size = juce::jmin (centerCol.getWidth(), centerCol.getHeight() - 60, 340);
        entropy.setBounds (juce::Rectangle<int> (size, size).withCentre (centerCol.getCentre()));
    }

    // ---------------- bottom strip: MASTER ----------------
    {
        modules.push_back ({ masterArea, "05 MASTER / SPACE", Fotw::green });
        auto c = contentOf (masterArea).withSizeKeepingCentre (510, contentOf (masterArea).getHeight());
        kSpace.setBounds  (c.removeFromLeft (170));
        kDryWet.setBounds (c.removeFromLeft (170));
        kOutput.setBounds (c);
    }
}
