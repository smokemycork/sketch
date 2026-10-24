#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

namespace UI
{
const juce::Colour bg      { 0xff0c0d11 };
const juce::Colour panel   { 0xff16181f };
const juce::Colour screen  { 0xff0f1116 };
const juce::Colour track   { 0xff2a2d38 };
const juce::Colour text    { 0xffe6e7ee };
const juce::Colour dim     { 0xff868a98 };
inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h).withStyle (bold ? "Bold" : "Regular"));
}
inline juce::Colour accentFor (int mode)
{
    return mode == 0 ? juce::Colour (0xffff7a2f) : mode == 1 ? juce::Colour (0xff2fd6a5) : juce::Colour (0xff4aa8ff);
}
}

struct SketchLookAndFeel : public juce::LookAndFeel_V4
{
    juce::Colour accent = UI::accentFor (0);
    SketchLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
};

struct Knob
{
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    void init (juce::Component& parent, juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& text);
    void setBounds (juce::Rectangle<int> area);
    void setVisible (bool v) { slider.setVisible (v); label.setVisible (v); }
};

// Top screen: LEVEL bars / PITCH bars / PRE EQ / POST EQ
class SketchScreen : public juce::Component, private juce::Timer
{
public:
    enum View { PreEq = 0, Level = 1, Pitch = 2, PostEq = 3 };
    explicit SketchScreen (SketchProcessor&);
    ~SketchScreen() override { stopTimer(); }

    void setView (View v) { view = v; selBand = -1; repaint(); }
    View getView() const { return view; }
    void setAccent (juce::Colour c) { accent = c; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr float kMargin = 10.0f;

private:
    struct Bar { int side; int k; double time; double u; float x; };

    void timerCallback() override;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (kMargin); }
    void computeBars();
    float barWidth() const;
    int nearestBar (float x, int sideFilter) const;
    int barCount (int side) const;
    float levelOf (const Bar& b) const;
    void setLevelAtBar (const Bar& b, float y, bool linked);
    void setLevelLine (int side, bool linked, int k1, float y1, int k2, float y2);
    void paintGrid (juce::Graphics&, const juce::Rectangle<float>& pa);
    void setPitchAtBar (const Bar& b, int semis, bool linked);
    void setPitchLine (int side, bool linked, int k1, int s1, int k2, int s2);
    void applyPoint (float x, float y, bool shift, bool isDown);
    bool isLinked() const { return proc.apvts.getRawParameterValue (PID::linkShape)->load() > 0.5f; }

    void paintLevel (juce::Graphics&);
    void paintPitch (juce::Graphics&);
    void paintEq (juce::Graphics&);

    // EQ helpers
    bool eqPost() const { return view == PostEq; }
    float freqToX (double f) const;
    double xToFreq (float x) const;
    float dbToY (double db) const;
    double yToDb (float y) const;
    juce::Point<float> nodePos (int band) const;
    int hitBand (juce::Point<float> p) const;
    void setEq (int band, const char* what, float plain);
    float getEq (int band, const char* what) const;
    void beginEq (int band, const char* what);
    void endEq (int band, const char* what);
    void showBandMenu (int band);

    SketchProcessor& proc;
    View view = Level;
    juce::Colour accent = UI::accentFor (0);
    std::vector<Bar> bars;
    bool bPP = false;
    float lvl[2][64];

    // level / pitch editing state
    int lockSide = -1;
    bool anchorValid = false;
    float anchorY = 0; int anchorSide = 0, anchorK = 1; int anchorSemis = 0;
    float lineY = 0; int lineSide = 0, lineK = 1; int lineSemis = 0; bool lineValid = false;
    float lastX = 0, lastY = 0;
    int hoverBar = -1;
    juce::Point<float> hoverPos;

    // EQ state
    int selBand = -1;
    float eqStartX = 0, eqStartY = 0; double eqF0 = 1000; float eqG0 = 0;
    juce::dsp::FFT fft { 11 };
    std::array<float, 4096> fftData {};
    std::array<float, 1024> spec {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SketchScreen)
};

class SketchScope : public juce::Component
{
public:
    explicit SketchScope (SketchProcessor& p) : proc (p) {}
    void setAccent (juce::Colour c) { accent = c; }
    void paint (juce::Graphics&) override;
private:
    SketchProcessor& proc;
    juce::Colour accent = UI::accentFor (0);
};

class SketchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SketchEditor (SketchProcessor&);
    ~SketchEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setChoice (const char* id, int index);
    void refreshView();

    SketchProcessor& proc;
    SketchLookAndFeel laf;
    SketchScreen screen;
    SketchScope scope;

    Knob delayMs, color, drive, width, duck, mix;
    juce::TextButton modeBtn[3], rateBtn[4], typeBtn[3], spreadBtn[3], viewBtn[4], levelBtn[10], pitchBtn[6];
    juce::TextButton linkShape { "LINK L/R" }, syncBtn { "SYNC" }, ppBtn { "PING PONG" }, imagerBtn { "IMAGER" },
                     stackBtn { "STACK" }, preOnBtn { "EQ ON" }, postOnBtn { "EQ ON" };
    juce::Label readout, spreadLabel, shapeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        aLinkShape, aSync, aPP, aImager, aStack, aPreOn, aPostOn;

    static constexpr int controlsTop = 480;
    int lastMode = -1;
    bool lastLinkShape = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SketchEditor)
};
