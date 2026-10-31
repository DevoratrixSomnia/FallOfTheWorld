#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Tweakable constants
//==============================================================================
namespace
{
    // Extra gain inside the feedback loop. Granular playback with overlap
    // normalisation loses a little level, so this makes "100%" sit close to the
    // edge of self-oscillation. Raise it if >100% doesn't howl enough for you.
    constexpr float kFeedbackMakeup = 1.15f;

    constexpr float kHalfPi = juce::MathConstants<float>::halfPi;
    constexpr float kTwoPi  = juce::MathConstants<float>::twoPi;

    // Builds the "12 ms" / "40 %" style text shown under each knob.
    juce::AudioParameterFloatAttributes attrs (const juce::String& suffix, int decimals = 0)
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([suffix, decimals] (float v, int) { return juce::String (v, decimals) + suffix; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
    }
}

//==============================================================================
// Everything the audio loop needs, read ONCE per block from the parameters.
//==============================================================================
struct FallOfTheWorldProcessor::BlockParams
{
    bool   freeze, reverse;
    double windowSamples;     // size of the usable audio memory, in samples
    float  grainMs, density, spray, pitchChaos, postPitch, swarm;
};

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FallOfTheWorldProcessor::createLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout] (const char* id, const char* name, Range range, float def, AudioParameterFloatAttributes a)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, a));
    };

    // ---- 1. INPUT & BUFFER ----
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::freeze,  1 }, "Freeze",  false));
    addFloat (ParamIDs::bufferSize, "Buffer Size", Range (50.0f, 4000.0f, 1.0f, 0.5f), 1500.0f, attrs (" ms"));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::reverse, 1 }, "Reverse", false));

    // ---- 2. GRANULAR ENGINE ----
    addFloat (ParamIDs::grainSize,  "Grain Size",   Range (5.0f, 500.0f, 0.1f, 0.5f),  80.0f, attrs (" ms", 0));
    addFloat (ParamIDs::density,    "Density",      Range (1.0f, 100.0f, 0.1f, 0.5f),  20.0f, attrs (" /s", 0));
    addFloat (ParamIDs::spray,      "Spray",        Range (0.0f, 100.0f, 0.1f),        25.0f, attrs (" %"));
    addFloat (ParamIDs::pitchChaos, "Pitch Chaos",  Range (0.0f, 24.0f, 0.01f),         0.0f, attrs (" st", 1));

    // ---- 3. FEEDBACK & MODULATION ----
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::postPitch, 1 }, "Post-Pitch", -24, 24, 0,
                    AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String (v) + " st"; })));
    addFloat (ParamIDs::feedback,   "Feedback",     Range (0.0f, 120.0f, 0.1f),         0.0f, attrs (" %"));
    addFloat (ParamIDs::swarm,      "Swarm",        Range (0.0f, 100.0f, 0.1f),        20.0f, attrs (" %"));

    // ---- 4. CHARACTER ----
    addFloat (ParamIDs::degrade,    "Degrade",      Range (0.0f, 100.0f, 0.1f),         0.0f, attrs (" %"));
    addFloat (ParamIDs::ruin,       "Ruin",         Range (0.0f, 100.0f, 0.1f),        20.0f, attrs (" %"));
    addFloat (ParamIDs::dampen,     "Dampen",       Range (0.0f, 100.0f, 0.1f),        30.0f, attrs (" %"));

    // ---- 5. MASTER ----
    addFloat (ParamIDs::space,      "Space",        Range (0.0f, 100.0f, 0.1f),        30.0f, attrs (" %"));
    addFloat (ParamIDs::dryWet,     "Dry/Wet",      Range (0.0f, 100.0f, 0.1f),        50.0f, attrs (" %"));

    // Output: -100 dB is treated as "-inf" (true silence). The skew puts the
    // middle of the knob near -18 dB so the useful range isn't squashed at the top.
    Range outRange (-100.0f, 12.0f, 0.1f);
    outRange.setSkewForCentre (-18.0f);
    addFloat (ParamIDs::output, "Output", outRange, 0.0f,
              AudioParameterFloatAttributes()
                  .withStringFromValueFunction ([] (float v, int) { return v <= -99.9f ? String ("-inf") : String (v, 1) + " dB"; })
                  .withValueFromStringFunction ([] (const String& t) { return t.containsIgnoreCase ("inf") ? -100.0f : t.getFloatValue(); }));

    return layout;
}

//==============================================================================
FallOfTheWorldProcessor::FallOfTheWorldProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pFreeze     = apvts.getRawParameterValue (ParamIDs::freeze);
    pBufferSize = apvts.getRawParameterValue (ParamIDs::bufferSize);
    pReverse    = apvts.getRawParameterValue (ParamIDs::reverse);
    pGrainSize  = apvts.getRawParameterValue (ParamIDs::grainSize);
    pDensity    = apvts.getRawParameterValue (ParamIDs::density);
    pSpray      = apvts.getRawParameterValue (ParamIDs::spray);
    pPitchChaos = apvts.getRawParameterValue (ParamIDs::pitchChaos);
    pPostPitch  = apvts.getRawParameterValue (ParamIDs::postPitch);
    pFeedback   = apvts.getRawParameterValue (ParamIDs::feedback);
    pSwarm      = apvts.getRawParameterValue (ParamIDs::swarm);
    pDegrade    = apvts.getRawParameterValue (ParamIDs::degrade);
    pRuin       = apvts.getRawParameterValue (ParamIDs::ruin);
    pDampen     = apvts.getRawParameterValue (ParamIDs::dampen);
    pSpace      = apvts.getRawParameterValue (ParamIDs::space);
    pDryWet     = apvts.getRawParameterValue (ParamIDs::dryWet);
    pOutput     = apvts.getRawParameterValue (ParamIDs::output);

    // Hann window lookup table: 0 -> 1 -> 0 bell curve that fades each grain in/out (no clicks).
    for (size_t i = 0; i < hannTable.size(); ++i)
        hannTable[i] = 0.5f * (1.0f - std::cos (kTwoPi * (float) i / 2048.0f));
}

//==============================================================================
bool FallOfTheWorldProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void FallOfTheWorldProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;

    // 12 seconds of memory: the window is at most 4 s, the rest is headroom so
    // pitched-up / reversed grains don't run into freshly-written audio.
    ringSize = (int) (sampleRate * 12.0);
    ring.setSize (2, ringSize, false, true, false);
    ring.clear();
    writePos = 0;

    wetBuffer.setSize (2, juce::jmax (samplesPerBlock, 4096), false, true, false);

    for (auto& g : grains) g.active = false;
    spawnCountdown = 0.0f;

    for (int c = 0; c < 2; ++c)
    {
        fbState[c] = 0.0f;
        fbDc[c] = {}; ruinDc[c] = {};
        ruinLp[c] = 0.0f; crushHeld[c] = 0.0f; crushCounter[c] = 0.0f;
        ladder[c] = {};
    }

    reverb.setSampleRate (sampleRate);
    reverb.reset();

    mixSmoothed.reset (sampleRate, 0.02);
    mixSmoothed.setCurrentAndTargetValue (pDryWet->load() / 100.0f);
    outSmoothed.reset (sampleRate, 0.02);
    outSmoothed.setCurrentAndTargetValue (1.0f);
    limiterGain = 1.0f;
}

float FallOfTheWorldProcessor::hannAt (float phase) const
{
    const float idx  = phase * 2048.0f;
    const int   i    = juce::jlimit (0, 2047, (int) idx);
    const float frac = idx - (float) i;
    return hannTable[(size_t) i] + frac * (hannTable[(size_t) i + 1] - hannTable[(size_t) i]);
}

//==============================================================================
// Birth of a single grain: decide where in the memory it starts, how long it
// lives, how fast it plays (pitch), which way, and where it sits in the stereo field.
//==============================================================================
void FallOfTheWorldProcessor::spawnGrain (const BlockParams& p, float n1, float n2)
{
    // Find a free slot; if all are busy, this grain is simply skipped.
    Grain* slot = nullptr;
    for (auto& g : grains)
        if (! g.active) { slot = &g; break; }
    if (slot == nullptr) return;

    // SWARM wobbles the grain size by up to +-1.5 octaves, and the spray by +-50 %.
    const float sizeMod = std::pow (2.0f, p.swarm * n1 * 1.5f);
    const float grainMs = juce::jlimit (5.0f, 500.0f, p.grainMs * sizeMod);
    const int   length  = juce::jmax (64, (int) (grainMs * 0.001 * sampleRate));
    const float sprayAmt = juce::jlimit (0.0f, 1.0f, p.spray + p.swarm * n2 * 0.5f);

    // PITCH: Post-Pitch (global shift) + a random detune of +-Pitch Chaos semitones for this grain only.
    // Pitch is changed by playing the grain faster/slower (like a tape / sampler).
    const float  semis = p.postPitch + (rng.nextFloat() * 2.0f - 1.0f) * p.pitchChaos;
    const double ratio = juce::jlimit (1.0 / 16.0, 16.0, std::pow (2.0, (double) semis / 12.0));

    // POSITION: "age" = how many samples BEHIND the write head the grain starts.
    // lo..hi is the range of ages that keeps the grain from running into the write head.
    const bool   live = ! p.freeze;
    const double span = (double) length * ratio + (live ? (double) length : 0.0); // distance a reversed grain travels
    double lo, hi;
    if (! p.reverse)
    {
        // Forward: starts at "age", reads towards newer audio. Must not reach the write head.
        lo = 64.0 + (live ? juce::jmax (0.0, (double) length * (ratio - 1.0)) : (double) length * ratio);
        hi = juce::jmax (lo, p.windowSamples);
    }
    else
    {
        // Reverse: starts at "age", reads towards OLDER audio. Must stay inside the window.
        lo = 64.0;
        hi = juce::jmax (lo, p.windowSamples - span);
    }
    // Spray 0 -> newest audio (or the frozen moment). Spray 100 -> anywhere in the window.
    double age = lo + (double) sprayAmt * (hi - lo);
    age = juce::jlimit (64.0, (double) ringSize - 64.0, age);

    double pos = (double) writePos - age;
    if (pos < 0.0) pos += (double) ringSize;

    // STEREO: random pan, wider when spray is higher. Equal-power law.
    const float pan  = 0.5f + (rng.nextFloat() - 0.5f) * (0.25f + 0.75f * sprayAmt);

    // LEVEL: when many grains overlap their sum gets loud, so scale down by the expected overlap.
    // (average Hann window value is 0.5, hence the *0.5)
    const float overlap = p.density * ((float) length / (float) sampleRate) * 0.5f;
    const float norm    = 1.0f / std::pow (juce::jmax (1.0f, overlap), 0.75f);

    slot->active    = true;
    slot->pos       = pos;
    slot->step      = p.reverse ? -ratio : ratio;
    slot->length    = length;
    slot->counter   = 0;
    slot->gainL     = std::cos (pan * kHalfPi) * 1.4142f * norm;
    slot->gainR     = std::sin (pan * kHalfPi) * 1.4142f * norm;
    slot->pitchNorm = juce::jlimit (0.0f, 1.0f, (semis + 48.0f) / 96.0f);
}

//==============================================================================
void FallOfTheWorldProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || ringSize == 0) return;   // stereo only
    if (wetBuffer.getNumSamples() < n) wetBuffer.setSize (2, n, false, false, true); // rare: host sent a huge block

    // ------------------------------------------------------------------ read parameters once per block
    BlockParams bp;
    bp.freeze        = pFreeze->load()  > 0.5f;
    bp.reverse       = pReverse->load() > 0.5f;
    bp.windowSamples = juce::jlimit (1024.0, (double) ringSize - 1024.0, (double) pBufferSize->load() * 0.001 * sampleRate);
    bp.grainMs       = pGrainSize->load();
    bp.density       = pDensity->load();
    bp.spray         = pSpray->load() / 100.0f;
    bp.pitchChaos    = pPitchChaos->load();
    bp.postPitch     = pPostPitch->load();
    bp.swarm         = pSwarm->load() / 100.0f;

    const float fbGain = pFeedback->load() / 100.0f;         // 0 .. 1.2

    // DEGRADE: bit depth 16 -> ~2.5 bits, and sample-rate hold of 1x -> ~48x
    const float degrade   = pDegrade->load() / 100.0f;
    const bool  crushOn   = degrade > 0.001f;
    const float levels    = std::pow (2.0f, (16.0f - 13.5f * degrade) - 1.0f);
    const float holdLen   = 1.0f + degrade * degrade * 47.0f;

    // RUIN: wavefolder amount
    const float ruin      = pRuin->load() / 100.0f;
    const bool  ruinOn    = ruin > 0.001f;
    const float ruinDrive = 1.0f + 11.0f * ruin;
    const float ruinBias  = 0.12f * ruin;                    // asymmetry -> even harmonics
    const float ruinMix   = juce::jmin (1.0f, ruin * 5.0f);
    const float lowCoef   = 1.0f - std::exp (-kTwoPi * 140.0f / (float) sampleRate);

    // DAMPEN: ladder filter cutoff sweeps 20 kHz (0 %) -> 120 Hz (100 %), exponentially
    const float dampen = pDampen->load() / 100.0f;
    const float cutoff = juce::jlimit (20.0f, 0.45f * (float) sampleRate, 20000.0f * std::pow (120.0f / 20000.0f, dampen));
    const float ladG   = 1.0f - std::exp (-kTwoPi * cutoff / (float) sampleRate);
    const float ladK   = 0.35f;                              // fixed light resonance
    const float ladMakeup = 1.0f + ladK;                     // resonance feedback eats bass; this puts it back

    auto* L = buffer.getWritePointer (0);   // input is read from here, final output is written back here
    auto* R = buffer.getWritePointer (1);
    auto* wetL = wetBuffer.getWritePointer (0);
    auto* wetR = wetBuffer.getWritePointer (1);
    auto* ringL = ring.getWritePointer (0);
    auto* ringR = ring.getWritePointer (1);

    // ================================================================== PASS 1: granular -> degrade -> ruin -> dampen
    for (int i = 0; i < n; ++i)
    {
        const float n1 = swarmA.next (sampleRate, rng);
        const float n2 = swarmB.next (sampleRate, rng);

        // ---- Grain scheduler: births happen at random intervals averaging 1/density seconds.
        spawnCountdown -= 1.0f;
        if (spawnCountdown <= 0.0f)
        {
            spawnGrain (bp, n1, n2);
            spawnCountdown += (float) (sampleRate / (double) bp.density) * (0.5f + rng.nextFloat());
        }

        // ---- Play every living grain and sum them.
        float gl = 0.0f, gr = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;

            const float w = hannAt ((float) g.counter / (float) g.length);

            // Linear interpolation between two neighbouring samples (needed because pitch makes positions fractional).
            int i0 = (int) g.pos;
            if (i0 >= ringSize) i0 = 0;
            int i1 = i0 + 1;
            if (i1 >= ringSize) i1 = 0;
            const float frac = (float) (g.pos - (double) i0);

            const float sL = ringL[i0] + frac * (ringL[i1] - ringL[i0]);
            const float sR = ringR[i0] + frac * (ringR[i1] - ringR[i0]);
            gl += sL * w * g.gainL;
            gr += sR * w * g.gainR;

            g.pos += g.step;
            if (g.pos >= (double) ringSize) g.pos -= (double) ringSize;
            else if (g.pos < 0.0)           g.pos += (double) ringSize;

            if (++g.counter >= g.length) g.active = false;
        }

        // ---- Write into the ring buffer: new input + FEEDBACK of the processed output.
        // tanh() is the saturator: below 100% the loop decays, above 100% it
        // self-oscillates but can never explode, it just runs into the soft wall.
        // When FROZEN nothing is written at all, so the memory is looped forever.
        if (! bp.freeze)
        {
            const float fbL = std::tanh (fbGain * kFeedbackMakeup * fbState[0]);
            const float fbR = std::tanh (fbGain * kFeedbackMakeup * fbState[1]);
            ringL[writePos] = L[i] + fbL;
            ringR[writePos] = R[i] + fbR;
            if (++writePos >= ringSize) writePos = 0;
        }

        // ---- Character chain, one channel at a time.
        float y[2] = { gl, gr };
        for (int c = 0; c < 2; ++c)
        {
            float x = y[c];

            // DEGRADE: sample-rate reducer (hold) + bit crusher (rounding)
            if (crushOn)
            {
                crushCounter[c] += 1.0f;
                if (crushCounter[c] >= holdLen)
                {
                    crushCounter[c] -= holdLen;
                    crushHeld[c] = std::round (x * levels) / levels;
                }
                x = crushHeld[c];
            }

            // RUIN: sine wavefolder. When the (amplified) signal goes past the top
            // of the sine it folds back down, which adds lots of harmonics.
            if (ruinOn)
            {
                const float folded = std::sin (kHalfPi * ruinDrive * (x + ruinBias));
                ruinLp[c] += lowCoef * (folded - ruinLp[c]);             // low-passed copy ...
                float r = folded + 0.6f * ruin * ruinLp[c];             // ... added back = more low end
                r = ruinDc[c].process (r);                              // remove the DC from the bias
                x = x + ruinMix * (r - x);
            }

            // DAMPEN: 24 dB/oct ladder low-pass, AFTER the distortion
            x = ladder[c].process (x, ladG, ladK) * ladMakeup;

            if (! std::isfinite (x)) { x = 0.0f; ladder[c] = {}; }
            y[c] = x;
        }

        // Output of this stage goes to the reverb pass AND becomes the feedback signal for next sample.
        wetL[i] = y[0];
        wetR[i] = y[1];
        fbState[0] = fbDc[0].process (y[0]);
        fbState[1] = fbDc[1].process (y[1]);
    }

    // ================================================================== PASS 2: SPACE (reverb on the wet signal only)
    const float space = pSpace->load() / 100.0f;
    if (space > 0.001f)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize   = 0.6f + 0.395f * space;          // up to ~0.995 = very long tail
        rp.damping    = 0.45f;
        rp.wetLevel   = space * 0.35f;                  // JUCE multiplies wet by 3 internally
        rp.dryLevel   = 0.5f * (1.0f - 0.7f * space);   // JUCE multiplies dry by 2 internally
        rp.width      = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.processStereo (wetL, wetR, n);
    }

    // ================================================================== PASS 3: dry/wet, output volume, brickwall limiter
    mixSmoothed.setTargetValue (pDryWet->load() / 100.0f);
    const float outDb = pOutput->load();
    outSmoothed.setTargetValue (outDb <= -99.9f ? 0.0f : juce::Decibels::decibelsToGain (outDb));
    const float limRelease = 1.0f - std::exp (-1.0f / (0.08f * (float) sampleRate));   // 80 ms

    for (int i = 0; i < n; ++i)
    {
        const float mix  = mixSmoothed.getNextValue();
        const float gDry = std::cos (mix * kHalfPi);     // equal-power crossfade
        const float gWet = std::sin (mix * kHalfPi);
        const float og   = outSmoothed.getNextValue();

        float l = (L[i] * gDry + wetL[i] * gWet) * og;
        float r = (R[i] * gDry + wetR[i] * gWet) * og;

        // Brickwall: gain drops INSTANTLY to whatever keeps the peak at or below 0 dBFS, then recovers slowly.
        // (No look-ahead, so zero latency; a final hard clamp guarantees nothing ever exceeds +-1.)
        const float peak = juce::jmax (std::abs (l), std::abs (r));
        const float need = peak > 1.0f ? 1.0f / peak : 1.0f;
        limiterGain += (1.0f - limiterGain) * limRelease;
        limiterGain  = juce::jmin (limiterGain, need);

        L[i] = juce::jlimit (-1.0f, 1.0f, l * limiterGain);
        R[i] = juce::jlimit (-1.0f, 1.0f, r * limiterGain);
    }

    // ================================================================== snapshot of the grains for the GUI
    for (int j = 0; j < maxGrains; ++j)
    {
        auto& g = grains[(size_t) j];
        auto& v = grainView[(size_t) j];
        if (! g.active) { v.x.store (-1.0f, std::memory_order_relaxed); continue; }

        double age = (double) writePos - g.pos;
        if (age < 0.0) age += (double) ringSize;
        v.x.store   (1.0f - (float) juce::jlimit (0.0, 1.0, age / bp.windowSamples), std::memory_order_relaxed);
        v.y.store   (1.0f - g.pitchNorm, std::memory_order_relaxed);
        v.env.store (hannAt ((float) g.counter / (float) g.length), std::memory_order_relaxed);
    }
}

//==============================================================================
// ENTROPY: "smart" randomisation.
//==============================================================================
void FallOfTheWorldProcessor::randomizeEntropy()
{
    // "bias" > 1 makes low values more likely (power curve), so the dice don't
    // land on the nastiest settings every single time.
    struct Target { const char* id; float bias; };
    static const Target targets[] =
    {
        // Granular
        { ParamIDs::grainSize,  1.0f }, { ParamIDs::density,  1.0f },
        { ParamIDs::spray,      1.0f }, { ParamIDs::pitchChaos, 1.4f },
        // Feedback & modulation
        { ParamIDs::postPitch,  1.0f }, { ParamIDs::feedback, 1.6f }, { ParamIDs::swarm, 1.0f },
        // Distortion
        { ParamIDs::degrade,    2.0f }, { ParamIDs::ruin,     1.4f }, { ParamIDs::dampen, 1.5f }
        // NOT here on purpose: Output, Dry/Wet (and Space, Freeze, Reverse, Buffer Size)
    };

    auto setNormalised = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();                         // lets the host record it as one undoable move
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
            p->endChangeGesture();
        }
    };

    for (const auto& t : targets)
        setNormalised (t.id, std::pow (entropyRng.nextFloat(), t.bias));

    // Smart rule 1: never land on a nearly silent patch (few, tiny grains = long gaps of nothing).
    auto* gs = apvts.getParameter (ParamIDs::grainSize);
    auto* dn = apvts.getParameter (ParamIDs::density);
    if (gs != nullptr && dn != nullptr)
    {
        const float grainSec = gs->convertFrom0to1 (gs->getValue()) * 0.001f;
        const float dens     = dn->convertFrom0to1 (dn->getValue());
        if (dens * grainSec < 0.5f)
            setNormalised (ParamIDs::density, dn->convertTo0to1 (juce::jmin (100.0f, 0.5f / grainSec)));
    }

    // Smart rule 2: self-oscillating feedback with a wide-open filter is just painful, so rein the top end in.
    auto* fb = apvts.getParameter (ParamIDs::feedback);
    auto* dm = apvts.getParameter (ParamIDs::dampen);
    if (fb != nullptr && dm != nullptr)
        if (fb->convertFrom0to1 (fb->getValue()) > 100.0f && dm->getValue() < 0.3f)
            setNormalised (ParamIDs::dampen, 0.3f + entropyRng.nextFloat() * 0.3f);
}

//==============================================================================
juce::AudioProcessorEditor* FallOfTheWorldProcessor::createEditor()
{
    return new FallOfTheWorldEditor (*this);
}

void FallOfTheWorldProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FallOfTheWorldProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin (JUCE calls it).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FallOfTheWorldProcessor();
}
