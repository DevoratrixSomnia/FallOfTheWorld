#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

//==============================================================================
// Parameter IDs. Using one shared list means the processor and the editor can
// never disagree about a name (a typo becomes a compile error, not a silent bug).
//==============================================================================
namespace ParamIDs
{
    inline constexpr const char* freeze      = "freeze";
    inline constexpr const char* bufferSize  = "bufferSize";
    inline constexpr const char* reverse     = "reverse";

    inline constexpr const char* grainSize   = "grainSize";
    inline constexpr const char* density     = "density";
    inline constexpr const char* spray       = "spray";
    inline constexpr const char* pitchChaos  = "pitchChaos";

    inline constexpr const char* postPitch   = "postPitch";
    inline constexpr const char* feedback    = "feedback";
    inline constexpr const char* swarm       = "swarm";

    inline constexpr const char* degrade     = "degrade";
    inline constexpr const char* ruin        = "ruin";
    inline constexpr const char* dampen      = "dampen";

    inline constexpr const char* space       = "space";
    inline constexpr const char* dryWet      = "dryWet";
    inline constexpr const char* output      = "output";
}

//==============================================================================
class FallOfTheWorldProcessor : public juce::AudioProcessor
{
public:
    FallOfTheWorldProcessor();
    ~FallOfTheWorldProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override { return "Fall of the World"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // The parameter tree. Host automation, presets and the GUI all go through it.
    juce::AudioProcessorValueTreeState apvts;

    // ENTROPY button: randomises Granular + Feedback + Distortion parameters.
    // Call from the message thread (the button click does).
    // Never touches Output, Dry/Wet, Space, Freeze, Reverse or Buffer Size.
    void randomizeEntropy();

    // Tiny read-only snapshot of every grain, written by the audio thread and
    // read by the GUI timer (atomics make that safe without locks).
    static constexpr int maxGrains = 128;
    struct GrainView
    {
        std::atomic<float> x { -1.0f };   // 0 = oldest audio in window, 1 = newest. <0 means "slot unused"
        std::atomic<float> y { 0.0f };    // 0 = low pitch, 1 = high pitch
        std::atomic<float> env { 0.0f };  // current grain envelope value 0..1
    };
    std::array<GrainView, maxGrains> grainView;

private:
    //==========================================================================
    struct BlockParams;   // snapshot of the parameter values for one block (defined in the .cpp)

    // One grain = a short windowed snippet being played back from the ring buffer.
    struct Grain
    {
        bool   active    = false;
        double pos       = 0.0;   // current read position (fractional index into the ring buffer)
        double step      = 1.0;   // samples advanced per output sample (negative = reversed, magnitude = pitch ratio)
        int    length    = 0;     // total length in output samples
        int    counter   = 0;     // how many samples of the grain have been played
        float  gainL     = 0.7f;  // per-grain pan/level, left
        float  gainR     = 0.7f;  // per-grain pan/level, right
        float  pitchNorm = 0.5f;  // only used by the GUI
    };

    // A smooth random wobble in [-1, 1]. Picks a new random target every
    // 0.15..0.75 s and glides towards it. Drives the "Swarm" modulation.
    struct SmoothNoise
    {
        float current = 0.0f, target = 0.0f;
        int   countdown = 0;
        float next (double sr, juce::Random& rng)
        {
            if (--countdown <= 0)
            {
                target    = rng.nextFloat() * 2.0f - 1.0f;
                countdown = (int) (sr * (0.15 + rng.nextFloat() * 0.6));
            }
            const float coeff = 1.0f - std::exp (-1.0f / (0.12f * (float) sr)); // ~120 ms glide
            current += (target - current) * coeff;
            return current;
        }
    };

    // Removes DC offset (a gentle ~7 Hz high-pass).
    struct DcBlock
    {
        float x1 = 0.0f, y1 = 0.0f;
        float process (float x)
        {
            const float y = x - x1 + 0.995f * y1;
            x1 = x; y1 = y;
            return y;
        }
    };

    // 4-pole (24 dB/oct) transistor-ladder style low-pass with tanh saturation.
    struct Ladder
    {
        float s[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float process (float in, float g, float k)
        {
            const float u = std::tanh (in - k * s[3]);   // resonance feedback + saturation
            s[0] += g * (u    - s[0]);                   // four cascaded one-pole low-passes
            s[1] += g * (s[0] - s[1]);
            s[2] += g * (s[1] - s[2]);
            s[3] += g * (s[2] - s[3]);
            return s[3];
        }
    };

    void spawnGrain (const BlockParams& p, float swarmNoise1, float swarmNoise2);
    float hannAt (float phase) const;   // phase 0..1 -> window value (table lookup)

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==========================================================================
    // Raw (atomic) parameter pointers: the cheap, lock-free way to read params on the audio thread.
    std::atomic<float> *pFreeze, *pBufferSize, *pReverse,
                       *pGrainSize, *pDensity, *pSpray, *pPitchChaos,
                       *pPostPitch, *pFeedback, *pSwarm,
                       *pDegrade, *pRuin, *pDampen,
                       *pSpace, *pDryWet, *pOutput;

    // The ring buffer ("audio memory") that grains read from.
    juce::AudioBuffer<float> ring;
    int ringSize = 0;
    int writePos = 0;                 // index where the NEXT incoming sample is written

    juce::AudioBuffer<float> wetBuffer;   // scratch space for the processed signal of this block
    double sampleRate = 44100.0;

    std::array<Grain, maxGrains> grains;
    float spawnCountdown = 0.0f;      // samples until the next grain is born

    juce::Random rng { 0x5EED };      // audio-thread random generator
    juce::Random entropyRng { (juce::int64) juce::Time::currentTimeMillis() }; // GUI-thread one

    SmoothNoise swarmA, swarmB;       // two independent modulators (grain size / spray)

    float fbState[2] = { 0.0f, 0.0f };      // last processed output sample, fed back into the buffer
    DcBlock fbDc[2], ruinDc[2];
    float ruinLp[2]       = { 0.0f, 0.0f }; // low-end "beef" filter state
    float crushHeld[2]    = { 0.0f, 0.0f }; // sample-and-hold value for the sample-rate reducer
    float crushCounter[2] = { 0.0f, 0.0f };
    Ladder ladder[2];

    juce::Reverb reverb;
    juce::SmoothedValue<float> mixSmoothed, outSmoothed;
    float limiterGain = 1.0f;

    std::array<float, 2049> hannTable {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FallOfTheWorldProcessor)
};
