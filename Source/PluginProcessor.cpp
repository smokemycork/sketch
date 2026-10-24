#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const double kRateBeats[4] = { 1.0, 0.5, 0.25, 0.125 };                    // 1/4 1/8 1/16 1/32 in beats
const double kTypeMul[3]   = { 1.0, 1.5, 2.0 / 3.0 };                      // straight, dotted, triplet
const double kEqDefaultFreq[6] = { 80.0, 250.0, 800.0, 2500.0, 6000.0, 12000.0 };
}

juce::AudioProcessorValueTreeState::ParameterLayout SketchProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto pctParse = [] (const String& t) { return t.getFloatValue() / 100.0f; };
    auto addPct = [&] (const char* id, const char* name, float mn, float mx, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (mn, mx, 0.001f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (pctParse)));
    };
    auto addBool = [&] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto addChoice = [&] (const String& id, const String& name, StringArray items, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

    addChoice (PID::mode, "Character", { "Tape", "Analog", "Digital" }, 0);
    addChoice (PID::rate, "Rate Mode", { "1/4 - 4 bars", "1/8 - 8 bars", "1/16 - 16 bars", "1/32 - 32 bars" }, 1);
    addPct (PID::color, "Color", 0.0f, 1.0f, 0.0f);
    addPct (PID::drive, "Drive", 0.0f, 1.0f, 0.0f);
    addPct (PID::width, "Width", 0.0f, 2.0f, 1.0f);
    addChoice (PID::spread, "Spread Type", { "0", "1", "2" }, 0);
    addBool (PID::imager, "Imager", false);
    addPct (PID::duck, "Duck", 0.0f, 1.0f, 0.0f);
    addPct (PID::mix, "Mix", 0.0f, 1.0f, 0.35f);
    addBool (PID::pingpong, "Ping Pong", false);
    addBool (PID::sync, "Sync", true);
    addChoice (PID::noteType, "Note Type", { "Straight", "Dotted", "Triplet" }, 0);
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { PID::ms, 1 }, "Time (free)", NormalisableRange<float> (1.0f, 2000.0f, 1.0f, 0.4f), 250.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    addBool (PID::linkShape, "Link L/R", true);
    addBool (PID::stack, "Pitch Stack", false);
    addBool (PID::preOn, "Pre EQ On", true);
    addBool (PID::postOn, "Post EQ On", true);

    const StringArray types { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf" };
    const StringArray slopes { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    for (int e = 0; e < 2; ++e)
        for (int b = 0; b < 6; ++b)
        {
            const String nm = String (e ? "Post EQ " : "Pre EQ ") + String (b + 1) + " ";
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { bandId (e == 1, b, "on"), 1 }, nm + "On", false));
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { bandId (e == 1, b, "type"), 1 }, nm + "Type", types, 0));
            p.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { bandId (e == 1, b, "freq"), 1 }, nm + "Freq", NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.3f), (float) kEqDefaultFreq[b],
                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));
            p.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { bandId (e == 1, b, "gain"), 1 }, nm + "Gain", NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
            p.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { bandId (e == 1, b, "q"), 1 }, nm + "Q", NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f), 0.71f));
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { bandId (e == 1, b, "slope"), 1 }, nm + "Slope", slopes, 1));
        }

    return { p.begin(), p.end() };
}

//==============================================================================
SketchProcessor::SketchProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    P.mode = raw (PID::mode);       P.rate = raw (PID::rate);         P.color = raw (PID::color);
    P.drive = raw (PID::drive);     P.width = raw (PID::width);       P.spread = raw (PID::spread);
    P.imager = raw (PID::imager);   P.duck = raw (PID::duck);         P.mix = raw (PID::mix);
    P.pingpong = raw (PID::pingpong); P.sync = raw (PID::sync);       P.noteType = raw (PID::noteType);
    P.ms = raw (PID::ms);           P.linkShape = raw (PID::linkShape); P.stack = raw (PID::stack);
    P.preOn = raw (PID::preOn);     P.postOn = raw (PID::postOn);
    static const char* what[6] = { "on", "type", "freq", "gain", "q", "slope" };
    for (int e = 0; e < 2; ++e)
        for (int b = 0; b < 6; ++b)
            for (int w = 0; w < 6; ++w)
                P.eq[e][b][w] = raw (bandId (e == 1, b, what[w]));

    initShapes();
}

bool SketchProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono()) && l.getMainInputChannelSet() == o;
}

void SketchProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

void SketchProcessor::initShapes()
{
    for (int m = 0; m < 4; ++m)
        for (int s = 0; s < 2; ++s)
        {
            const int n = barsForRate (m);
            for (int j = 0; j < 64; ++j)
            {
                const double u = n > 1 ? (double) j / (n - 1) : 0.0;
                levelShape[m][s][(size_t) j].store (j < n ? (float) (0.88 * (1.0 - u)) : 0.0f);
                pitchShape[m][s][(size_t) j].store (0);
            }
        }
}

void SketchProcessor::applyLevelPreset (int preset, int sideMask)
{
    const int m = rateIndex(), n = barsForRate (m);
    for (int s = 0; s < 2; ++s)
    {
        if (! (sideMask & (1 << s))) continue;
        for (int j = 0; j < 64; ++j)
        {
            const double u = n > 1 ? (double) j / (n - 1) : 0.0;
            double y = 0.88 * (1.0 - u);                                  // Natural: even steps down to silence
            switch (preset)
            {
                case 1: y = 1.0 - u; break;                               // Linear
                case 2: y = std::pow (1.0 - u, 2.4); break;               // Curve Down: drops fast, quiet tail
                case 3: y = 1.0 - std::pow (u, 3.0); break;               // Curve Up: stays loud, then drops
                case 4: y = 0.12 + 0.88 * u; break;                       // Ramp Up (build-up)
                case 5: y = (j % 2 == 0) ? 0.9 : 0.08; break;             // Gate: on / off every bar
                case 6: y = 0.9 * (0.55 + 0.45 * std::cos (u * 6.2831853 * 2.0)) * (1.0 - 0.6 * u); break; // Bounce
                case 7: y = 0.25 + 0.75 * rng.nextDouble(); break;        // Random
                default: break;
            }
            levelShape[m][s][(size_t) j].store (j < n ? (float) juce::jlimit (0.0, 1.0, y) : 0.0f);
        }
    }
}

void SketchProcessor::applyPitchPreset (int preset, int sideMask)
{
    static const int arp[4] = { 0, 3, 7, 12 };
    static const int scale[7] = { 0, 3, 5, 7, 10, 12, -5 };               // minor pentatonic-ish
    const int m = rateIndex(), n = barsForRate (m);
    for (int s = 0; s < 2; ++s)
    {
        if (! (sideMask & (1 << s))) continue;
        for (int j = 0; j < 64; ++j)
        {
            int v = 0;
            if (j < n)
            {
                switch (preset)
                {
                    case 1: v = (j % 2) ? 12 : 0; break;                  // Octaves
                    case 2: v = arp[j % 4]; break;                        // Arp
                    case 3: v = (int) std::lround (24.0 * j / std::max (1, n - 1)) - 12; break; // Ramp -12..+12
                    case 4: v = scale[rng.nextInt (7)]; break;            // Random scale
                    default: v = 0; break;
                }
            }
            pitchShape[m][s][(size_t) j].store (juce::jlimit (-24, 24, v));
        }
    }
}

void SketchProcessor::smoothLevels (int sideMask)
{
    const int m = rateIndex(), n = barsForRate (m);
    for (int s = 0; s < 2; ++s)
    {
        if (! (sideMask & (1 << s))) continue;
        float t[64];
        for (int j = 0; j < n; ++j)
        {
            const float a = levelShape[m][s][(size_t) std::max (0, j - 1)].load();
            const float b = levelShape[m][s][(size_t) j].load();
            const float c2 = levelShape[m][s][(size_t) std::min (n - 1, j + 1)].load();
            t[j] = 0.25f * a + 0.5f * b + 0.25f * c2;
        }
        for (int j = 0; j < n; ++j) levelShape[m][s][(size_t) j].store (t[j]);
    }
}

void SketchProcessor::reverseSketch (bool levels, bool pitches, int sideMask)
{
    const int m = rateIndex(), n = barsForRate (m);
    for (int s = 0; s < 2; ++s)
    {
        if (! (sideMask & (1 << s))) continue;
        for (int j = 0; j < n / 2; ++j)
        {
            if (levels)
            {
                const float a = levelShape[m][s][(size_t) j].load(), b = levelShape[m][s][(size_t) (n - 1 - j)].load();
                levelShape[m][s][(size_t) j].store (b); levelShape[m][s][(size_t) (n - 1 - j)].store (a);
            }
            if (pitches)
            {
                const int a = pitchShape[m][s][(size_t) j].load(), b = pitchShape[m][s][(size_t) (n - 1 - j)].load();
                pitchShape[m][s][(size_t) j].store (b); pitchShape[m][s][(size_t) (n - 1 - j)].store (a);
            }
        }
    }
}

void SketchProcessor::copyLeftToRight()
{
    for (int m = 0; m < 4; ++m)
        for (int j = 0; j < 64; ++j)
        {
            levelShape[m][1][(size_t) j].store (levelShape[m][0][(size_t) j].load());
            pitchShape[m][1][(size_t) j].store (pitchShape[m][0][(size_t) j].load());
        }
}

//==============================================================================
void SketchProcessor::fillParams (sd::EngineParams& ep)
{
    double bpm = 120.0;
    bool known = false, playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            known = true;
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
        }
    if (! (bpm >= 20.0 && bpm <= 400.0)) bpm = 120.0;
    ep.hostKnown = known; ep.hostPlaying = playing;

    ep.mode = juce::jlimit (0, 2, (int) P.mode->load());
    ep.color = P.color->load(); ep.drive = P.drive->load(); ep.width = P.width->load();
    ep.spread = juce::jlimit (0, 2, (int) P.spread->load());
    ep.imager = P.imager->load() > 0.5f; ep.duck = P.duck->load(); ep.mix = P.mix->load();
    ep.pingpong = P.pingpong->load() > 0.5f; ep.stack = P.stack->load() > 0.5f;
    ep.preOn = P.preOn->load() > 0.5f; ep.postOn = P.postOn->load() > 0.5f;

    const int rate = rateIndex();
    const int ntype = juce::jlimit (0, 2, (int) P.noteType->load());
    double sec = (double) P.ms->load() / 1000.0;
    if (P.sync->load() > 0.5f) sec = kRateBeats[rate] * kTypeMul[ntype] * 60.0 / bpm;
    ep.delaySec[0] = ep.delaySec[1] = sec;
    ep.reps[0] = ep.reps[1] = barsForRate (rate);

    for (int e = 0; e < 2; ++e)
        for (int b = 0; b < 6; ++b)
        {
            sd::EqBandParams& q = (e == 0 ? ep.pre : ep.post)[b];
            q.on = P.eq[e][b][0]->load() > 0.5f;
            q.type = juce::jlimit (0, 7, (int) P.eq[e][b][1]->load());
            q.freq = P.eq[e][b][2]->load();
            q.gain = P.eq[e][b][3]->load();
            q.q = P.eq[e][b][4]->load();
            q.slope = juce::jlimit (0, 3, (int) P.eq[e][b][5]->load());
        }
}

void SketchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n == 0 || nch == 0) return;

    sd::EngineParams ep;
    fillParams (ep);

    if (! engine.isPrepared()) return;

    const int rate = rateIndex();
    float lv[2][64];
    int pt[2][64];
    const int src1 = (P.linkShape->load() > 0.5f) ? 0 : 1;
    for (int j = 0; j < 64; ++j)
    {
        lv[0][j] = levelShape[rate][0][(size_t) j].load(); lv[1][j] = levelShape[rate][src1][(size_t) j].load();
        pt[0][j] = pitchShape[rate][0][(size_t) j].load(); pt[1][j] = pitchShape[rate][src1][(size_t) j].load();
    }
    sd::Shapes sh;
    sh.level[0] = lv[0]; sh.level[1] = lv[1];
    sh.pitch[0] = pt[0]; sh.pitch[1] = pt[1];
    sh.len = barsForRate (rate);

    stereoActive.store (nch > 1);
    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.process (L, R, n, ep, sh);
}

juce::AudioProcessorEditor* SketchProcessor::createEditor() { return new SketchEditor (*this); }

//==============================================================================
void SketchProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int m = 0; m < 4; ++m)
        for (int s = 0; s < 2; ++s)
        {
            juce::String lv, pt;
            for (int j = 0; j < 64; ++j)
            {
                lv << juce::String (levelShape[m][s][(size_t) j].load(), 4) << " ";
                pt << pitchShape[m][s][(size_t) j].load() << " ";
            }
            state.setProperty ("lv" + juce::String (m) + juce::String (s), lv.trim(), nullptr);
            state.setProperty ("pt" + juce::String (m) + juce::String (s), pt.trim(), nullptr);
        }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void SketchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    for (int m = 0; m < 4; ++m)
        for (int s = 0; s < 2; ++s)
        {
            auto lv = juce::StringArray::fromTokens (state.getProperty ("lv" + juce::String (m) + juce::String (s)).toString(), " ", "");
            auto pt = juce::StringArray::fromTokens (state.getProperty ("pt" + juce::String (m) + juce::String (s)).toString(), " ", "");
            if (lv.isEmpty()) continue;
            for (int j = 0; j < 64; ++j)
            {
                if (j < lv.size()) levelShape[m][s][(size_t) j].store (juce::jlimit (0.0f, 1.0f, lv[j].getFloatValue()));
                if (j < pt.size()) pitchShape[m][s][(size_t) j].store (juce::jlimit (-24, 24, pt[j].getIntValue()));
            }
        }
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SketchProcessor(); }
