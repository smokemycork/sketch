#include "PluginEditor.h"

//==============================================================================
SketchLookAndFeel::SketchLookAndFeel()
{
    using namespace UI;
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, dim);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a3f50));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void SketchLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    using namespace UI;
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    juce::Colour fill = on ? accent : (down ? track.brighter (0.25f) : over ? track.brighter (0.1f) : panel);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? accent.brighter (0.25f) : track);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void SketchLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    using namespace UI;
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float arcW = 4.0f, arcR = radius - arcW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path tr;
    tr.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (track);
    g.strokePath (tr, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (pos > 0.001f)
    {
        juce::Path v;
        v.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (v, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float bodyR = arcR - arcW - 3.0f;
    g.setColour (juce::Colour (0xff1e2129));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour (juce::Colour (0xff343846));
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2, 1.2f);
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.7f, -bodyR + 4.0f, 3.4f, bodyR * 0.42f, 1.5f);
    g.setColour (accent);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

//==============================================================================
void Knob::init (juce::Component& parent, juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    parent.addAndMakeVisible (slider);
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (UI::font (12.0f, true));
    parent.addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
}

void Knob::setBounds (juce::Rectangle<int> a)
{
    label.setBounds (a.removeFromTop (16));
    slider.setBounds (a);
}

//==============================================================================
SketchScreen::SketchScreen (SketchProcessor& p) : proc (p)
{
    for (auto& s : spec) s = -100.0f;
    for (int s = 0; s < 2; ++s)
        for (int j = 0; j < 64; ++j) lvl[s][j] = 0.f;
    startTimerHz (30);
}

void SketchScreen::timerCallback()
{
    if (view == PreEq || view == PostEq)
    {
        const bool post = eqPost();
        const float* ring = post ? proc.engine.ringPost : proc.engine.ringPre;
        const int wr = (post ? proc.engine.ringPostWp : proc.engine.ringPreWp).load();
        for (int i = 0; i < 2048; ++i)
        {
            const int idx = (wr - 2048 + i) & (sd::Engine::kRing - 1);
            const float w = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / 2047.0f);
            fftData[(size_t) i] = ring[idx] * w;
        }
        for (size_t i = 2048; i < fftData.size(); ++i) fftData[i] = 0.f;
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        for (size_t b = 0; b < spec.size(); ++b)
        {
            const float db = 20.0f * std::log10 (fftData[b] / 512.0f + 1.0e-7f);
            spec[b] = spec[b] * 0.65f + db * 0.35f;
        }
    }
    repaint();
}

//------------------------------------------------------------------ bars
void SketchScreen::computeBars()
{
    bars.clear();
    auto& e = proc.engine;
    bPP = e.pubPP.load() != 0;
    const int N0 = e.pubN0.load(), N1 = e.pubN1.load();
    const double T0 = e.pubT0.load(), T1 = e.pubT1.load();
    const double span = std::max (0.01, (double) e.pubSpan.load());
    const auto pa = plot();
    auto add = [&] (int side, int k, double time, double u)
    {
        bars.push_back ({ side, k, time, u, (float) (pa.getX() + time / span * pa.getWidth()) });
    };
    if (bPP)
    {
        for (int k = 1; k <= N0; ++k)
            add ((k & 1) ? 0 : 1, k, ((k + 1) / 2) * T0 + (k / 2) * T1, N0 > 1 ? (k - 1.0) / (N0 - 1.0) : 0.0);
    }
    else
    {
        const int sides = proc.stereoActive.load() ? 2 : 1;
        for (int s = 0; s < sides; ++s)
        {
            const int N = s ? N1 : N0;
            const double T = s ? T1 : T0;
            for (int k = 1; k <= N; ++k) add (s, k, k * T, N > 1 ? (k - 1.0) / (N - 1.0) : 0.0);
        }
    }
}

int SketchScreen::barCount (int side) const
{
    int n = 0;
    for (auto& b : bars) if (b.side == side) ++n;
    return n;
}

float SketchScreen::barWidth() const
{
    const auto pa = plot();
    const double span = std::max (0.01, (double) proc.engine.pubSpan.load());
    double gap;
    if (bPP) gap = 0.5 * (proc.engine.pubT0.load() + proc.engine.pubT1.load()) / span * pa.getWidth();
    else
    {
        double t = proc.engine.pubT0.load();
        if (proc.stereoActive.load()) t = std::min (t, (double) proc.engine.pubT1.load());
        gap = t / span * pa.getWidth();
    }
    return (float) juce::jlimit (2.0, 18.0, gap * 0.42);
}

int SketchScreen::nearestBar (float x, int sideFilter) const
{
    int best = -1;
    float bd = 1.0e9f;
    for (size_t i = 0; i < bars.size(); ++i)
    {
        if (sideFilter >= 0 && bars[i].side != sideFilter) continue;
        const float d = std::fabs (bars[i].x - x);
        if (d < bd) { bd = d; best = (int) i; }
    }
    return best;
}

float SketchScreen::levelOf (const Bar& b) const { return lvl[b.side][b.k - 1]; }

void SketchScreen::setLevelAtBar (const Bar& b, float y, bool linked)
{
    for (int s = 0; s < 2; ++s)
        if (linked || s == b.side) proc.setLevel (s, b.k - 1, y);
}

void SketchScreen::setLevelLine (int side, bool linked, int k1, float y1, int k2, float y2)
{
    if (k1 > k2) { std::swap (k1, k2); std::swap (y1, y2); }
    for (int k = k1; k <= k2; ++k)
    {
        const double t = (k2 > k1) ? (double) (k - k1) / (k2 - k1) : 0.0;
        const float v = (float) (y1 + (y2 - y1) * t);
        const int sd_ = bPP ? ((k & 1) ? 0 : 1) : side;
        for (int s = 0; s < 2; ++s)
            if (linked || s == sd_) proc.setLevel (s, k - 1, v);
    }
}

void SketchScreen::setPitchAtBar (const Bar& b, int semis, bool linked)
{
    for (int s = 0; s < 2; ++s)
        if (linked || s == b.side) proc.setPitch (s, b.k - 1, semis);
}

void SketchScreen::setPitchLine (int side, bool linked, int k1, int s1, int k2, int s2)
{
    if (k1 > k2) { std::swap (k1, k2); std::swap (s1, s2); }
    for (int k = k1; k <= k2; ++k)
    {
        const double t = (k2 > k1) ? (double) (k - k1) / (k2 - k1) : 0.0;
        const int v = (int) std::lround (s1 + (s2 - s1) * t);
        const int sd_ = bPP ? ((k & 1) ? 0 : 1) : side;
        for (int s = 0; s < 2; ++s)
            if (linked || s == sd_) proc.setPitch (s, k - 1, v);
    }
}

void SketchScreen::applyPoint (float x, float y, bool shift, bool isDown)
{
    computeBars();
    if (bars.empty()) return;
    const auto pa = plot();
    const bool lk = isLinked();
    const int idx = nearestBar (x, lk ? -1 : lockSide);
    if (idx < 0) return;
    const Bar& b = bars[(size_t) idx];

    if (view == Level)
    {
        float yy = juce::jlimit (0.0f, 1.0f, (pa.getBottom() - y) / pa.getHeight());
        if (yy < 0.012f) yy = 0.0f;
        if (shift && lineValid)
        {
            setLevelLine (b.side, lk, lineK, lineY, b.k, yy);
        }
        else
        {
            if (isDown) { lastX = x; lastY = yy; }
            const int steps = std::max (1, (int) (std::fabs (x - lastX) / 2.0f));
            for (int i = 1; i <= steps; ++i)
            {
                const float t = (float) i / (float) steps;
                const int ii = nearestBar (lastX + (x - lastX) * t, lk ? -1 : lockSide);
                if (ii >= 0) setLevelAtBar (bars[(size_t) ii], lastY + (yy - lastY) * t, lk);
            }
            if (isDown) setLevelAtBar (b, yy, lk);
            lastX = x; lastY = yy;
        }
        anchorValid = true; anchorK = b.k; anchorY = yy; anchorSide = b.side;
    }
    else if (view == Pitch)
    {
        const int semis = juce::jlimit (-24, 24, (int) std::lround ((pa.getCentreY() - y) / (pa.getHeight() * 0.5f) * 24.0f));
        if (shift && lineValid)
        {
            setPitchLine (b.side, lk, lineK, lineSemis, b.k, semis);
        }
        else
        {
            if (isDown) lastX = x;
            const int steps = std::max (1, (int) (std::fabs (x - lastX) / 2.0f));
            for (int i = 1; i <= steps; ++i)
            {
                const int ii = nearestBar (lastX + (x - lastX) * ((float) i / (float) steps), lk ? -1 : lockSide);
                if (ii >= 0) setPitchAtBar (bars[(size_t) ii], semis, lk);
            }
            if (isDown) setPitchAtBar (b, semis, lk);
            lastX = x;
        }
        anchorValid = true; anchorK = b.k; anchorSemis = semis; anchorSide = b.side;
    }
}

//------------------------------------------------------------------ painting
void SketchScreen::paint (juce::Graphics& g)
{
    using namespace UI;
    g.setColour (screen);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (track);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    if (view == Level) paintLevel (g);
    else if (view == Pitch) paintPitch (g);
    else paintEq (g);
}

void SketchScreen::paintGrid (juce::Graphics& g, const juce::Rectangle<float>& pa)
{
    using namespace UI;
    const auto& e = proc.engine;
    const int N = std::max (1, e.pubN0.load());
    const double span = std::max (0.01, (double) e.pubSpan.load());
    const double T = bPP ? 0.5 * (e.pubT0.load() + e.pubT1.load()) : (double) e.pubT0.load();
    const double beat = N / 4.0 * T;                       // a bar always holds 4 beats of repeats
    g.setFont (UI::font (10.0f, true));
    for (int m = 0; m <= 4; ++m)
    {
        const float x = (float) (pa.getX() + m * beat / span * pa.getWidth());
        g.setColour (m == 0 || m == 4 ? dim.withAlpha (0.8f) : track.withAlpha (0.9f));
        g.drawVerticalLine ((int) x, pa.getY(), pa.getBottom());
        if (m < 4)
        {
            g.setColour (dim);
            g.drawText (juce::String (m + 1), (int) x + 3, (int) pa.getY() + 1, 14, 11, juce::Justification::left);
        }
    }
    // bar numbers along the bottom
    g.setFont (UI::font (9.0f));
    g.setColour (dim);
    for (auto& b : bars)
    {
        if (b.side != 0 && ! bPP) continue;
        if (N > 16 && (b.k % 4) != 0 && b.k != 1) continue;
        g.drawText (juce::String (b.k), (int) b.x - 8, (int) pa.getBottom() - 11, 16, 11, juce::Justification::centred);
    }
}

void SketchScreen::paintLevel (juce::Graphics& g)
{
    using namespace UI;
    const auto pa = plot();
    computeBars();
    for (int s = 0; s < 2; ++s)
        for (int j = 0; j < 64; ++j) lvl[s][j] = proc.getLevel (s, j);

    g.setFont (UI::font (10.0f));
    for (int db : { 0, -6, -12, -24, -36, -48 })
    {
        const float y = pa.getBottom() - (1.0f + (float) db / 60.0f) * pa.getHeight();
        g.setColour (track.withAlpha (0.55f));
        g.drawHorizontalLine ((int) y, pa.getX(), pa.getRight());
        g.setColour (dim);
        g.drawText (db == 0 ? "0 dB" : juce::String (db), (int) pa.getX() + 2, (int) y - 11, 40, 11, juce::Justification::left);
    }

    paintGrid (g, pa);
    const float bw = barWidth();
    const bool two = ! bPP && proc.stereoActive.load();
    for (auto& b : bars)
    {
        const float h = levelOf (b) * pa.getHeight();
        const float x = two ? (b.side == 0 ? b.x - bw : b.x) : b.x - bw * 0.5f;
        g.setColour (b.side == 0 ? accent : accent.brighter (0.6f));
        g.fillRect (x, pa.getBottom() - h, bw, h);
    }
    if (hoverBar >= 0 && hoverBar < (int) bars.size())
    {
        const auto& hb = bars[(size_t) hoverBar];
        const float y = levelOf (hb);
        juce::String t = "bar " + juce::String (hb.k) + ":  " + (y <= 0.003f ? juce::String ("off") : juce::String ((y - 1.0f) * 60.0f, 1) + " dB");
        const auto r = juce::Rectangle<float> (hoverPos.x + 12, hoverPos.y - 24, 7.0f * t.length() + 14, 20).constrainedWithin (getLocalBounds().toFloat());
        g.setColour (juce::Colour (0xee20232c));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (text);
        g.setFont (UI::font (12.0f));
        g.drawText (t, r.toNearestInt(), juce::Justification::centred);
    }
    for (int s = 0; s < 2; ++s)
    {
        juce::Path line;
        bool first = true;
        for (auto& b : bars)
        {
            if (b.side != s) continue;
            const float cx = two ? (s == 0 ? b.x - bw * 0.5f : b.x + bw * 0.5f) : b.x;
            const float cy = pa.getBottom() - levelOf (b) * pa.getHeight();
            if (first) { line.startNewSubPath (cx, cy); first = false; } else line.lineTo (cx, cy);
        }
        g.setColour ((s == 0 ? accent : accent.brighter (0.6f)).withAlpha (0.85f));
        g.strokePath (line, juce::PathStrokeType (1.3f));
    }
}

void SketchScreen::paintPitch (juce::Graphics& g)
{
    using namespace UI;
    const auto pa = plot();
    computeBars();
    const bool stack = proc.apvts.getRawParameterValue (PID::stack)->load() > 0.5f;
    const float mid = pa.getCentreY(), half = pa.getHeight() * 0.5f;

    g.setFont (UI::font (10.0f));
    for (int st : { 24, 12, 0, -12, -24 })
    {
        const float y = mid - (float) st / 24.0f * half;
        g.setColour (st == 0 ? dim : track.withAlpha (0.55f));
        g.drawHorizontalLine ((int) y, pa.getX(), pa.getRight());
        g.setColour (dim);
        g.drawText (st > 0 ? "+" + juce::String (st) : juce::String (st), (int) pa.getX() + 2, (int) y - 11, 40, 11, juce::Justification::left);
    }

    paintGrid (g, pa);
    int pit[2][64];
    for (int s = 0; s < 2; ++s)
        for (int j = 0; j < 64; ++j) pit[s][j] = proc.getPitch (s, j);

    const float bw = barWidth();
    const bool two = ! bPP && proc.stereoActive.load();
    for (auto& b : bars)
    {
        const float y = mid - (float) pit[b.side][b.k - 1] / 24.0f * half;
        const float x = two ? (b.side == 0 ? b.x - bw : b.x) : b.x - bw * 0.5f;
        g.setColour (b.side == 0 ? accent : accent.brighter (0.6f));
        g.fillRect (x, std::min (y, mid), bw, std::max (2.0f, std::fabs (y - mid)));
    }
    if (stack)
    {
        for (int s = 0; s < 2; ++s)
        {
            juce::Path line;
            bool first = true;
            for (auto& b : bars)
            {
                if (b.side != s) continue;
                const int sd_ = bPP ? s : s;
                const float y = mid - (float) sd::effPitch (pit[sd_], true, b.k) / 24.0f * half;
                const float cx = two ? (s == 0 ? b.x - bw * 0.5f : b.x + bw * 0.5f) : b.x;
                if (first) { line.startNewSubPath (cx, y); first = false; } else line.lineTo (cx, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.strokePath (line, juce::PathStrokeType (1.3f));
        }
    }
    if (hoverBar >= 0 && hoverBar < (int) bars.size())
    {
        const auto& b = bars[(size_t) hoverBar];
        const int s = pit[b.side][b.k - 1];
        juce::String t = "bar " + juce::String (b.k) + ":  " + (s > 0 ? "+" : "") + juce::String (s) + " st";
        if (stack) { const int e = sd::effPitch (pit[b.side], true, b.k); t << "  (plays " << (e > 0 ? "+" : "") << e << ")"; }
        const auto r = juce::Rectangle<float> (hoverPos.x + 12, hoverPos.y - 24, 7.0f * t.length() + 14, 20).constrainedWithin (getLocalBounds().toFloat());
        g.setColour (juce::Colour (0xee20232c));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (text);
        g.setFont (UI::font (12.0f));
        g.drawText (t, r.toNearestInt(), juce::Justification::centred);
    }
}

//------------------------------------------------------------------ EQ view
float SketchScreen::freqToX (double f) const
{
    const auto pa = plot();
    return (float) (pa.getX() + pa.getWidth() * (std::log (std::max (f, 20.0) / 20.0) / std::log (1000.0)));
}
double SketchScreen::xToFreq (float x) const
{
    const auto pa = plot();
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - pa.getX()) / pa.getWidth()));
    return 20.0 * std::pow (1000.0, t);
}
float SketchScreen::dbToY (double db) const
{
    const auto pa = plot();
    return pa.getCentreY() - (float) (db / 18.0) * pa.getHeight() * 0.5f;
}
double SketchScreen::yToDb (float y) const
{
    const auto pa = plot();
    return juce::jlimit (-18.0, 18.0, (double) ((pa.getCentreY() - y) / (pa.getHeight() * 0.5f)) * 18.0);
}
float SketchScreen::getEq (int band, const char* what) const
{
    return proc.apvts.getRawParameterValue (bandId (eqPost(), band, what))->load();
}
void SketchScreen::setEq (int band, const char* what, float plain)
{
    if (auto* p = proc.apvts.getParameter (bandId (eqPost(), band, what)))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}
void SketchScreen::beginEq (int band, const char* what)
{
    if (auto* p = proc.apvts.getParameter (bandId (eqPost(), band, what))) p->beginChangeGesture();
}
void SketchScreen::endEq (int band, const char* what)
{
    if (auto* p = proc.apvts.getParameter (bandId (eqPost(), band, what))) p->endChangeGesture();
}

juce::Point<float> SketchScreen::nodePos (int band) const
{
    const int type = (int) getEq (band, "type");
    const bool hasGain = (type == sd::Bell || type == sd::LowShelf || type == sd::HighShelf || type == sd::Tilt);
    return { freqToX (getEq (band, "freq")), dbToY (hasGain ? getEq (band, "gain") : 0.0) };
}

int SketchScreen::hitBand (juce::Point<float> p) const
{
    int best = -1;
    float bd = 10.0f;
    for (int b = 0; b < 6; ++b)
    {
        if (getEq (b, "on") < 0.5f) continue;
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bd) { bd = d; best = b; }
    }
    return best;
}

void SketchScreen::paintEq (juce::Graphics& g)
{
    using namespace UI;
    const auto pa = plot();
    const bool post = eqPost();
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const bool on = proc.apvts.getRawParameterValue (post ? PID::postOn : PID::preOn)->load() > 0.5f;

    g.setFont (UI::font (10.0f));
    for (int db = -12; db <= 12; db += 6)
    {
        g.setColour (db == 0 ? dim.withAlpha (0.6f) : track.withAlpha (0.5f));
        g.drawHorizontalLine ((int) dbToY (db), pa.getX(), pa.getRight());
        g.setColour (dim);
        g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db), (int) pa.getX() + 2, (int) dbToY (db) - 11, 30, 11, juce::Justification::left);
    }
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = freqToX (f);
        g.setColour (track.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, pa.getY(), pa.getBottom());
        g.setColour (dim);
        g.drawText (f >= 1000 ? juce::String (f / 1000.0, f == 1000.0 || f == 2000.0 || f == 5000.0 || f == 10000.0 ? 0 : 1) + "k" : juce::String ((int) f),
                    (int) x + 2, (int) pa.getBottom() - 12, 30, 11, juce::Justification::left);
    }

    // live spectrum
    juce::Path sp;
    sp.startNewSubPath (pa.getX(), pa.getBottom());
    for (int b = 2; b < 1024; ++b)
    {
        const double f = b * fs / 2048.0;
        if (f < 20.0 || f > 20000.0) continue;
        const float lvlN = juce::jlimit (0.0f, 1.0f, (spec[(size_t) b] + 100.0f) / 100.0f);
        sp.lineTo (freqToX (f), pa.getBottom() - lvlN * pa.getHeight() * 0.85f);
    }
    sp.lineTo (pa.getRight(), pa.getBottom());
    sp.closeSubPath();
    g.setColour (accent.withAlpha (0.18f));
    g.fillPath (sp);

    // curve
    sd::EqBand eb[6];
    for (int b = 0; b < 6; ++b)
    {
        sd::EqBandParams q;
        q.on = getEq (b, "on") > 0.5f; q.type = (int) getEq (b, "type"); q.freq = getEq (b, "freq");
        q.gain = getEq (b, "gain"); q.q = getEq (b, "q"); q.slope = (int) getEq (b, "slope");
        sd::designBand (q, fs, eb[b]);
    }
    juce::Path curve;
    bool first = true;
    for (float x = pa.getX(); x <= pa.getRight(); x += 2.0f)
    {
        const double f = xToFreq (x);
        double db = 0.0;
        for (auto& band : eb) db += sd::bandMagnitudeDb (band, fs, f);
        const float y = dbToY (juce::jlimit (-18.0, 18.0, db));
        if (first) { curve.startNewSubPath (x, y); first = false; } else curve.lineTo (x, y);
    }
    g.setColour ((on ? accent : dim).withAlpha (0.95f));
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    for (int b = 0; b < 6; ++b)
    {
        if (getEq (b, "on") < 0.5f) continue;
        const auto p = nodePos (b);
        const float r = (b == selBand) ? 7.5f : 6.0f;
        g.setColour (juce::Colour (0xff101218));
        g.fillEllipse (p.x - r - 1.5f, p.y - r - 1.5f, (r + 1.5f) * 2, (r + 1.5f) * 2);
        g.setColour (on ? accent : dim);
        g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
        g.setColour (juce::Colours::black);
        g.setFont (UI::font (9.0f, true));
        g.drawText (juce::String (b + 1), (int) (p.x - r), (int) (p.y - r), (int) (r * 2), (int) (r * 2), juce::Justification::centred);
    }
    g.setColour (dim);
    g.setFont (UI::font (11.0f));
    g.drawText (post ? "POST EQ  -  double-click: add / remove   wheel: Q   right-click: type, slope" : "PRE EQ  -  double-click: add / remove   wheel: Q   right-click: type, slope",
                (int) pa.getX() + 40, (int) pa.getY() + 2, (int) pa.getWidth() - 50, 14, juce::Justification::right);
}

void SketchScreen::showBandMenu (int band)
{
    juce::PopupMenu m;
    const char* types[8] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf" };
    const int cur = (int) getEq (band, "type");
    for (int i = 0; i < 8; ++i) m.addItem (1 + i, types[i], true, i == cur);
    if (cur == sd::LowCut || cur == sd::HighCut)
    {
        m.addSeparator();
        const char* sl[4] = { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };
        const int cs = (int) getEq (band, "slope");
        for (int i = 0; i < 4; ++i) m.addItem (101 + i, sl[i], true, i == cs);
    }
    m.addSeparator();
    m.addItem (200, "Reset band");
    m.addItem (201, "Delete band");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, band] (int r)
    {
        if (r >= 1 && r <= 8) setEq (band, "type", (float) (r - 1));
        else if (r >= 101 && r <= 104) setEq (band, "slope", (float) (r - 101));
        else if (r == 200) { setEq (band, "gain", 0.0f); setEq (band, "q", 0.71f); setEq (band, "type", 0.0f); setEq (band, "slope", 1.0f); }
        else if (r == 201) { setEq (band, "on", 0.0f); selBand = -1; }
    });
}

//------------------------------------------------------------------ mouse
void SketchScreen::mouseDown (const juce::MouseEvent& e)
{
    if (view == Level || view == Pitch)
    {
        computeBars();
        if (bars.empty()) return;
        const int i0 = nearestBar (e.position.x, -1);
        lockSide = isLinked() ? -1 : bars[(size_t) i0].side;
        lineValid = anchorValid; lineY = anchorY; lineSide = anchorSide;
        lineK = anchorK; lineSemis = anchorSemis;
        applyPoint (e.position.x, e.position.y, e.mods.isShiftDown(), true);
        return;
    }
    const int b = hitBand (e.position);
    if (e.mods.isPopupMenu()) { if (b >= 0) showBandMenu (b); return; }
    if (b >= 0)
    {
        if (e.mods.isAltDown()) { setEq (b, "on", 0.0f); selBand = -1; return; }
        selBand = b;
        eqStartX = e.position.x; eqStartY = e.position.y;
        eqF0 = getEq (b, "freq"); eqG0 = getEq (b, "gain");
        beginEq (b, "freq"); beginEq (b, "gain");
    }
    else selBand = -1;
}

void SketchScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (view == Level || view == Pitch)
    {
        applyPoint (e.position.x, e.position.y, e.mods.isShiftDown(), false);
        return;
    }
    if (selBand < 0) return;
    const auto pa = plot();
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
    const double f = juce::jlimit (20.0, 20000.0, eqF0 * std::pow (1000.0, (double) ((e.position.x - eqStartX) * fine / pa.getWidth())));
    setEq (selBand, "freq", (float) f);
    const int type = (int) getEq (selBand, "type");
    if (type == sd::Bell || type == sd::LowShelf || type == sd::HighShelf || type == sd::Tilt)
    {
        const double g = juce::jlimit (-18.0, 18.0, (double) eqG0 + (double) ((eqStartY - e.position.y) * fine / (pa.getHeight() * 0.5f)) * 18.0);
        setEq (selBand, "gain", (float) g);
    }
}

void SketchScreen::mouseUp (const juce::MouseEvent&)
{
    if ((view == PreEq || view == PostEq) && selBand >= 0) { endEq (selBand, "freq"); endEq (selBand, "gain"); }
}

void SketchScreen::mouseMove (const juce::MouseEvent& e)
{
    if (view == Pitch || view == Level)
    {
        computeBars();
        hoverBar = nearestBar (e.position.x, -1);
        hoverPos = e.position;
    }
}

void SketchScreen::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (view == Level || view == Pitch)
    {
        computeBars();
        if (bars.empty()) return;
        const int mask = isLinked() ? 3 : (1 << bars[(size_t) nearestBar (e.position.x, -1)].side);
        if (view == Level) proc.applyLevelPreset (0, mask); else proc.applyPitchPreset (0, mask);
        return;
    }
    const int b = hitBand (e.position);
    if (b >= 0) { setEq (b, "on", 0.0f); selBand = -1; return; }
    for (int i = 0; i < 6; ++i)
    {
        if (getEq (i, "on") > 0.5f) continue;
        const double f = xToFreq (e.position.x);
        const double db = yToDb (e.position.y);
        int type = sd::Bell;
        if (f < 50.0) type = db < 0 ? sd::LowCut : sd::LowShelf;
        else if (f > 10000.0) type = db < 0 ? sd::HighCut : sd::HighShelf;
        const bool hasGain = (type == sd::Bell || type == sd::LowShelf || type == sd::HighShelf);
        setEq (i, "type", (float) type);
        setEq (i, "freq", (float) f);
        setEq (i, "gain", hasGain ? (float) db : 0.0f);
        setEq (i, "q", 0.71f);
        setEq (i, "slope", 1.0f);
        setEq (i, "on", 1.0f);
        selBand = i;
        return;
    }
}

void SketchScreen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (view != PreEq && view != PostEq) return;
    const int b = hitBand (e.position);
    if (b < 0) return;
    const double q = juce::jlimit (0.1, 10.0, (double) getEq (b, "q") * std::exp ((double) w.deltaY * 0.6));
    setEq (b, "q", (float) q);
}

//==============================================================================
void SketchScope::paint (juce::Graphics& g)
{
    using namespace UI;
    g.setColour (screen);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (track);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

    const auto pa = getLocalBounds().toFloat().reduced (SketchScreen::kMargin, 5.0f);
    const float cy = pa.getCentreY(), half = pa.getHeight() * 0.5f;
    g.setColour (track.withAlpha (0.6f));
    g.drawHorizontalLine ((int) cy, pa.getX(), pa.getRight());

    const int cols = sd::Engine::kScopeCols;
    const int pos = std::min (proc.engine.scopePos.load(), cols);
    const float cw = pa.getWidth() / (float) cols;
    for (int c = 0; c < pos; ++c)
    {
        const float x = pa.getX() + (c + 0.5f) * cw;
        const float d = std::sqrt (std::min (1.0f, proc.engine.scopeDry[c].load()));
        const float w = std::sqrt (std::min (1.0f, proc.engine.scopeWet[c].load()));
        g.setColour (juce::Colour (0xff6b6f7d).withAlpha (0.8f));
        g.drawLine (x, cy - d * half, x, cy + d * half, juce::jmax (1.0f, cw));
        g.setColour (accent.withAlpha (0.9f));
        g.drawLine (x, cy - w * half, x, cy + w * half, juce::jmax (1.0f, cw));
    }
}

//==============================================================================
SketchEditor::SketchEditor (SketchProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), screen (p), scope (p)
{
    setLookAndFeel (&laf);
    setSize (940, 636);
    auto& s = proc.apvts;

    addAndMakeVisible (screen);
    addAndMakeVisible (scope);

    delayMs.init (*this, s, PID::ms, "TIME");
    color.init (*this, s, PID::color, "COLOR");   drive.init (*this, s, PID::drive, "DRIVE");
    width.init (*this, s, PID::width, "WIDTH");   duck.init (*this, s, PID::duck, "DUCK");
    mix.init (*this, s, PID::mix, "MIX");

    auto setupToggle = [&] (juce::TextButton& b, const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& a)
    {
        b.setClickingTogglesState (true);
        addAndMakeVisible (b);
        a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id, b);
    };
    setupToggle (linkShape, PID::linkShape, aLinkShape);
    setupToggle (syncBtn, PID::sync, aSync);
    setupToggle (ppBtn, PID::pingpong, aPP);
    setupToggle (imagerBtn, PID::imager, aImager);
    setupToggle (stackBtn, PID::stack, aStack);
    setupToggle (preOnBtn, PID::preOn, aPreOn);
    setupToggle (postOnBtn, PID::postOn, aPostOn);

    const char* modeNames[3] = { "TAPE", "ANALOG", "DIGITAL" };
    const char* rateNames[4] = { "1/4   -   4 BARS", "1/8   -   8 BARS", "1/16   -   16 BARS", "1/32   -   32 BARS" };
    const char* typeNames[3] = { "STR", "DOT", "TRI" };
    const char* spreadNames[3] = { "0", "1", "2" };
    const char* viewNames[4] = { "PRE EQ", "LEVEL", "PITCH", "POST EQ" };
    const char* levelNames[10] = { "Natural", "Linear", "Curve Dn", "Curve Up", "Ramp Up", "Gate", "Bounce", "Random", "Smooth", "Reverse" };
    const char* pitchNames[6] = { "Reset", "Octaves", "Arp", "Ramp", "Random", "Reverse" };
    for (int i = 0; i < 3; ++i)
    {
        modeBtn[i].setButtonText (modeNames[i]);     modeBtn[i].onClick = [this, i] { setChoice (PID::mode, i); };      addAndMakeVisible (modeBtn[i]);
        typeBtn[i].setButtonText (typeNames[i]);     typeBtn[i].onClick = [this, i] { setChoice (PID::noteType, i); };  addAndMakeVisible (typeBtn[i]);
        spreadBtn[i].setButtonText (spreadNames[i]); spreadBtn[i].onClick = [this, i] { setChoice (PID::spread, i); };  addAndMakeVisible (spreadBtn[i]);
    }
    for (int i = 0; i < 4; ++i)
    {
        rateBtn[i].setButtonText (rateNames[i]);  rateBtn[i].onClick = [this, i] { setChoice (PID::rate, i); };          addAndMakeVisible (rateBtn[i]);
        viewBtn[i].setButtonText (viewNames[i]);
        viewBtn[i].onClick = [this, i] { screen.setView ((SketchScreen::View) i); refreshView(); };
        addAndMakeVisible (viewBtn[i]);
    }
    for (int i = 0; i < 10; ++i)
    {
        levelBtn[i].setButtonText (levelNames[i]);
        levelBtn[i].onClick = [this, i]
        {
            if (i < 8) proc.applyLevelPreset (i, 3);
            else if (i == 8) proc.smoothLevels (3);
            else proc.reverseSketch (true, false, 3);
        };
        addAndMakeVisible (levelBtn[i]);
    }
    for (int i = 0; i < 6; ++i)
    {
        pitchBtn[i].setButtonText (pitchNames[i]);
        pitchBtn[i].onClick = [this, i]
        {
            if (i < 5) proc.applyPitchPreset (i, 3); else proc.reverseSketch (false, true, 3);
        };
        addAndMakeVisible (pitchBtn[i]);
    }

    for (auto* l : { &readout, &spreadLabel, &shapeLabel })
    {
        l->setFont (UI::font (12.0f, true));
        l->setColour (juce::Label::textColourId, UI::dim);
        addAndMakeVisible (*l);
    }
    readout.setJustificationType (juce::Justification::centredRight);
    spreadLabel.setText ("SPREAD TYPE", juce::dontSendNotification);

    refreshView();
    timerCallback();
    startTimerHz (20);
}

SketchEditor::~SketchEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SketchEditor::setChoice (const char* id, int index)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

void SketchEditor::refreshView()
{
    const auto v = screen.getView();
    for (int i = 0; i < 4; ++i) viewBtn[i].setToggleState (i == (int) v, juce::dontSendNotification);
    for (auto& b : levelBtn) b.setVisible (v == SketchScreen::Level);
    for (auto& b : pitchBtn) b.setVisible (v == SketchScreen::Pitch);
    stackBtn.setVisible (v == SketchScreen::Pitch);
    preOnBtn.setVisible (v == SketchScreen::PreEq);
    postOnBtn.setVisible (v == SketchScreen::PostEq);
}

void SketchEditor::timerCallback()
{
    auto& s = proc.apvts;
    const int mode = juce::jlimit (0, 2, (int) s.getRawParameterValue (PID::mode)->load());
    if (mode != lastMode)
    {
        lastMode = mode;
        laf.accent = UI::accentFor (mode);
        screen.setAccent (laf.accent);
        scope.setAccent (laf.accent);
        repaint();
    }
    const int rate = proc.rateIndex();
    const int nt = juce::jlimit (0, 2, (int) s.getRawParameterValue (PID::noteType)->load());
    const int sp = juce::jlimit (0, 2, (int) s.getRawParameterValue (PID::spread)->load());
    for (int i = 0; i < 3; ++i)
    {
        modeBtn[i].setToggleState (i == mode, juce::dontSendNotification);
        typeBtn[i].setToggleState (i == nt, juce::dontSendNotification);
        spreadBtn[i].setToggleState (i == sp, juce::dontSendNotification);
    }
    for (int i = 0; i < 4; ++i) rateBtn[i].setToggleState (i == rate, juce::dontSendNotification);

    const bool sync = s.getRawParameterValue (PID::sync)->load() > 0.5f;
    delayMs.setVisible (! sync);
    for (auto& b : typeBtn) b.setEnabled (sync);

    const bool ls = s.getRawParameterValue (PID::linkShape)->load() > 0.5f;
    if (ls && ! lastLinkShape) proc.copyLeftToRight(); // turning LINK L/R back on copies left to right
    lastLinkShape = ls;

    auto& e = proc.engine;
    const int n0 = e.pubN0.load();
    const int t0 = (int) std::lround (e.pubT0.load() * 1000.0);
    readout.setText (juce::String (n0) + " bars   x   " + juce::String (t0) + " ms" + (e.pubPP.load() != 0 ? "   PING PONG" : ""), juce::dontSendNotification);

    scope.repaint();
}

void SketchEditor::paint (juce::Graphics& g)
{
    using namespace UI;
    g.setGradientFill (juce::ColourGradient (bg, 0.0f, 0.0f, juce::Colour (0xff12141a), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (text);
    g.setFont (UI::font (22.0f, true));
    g.drawText ("SKETCH DELAY", 20, 8, 300, 30, juce::Justification::centredLeft);
    g.setColour (laf.accent);
    g.fillRoundedRectangle (20.0f, 38.0f, 50.0f, 3.0f, 1.5f);
    g.setColour (dim);
    g.setFont (UI::font (11.0f));
    g.drawText ("draw every bar", 80, 31, 200, 14, juce::Justification::centredLeft);

    g.setColour (panel);
    g.fillRoundedRectangle (14.0f, (float) controlsTop, (float) getWidth() - 28.0f, 134.0f, 10.0f);
}

void SketchEditor::resized()
{
    const int W = getWidth();
    const int mx = W - 20 - 3 * 92;
    for (int i = 0; i < 3; ++i) modeBtn[i].setBounds (mx + i * 92, 10, 88, 28);

    // rate modes: four big buttons
    const int rw = (W - 40 - 3 * 8) / 4;
    for (int i = 0; i < 4; ++i) rateBtn[i].setBounds (20 + i * (rw + 8), 50, rw, 34);

    screen.setBounds (20, 92, W - 40, 250);

    int x = 20;
    for (int i = 0; i < 4; ++i) { viewBtn[i].setBounds (x, 348, 80, 26); x += 84; }
    linkShape.setBounds (W - 20 - 96, 348, 96, 26);
    readout.setBounds (W - 20 - 96 - 360, 348, 350, 26);

    // contextual tools row
    const int ty = 378;
    for (int i = 0; i < 10; ++i) levelBtn[i].setBounds (20 + i * 90, ty, 86, 24);
    for (int i = 0; i < 6; ++i) pitchBtn[i].setBounds (20 + i * 90, ty, 86, 24);
    stackBtn.setBounds (20 + 6 * 90, ty, 86, 24);
    preOnBtn.setBounds (20, ty, 86, 24);
    postOnBtn.setBounds (20, ty, 86, 24);

    scope.setBounds (20, 408, W - 40, 60);

    const int y = controlsTop + 8, kh = 112;
    color.setBounds ({ 24, y, 96, kh });
    drive.setBounds ({ 124, y, 96, kh });
    width.setBounds ({ 224, y, 96, kh });
    duck.setBounds ({ 324, y, 96, kh });
    mix.setBounds ({ 424, y, 96, kh });
    delayMs.setBounds ({ 524, y, 96, kh });

    syncBtn.setBounds (650, y + 8, 84, 28);
    for (int i = 0; i < 3; ++i) typeBtn[i].setBounds (744 + i * 62, y + 8, 58, 28);
    ppBtn.setBounds (650, y + 44, 120, 28);
    imagerBtn.setBounds (780, y + 44, 110, 28);
    spreadLabel.setBounds (650, y + 78, 110, 16);
    for (int i = 0; i < 3; ++i) spreadBtn[i].setBounds (650 + i * 38, y + 98, 34, 24);
}
