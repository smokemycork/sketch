#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Engine.h"

namespace PID
{
inline constexpr const char* mode = "mode";         // character: Tape / Analog / Digital
inline constexpr const char* rate = "rate";         // grid: 1/4 (4 bars) 1/8 (8) 1/16 (16) 1/32 (32)
inline constexpr const char* color = "color";
inline constexpr const char* drive = "drive";
inline constexpr const char* width = "width";
inline constexpr const char* spread = "spread";
inline constexpr const char* imager = "imager";
inline constexpr const char* duck = "duck";
inline constexpr const char* mix = "mix";
inline constexpr const char* pingpong = "pingpong";
inline constexpr const char* sync = "sync";
inline constexpr const char* noteType = "noteType";
inline constexpr const char* ms = "ms";
inline constexpr const char* linkShape = "linkShape";
inline constexpr const char* stack = "stack";
inline constexpr const char* preOn = "preOn";
inline constexpr const char* postOn = "postOn";
}

inline juce::String bandId (bool post, int band, const char* what)
{
    return juce::String (post ? "post" : "pre") + juce::String (band + 1) + "_" + what;
}

// number of sketch bars (= repeats) in each rate mode
inline int barsForRate (int rate) { static const int b[4] = { 4, 8, 16, 32 }; return b[rate < 0 ? 0 : rate > 3 ? 3 : rate]; }

class SketchProcessor : public juce::AudioProcessor
{
public:
    SketchProcessor();
    ~SketchProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sketch Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- the sketch: every rate mode (1/4, 1/8, 1/16, 1/32) keeps its own drawing
    int   rateIndex() const { return juce::jlimit (0, 3, (int) P.rate->load()); }
    int   numBars() const   { return barsForRate (rateIndex()); }
    void  setLevel (int side, int idx, float v)  { levelShape[rateIndex()][side][(size_t) idx].store (v); }
    float getLevel (int side, int idx) const     { return levelShape[rateIndex()][side][(size_t) idx].load(); }
    void  setPitch (int side, int idx, int v)    { pitchShape[rateIndex()][side][(size_t) idx].store (juce::jlimit (-24, 24, v)); }
    int   getPitch (int side, int idx) const     { return pitchShape[rateIndex()][side][(size_t) idx].load(); }

    // level presets: 0 Natural 1 Linear 2 Curve Down 3 Curve Up 4 Ramp Up 5 Gate 6 Bounce 7 Random
    void  applyLevelPreset (int preset, int sideMask);
    // pitch presets: 0 Reset 1 Octaves 2 Arp 3 Ramp 4 Random scale
    void  applyPitchPreset (int preset, int sideMask);
    void  smoothLevels (int sideMask);
    void  reverseSketch (bool levels, bool pitches, int sideMask);
    void  copyLeftToRight();

    juce::AudioProcessorValueTreeState apvts;
    sd::Engine engine;
    std::atomic<bool> stereoActive { true };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void fillParams (sd::EngineParams& ep);
    void initShapes();
    std::atomic<float>* raw (const juce::String& id) { return apvts.getRawParameterValue (id); }

    std::array<std::atomic<float>, 64> levelShape[4][2];
    std::array<std::atomic<int>, 64> pitchShape[4][2];
    juce::Random rng;

    struct Cache
    {
        std::atomic<float> *mode, *rate, *color, *drive, *width, *spread, *imager, *duck, *mix, *pingpong, *sync, *noteType,
                           *ms, *linkShape, *stack, *preOn, *postOn;
        std::atomic<float>* eq[2][6][6];
    } P {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SketchProcessor)
};
