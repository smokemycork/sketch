#pragma once
// Sketch Delay DSP engine - pure C++17, no framework dependencies (unit-testable).
//
// Architecture: every repeat is its own tap on one long input delay line, so each repeat has an
// independent level (drawn), pitch (drawn, shifted exactly once) and colour (progressively darker
// / more saturated with the repeat number, like an echo passing through the machine again).
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace sd
{
constexpr int kMaxRep = 64;
constexpr double kPi = 3.14159265358979323846;

inline float fastTanh (float x)
{
    if (x > 3.0f) x = 3.0f;
    if (x < -3.0f) x = -3.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

//==============================================================================
// Biquads & EQ
struct BQ { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
struct BQState { double z1 = 0, z2 = 0; };

inline double bqProc (const BQ& c, BQState& s, double x)
{
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

enum EqType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, Tilt };

inline BQ rbj (int type, double fs, double f, double gDb, double Q)
{
    f = std::min (std::max (f, 10.0), 0.49 * fs);
    Q = std::max (Q, 0.05);
    const double w0 = 2.0 * kPi * f / fs, cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * Q);
    const double A = std::pow (10.0, gDb / 40.0);
    const double sA = 2.0 * std::sqrt (A) * alpha;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
        case Bell:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
        case LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cw + sA);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sA);
            a0 = (A + 1) + (A - 1) * cw + sA;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sA; break;
        case HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cw + sA);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sA);
            a0 = (A + 1) - (A - 1) * cw + sA;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sA; break;
        case LowCut: // high-pass
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case HighCut: // low-pass
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case Notch:
            b0 = 1; b1 = -2 * cw; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case BandPass:
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        default: break;
    }
    BQ c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

inline BQ firstOrder (bool highpass, double fs, double f)
{
    f = std::min (std::max (f, 10.0), 0.49 * fs);
    const double K = std::tan (kPi * f / fs);
    const double n = 1.0 / (1.0 + K);
    BQ c;
    if (highpass) { c.b0 = n; c.b1 = -n; }
    else          { c.b0 = K * n; c.b1 = K * n; }
    c.a1 = (K - 1.0) * n;
    return c;
}

struct EqBandParams
{
    bool on = false;
    int type = Bell;
    double freq = 1000.0, gain = 0.0, q = 0.707;
    int slope = 1; // 0:6  1:12  2:24  3:48 dB/oct (cuts only)
    bool operator== (const EqBandParams& o) const
    {
        return on == o.on && type == o.type && freq == o.freq && gain == o.gain && q == o.q && slope == o.slope;
    }
};

struct EqBand
{
    int nSec = 0;
    BQ c[4];
    BQState s[2][4];
};

inline void designBand (const EqBandParams& p, double fs, EqBand& out)
{
    out.nSec = 0;
    if (! p.on) return;
    const double f = std::min (std::max (p.freq, 10.0), 0.49 * fs);
    auto add = [&] (const BQ& c) { out.c[out.nSec++] = c; };

    switch (p.type)
    {
        case Bell:      add (rbj (Bell, fs, f, p.gain, p.q)); break;
        case LowShelf:  add (rbj (LowShelf, fs, f, p.gain, p.q)); break;
        case HighShelf: add (rbj (HighShelf, fs, f, p.gain, p.q)); break;
        case Notch:     add (rbj (Notch, fs, f, 0.0, p.q)); break;
        case BandPass:  add (rbj (BandPass, fs, f, 0.0, p.q)); break;
        case Tilt:
            add (rbj (LowShelf, fs, f, -0.5 * p.gain, 0.707));
            add (rbj (HighShelf, fs, f, 0.5 * p.gain, 0.707));
            break;
        case LowCut:
        case HighCut:
        {
            const int type = p.type;
            const double extra = std::max (1.0, p.q / 0.7071); // Q above .71 = resonant bump at the corner
            if (p.slope <= 0) { add (firstOrder (type == LowCut, fs, f)); }
            else if (p.slope == 1) { add (rbj (type, fs, f, 0.0, p.q)); }
            else if (p.slope == 2)
            {
                add (rbj (type, fs, f, 0.0, 0.5412));
                add (rbj (type, fs, f, 0.0, 1.3066 * extra));
            }
            else
            {
                add (rbj (type, fs, f, 0.0, 0.5098));
                add (rbj (type, fs, f, 0.0, 0.6013));
                add (rbj (type, fs, f, 0.0, 0.9000));
                add (rbj (type, fs, f, 0.0, 2.5629 * extra));
            }
            break;
        }
        default: break;
    }
}

inline double bandMagnitudeDb (const EqBand& b, double fs, double f)
{
    if (b.nSec == 0) return 0.0;
    const double w = 2.0 * kPi * f / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    double mag = 1.0;
    for (int i = 0; i < b.nSec; ++i)
    {
        const BQ& c = b.c[i];
        const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
        const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;
        mag *= std::abs (num) / std::max (std::abs (den), 1e-12);
    }
    return 20.0 * std::log10 (std::max (mag, 1e-9));
}

struct StereoEq
{
    EqBand bands[6];
    EqBandParams cache[6];
    bool designed = false;

    void update (const EqBandParams* p, double fs)
    {
        for (int i = 0; i < 6; ++i)
            if (! designed || ! (cache[i] == p[i]))
            {
                cache[i] = p[i];
                designBand (p[i], fs, bands[i]);
            }
        designed = true;
    }

    inline void process (double& l, double& r, bool stereo)
    {
        for (auto& b : bands)
            for (int s = 0; s < b.nSec; ++s)
            {
                l = bqProc (b.c[s], b.s[0][s], l);
                if (stereo) r = bqProc (b.c[s], b.s[1][s], r);
            }
    }
};

//==============================================================================
// 2x half-band FIR resampling (47 taps, linear phase) used for 4x oversampled saturation
struct HalfbandKernel
{
    static constexpr int N = 47, C = 23;
    std::array<float, N> h{};
    HalfbandKernel()
    {
        for (int i = 0; i < N; ++i)
        {
            const int n = i - C;
            const double x = n * 0.5;
            const double s = (n == 0) ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double w = 0.42 - 0.5 * std::cos (2 * kPi * i / (N - 1)) + 0.08 * std::cos (4 * kPi * i / (N - 1));
            h[(size_t) i] = (float) (s * w);
        }
        // normalise DC gain to exactly 2
        double sum = 0; for (auto v : h) sum += v;
        for (auto& v : h) v = (float) (v * 2.0 / sum);
    }
};
inline const HalfbandKernel& hbk() { static HalfbandKernel k; return k; }

struct Up2
{
    std::array<float, 64> buf{}; int pos = 0;
    inline void push (float v) { pos = (pos + 1) & 63; buf[(size_t) pos] = v; }
    inline float fir() const
    {
        const auto& h = hbk().h; float acc = 0; int p = pos;
        for (int i = 0; i < HalfbandKernel::N; ++i) { acc += h[(size_t) i] * buf[(size_t) p]; p = (p - 1) & 63; }
        return acc;
    }
    inline void process (float x, float& o0, float& o1) { push (x); o0 = fir(); push (0.f); o1 = fir(); }
};

struct Down2
{
    std::array<float, 64> buf{}; int pos = 0;
    inline void push (float v) { pos = (pos + 1) & 63; buf[(size_t) pos] = v; }
    inline float process (float a, float b)
    {
        push (a); push (b);
        const auto& h = hbk().h; float acc = 0; int p = pos;
        for (int i = 0; i < HalfbandKernel::N; ++i) { acc += 0.5f * h[(size_t) i] * buf[(size_t) p]; p = (p - 1) & 63; }
        return acc;
    }
};

struct AP1
{
    double a = 0.5, z = 0;
    inline double process (double x) { const double y = a * x + z; z = x - a * y; return y; }
};

//==============================================================================
// 12-point Kaiser windowed-sinc fractional-delay interpolator (flat to ~18 kHz at 48 kHz)
struct SincTable
{
    static constexpr int Taps = 12, Phases = 512;
    float t[Phases + 2][Taps];
    static double i0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    }
    SincTable()
    {
        const double beta = 5.0, norm = i0 (beta);
        for (int q = 0; q < Phases + 2; ++q)
        {
            const double fr = (double) q / Phases;
            double h[Taps], sum = 0.0;
            for (int i = 0; i < Taps; ++i)
            {
                const double x = i - (Taps / 2 - 1) - fr;
                const double s = (std::fabs (x) < 1e-9) ? 1.0 : std::sin (kPi * x) / (kPi * x);
                const double r = x / (Taps / 2.0);
                const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / norm;
                h[i] = s * w; sum += h[i];
            }
            for (int i = 0; i < Taps; ++i) t[q][i] = (float) (h[i] / sum);
        }
    }
};
inline const SincTable& sincTable() { static SincTable st; return st; }

//==============================================================================
struct EngineParams
{
    int mode = 0;               // 0 Tape, 1 Analog, 2 Digital
    float color = 0, drive = 0, width = 1.0f, duck = 0, mix = 0.35f;
    int spread = 0;             // 0,1,2
    bool imager = false, pingpong = false, stack = false;
    double delaySec[2] = { 0.375, 0.375 };
    int reps[2] = { 7, 7 };
    bool preOn = true, postOn = true;
    EqBandParams pre[6], post[6];
    bool hostKnown = false, hostPlaying = false;
};

struct Shapes
{
    const float* level[2];  // 64 values 0..1 (0 = silent, 1 = 0 dB, 60 dB scale)
    const int* pitch[2];    // 64 semitone values, -24..24 (index = repeat number - 1)
    int len = 64;           // number of valid level entries (sketch bars); repeat k uses entry k-1 when len == repeats
};

inline float shapeAt (const float* sh, double u)
{
    const double x = std::min (std::max (u, 0.0), 1.0) * 63.0;
    const int i = (int) x;
    if (i >= 63) return sh[63];
    const double f = x - i;
    return (float) (sh[i] * (1.0 - f) + sh[i + 1] * f);
}
inline float shapeAtN (const float* sh, int len, double u)
{
    if (len <= 1) return sh[0];
    const double x = std::min (std::max (u, 0.0), 1.0) * (len - 1);
    const int i = (int) (x + 1.0e-9);
    if (i >= len - 1) return sh[len - 1];
    const double f = std::max (0.0, x - i);
    return (float) (sh[i] * (1.0 - f) + sh[i + 1] * f);
}
inline float yToGain (float y) { return y <= 0.003f ? 0.f : std::pow (10.0f, (y - 1.0f) * 3.0f); }
inline int effPitch (const int* pit, bool stack, int k)
{
    if (! stack) return pit[k - 1];
    int s = 0;
    for (int j = 0; j < k; ++j) s += pit[j];
    return std::min (std::max (s, -24), 24);
}

//==============================================================================
class Engine
{
public:
    static constexpr int kChunk = 256;
    static constexpr int kScopeCols = 600;
    static constexpr int kRing = 8192;
    static constexpr double kOsLat = 34.5; // total latency of the 4x oversampler, in samples

    Engine()
    {
        for (auto& a : scopeDry) a.store (0.f);
        for (auto& a : scopeWet) a.store (0.f);
        for (auto& a : ringPre) a = 0.f;
        for (auto& a : ringPost) a = 0.f;
    }

    // ------------------------------------------------------------------ UI-visible state
    std::atomic<float> pubSpan { 3.0f }, pubT0 { 0.375f }, pubT1 { 0.375f };
    std::atomic<int> pubN0 { 7 }, pubN1 { 7 }, pubPP { 0 };
    std::atomic<float> scopeDry[kScopeCols], scopeWet[kScopeCols];
    std::atomic<int> scopePos { kScopeCols };
    float ringPre[kRing], ringPost[kRing];
    std::atomic<int> ringPreWp { 0 }, ringPostWp { 0 };

    // ------------------------------------------------------------------ setup
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const size_t want = (size_t) (sr * 21.5) + 64;
        lineSize = 1; while ((size_t) lineSize < want) lineSize <<= 1;
        mask = lineSize - 1;
        line[0].assign ((size_t) lineSize, 0.f);
        line[1].assign ((size_t) lineSize, 0.f);
        maxD = (double) lineSize - 16.0;
        gc = 1.0 - std::exp (-1.0 / (0.005 * sr));
        tc = 1.0 - std::exp (-1.0 / (0.025 * sr));
        for (int k = 0; k <= kMaxRep; ++k)
        {
            c1[k] = std::cos (0.9 * k); s1[k] = std::sin (0.9 * k);
            c2[k] = std::cos (1.7 * k); s2[k] = std::sin (1.7 * k);
        }
        colourKeyMode = -1;
        reset();
    }

    bool isPrepared() const { return lineSize > 0; }

    void reset()
    {
        std::fill (line[0].begin(), line[0].end(), 0.f);
        std::fill (line[1].begin(), line[1].end(), 0.f);
        wp = 0;
        Tinit = false;
        resetBanks();
        for (auto& b : bump) b = BQState{};
        preEq = StereoEq{}; postEq = StereoEq{};
        for (int c = 0; c < 2; ++c) { up1[c] = Up2{}; up2[c] = Up2{}; dn2[c] = Down2{}; dn1[c] = Down2{}; }
        envIn = envOut = 0; gMatch = 1.0f; dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0;
        duckEnv = 0; duckGain = 1.0f;
        imgHp = 0; for (auto& a : imgAp) a.z = 0;
        ph1 = ph2 = 0;
        scopeRunning = false; prevPlaying = false;
    }

    // ------------------------------------------------------------------ processing
    // In-place. R may be nullptr for a mono track.
    void process (float* L, float* R, int n, const EngineParams& p, const Shapes& sh)
    {
        int done = 0;
        while (done < n)
        {
            const int m = std::min (kChunk, n - done);
            processChunk (L + done, R ? R + done : nullptr, m, p, sh);
            done += m;
        }
    }

private:
    struct Tap { int k = 1; double mL = 0, mR = 0; float target = 0; int semis = 0; };
    struct Bank
    {
        Tap tap[kMaxRep];
        int nTap = 0, prevN = 0;
        float gain[kMaxRep] = {}, z1[kMaxRep] = {}, z2[kMaxRep] = {}, ph[kMaxRep] = {};
    };
    struct LP32 { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct ModeChar { double fc1, slope, fmin, sat, flutter, bump; };

    double sr = 44100.0;
    std::vector<float> line[2];
    int lineSize = 0, mask = 0, wp = 0;
    double maxD = 1e6, gc = 0.01, tc = 0.001;
    double Tt[2] = { 0.375, 0.375 }, Tsm[2] = { 0.375, 0.375 };
    bool Tinit = false, lastPP = false;
    Bank bank[2];
    double c1[kMaxRep + 1], s1[kMaxRep + 1], c2[kMaxRep + 1], s2[kMaxRep + 1];
    double ph1 = 0, ph2 = 0;
    // per-sample LFO values
    double lS1 = 0, lC1 = 1, lS2 = 0, lC2 = 1, curT0 = 0, curT1 = 0, spreadSamp = 0;

    // colour tables
    int colourKeyMode = -1; float colourKeyColor = -1.f; double colourKeySr = 0;
    LP32 lp[kMaxRep + 1];
    float satG[kMaxRep + 1];
    double flutS[kMaxRep + 1];
    BQ bumpC; bool bumpOn = false; BQState bump[2];

    StereoEq preEq, postEq;
    Up2 up1[2], up2[2]; Down2 dn2[2], dn1[2];
    float envIn = 0, envOut = 0, gMatch = 1.0f;
    double dcX[2] = {}, dcY[2] = {};
    float duckEnv = 0, duckGain = 1.0f;
    double imgHp = 0; AP1 imgAp[4] = { { 0.45, 0 }, { -0.62, 0 }, { 0.73, 0 }, { -0.81, 0 } };

    // scope
    bool scopeRunning = false, prevPlaying = false;
    int scopeCol = 0; double scopeCnt = 0, scopeColLen = 1; float accDry = 0, accWet = 0;
    bool imgInit = false;

    float dryL[kChunk], dryR[kChunk], wetL[kChunk], wetR[kChunk];

    void resetBanks()
    {
        for (auto& b : bank)
        {
            b.nTap = b.prevN = 0;
            std::fill (std::begin (b.gain), std::end (b.gain), 0.f);
            std::fill (std::begin (b.z1), std::end (b.z1), 0.f);
            std::fill (std::begin (b.z2), std::end (b.z2), 0.f);
            std::fill (std::begin (b.ph), std::end (b.ph), 0.f);
        }
    }

    // ------------------------------------------------------------------ control-rate update
    void control (const EngineParams& p, const Shapes& sh, bool stereo, bool pp)
    {
        double T[2];
        int N[2];
        if (pp)
        {
            const int M = std::min (std::max ((int) std::lround ((p.reps[0] + p.reps[1]) * 0.5), 1), kMaxRep);
            N[0] = N[1] = M;
            T[0] = std::min (std::max (p.delaySec[0], 0.001), 8.0);
            T[1] = std::min (std::max (p.delaySec[1], 0.001), 8.0);
            const double tot = M * 0.5 * (T[0] + T[1]);
            if (tot > 20.0) { T[0] *= 20.0 / tot; T[1] *= 20.0 / tot; }
        }
        else
        {
            for (int s = 0; s < 2; ++s)
            {
                N[s] = std::min (std::max (p.reps[s], 1), kMaxRep);
                T[s] = std::min (std::min (std::max (p.delaySec[s], 0.001), 8.0), 20.0 / N[s]);
            }
        }
        if (! stereo) { T[1] = T[0]; N[1] = N[0]; }
        Tt[0] = T[0]; Tt[1] = T[1];
        if (! Tinit) { Tsm[0] = Tt[0]; Tsm[1] = Tt[1]; Tinit = true; }

        pubT0.store ((float) T[0]); pubT1.store ((float) T[1]);
        pubN0.store (N[0]); pubN1.store (N[1]); pubPP.store (pp ? 1 : 0);
        double span = std::max (N[0] * T[0], N[1] * T[1]);
        if (pp) span = N[0] * 0.5 * (T[0] + T[1]);
        span += 0.5 * std::max (T[0], T[1]);
        pubSpan.store ((float) span);
        scopeColLen = span * sr / kScopeCols;

        spreadSamp = (stereo && ! pp) ? (p.spread == 1 ? 0.010 : p.spread == 2 ? 0.020 : 0.0) * sr : 0.0;

        const int nb = stereo ? 2 : 1;
        for (int b = 0; b < nb; ++b)
        {
            Bank& bk = bank[b];
            int n = 0;
            if (! pp)
            {
                for (int k = 1; k <= N[b]; ++k, ++n)
                {
                    Tap& t = bk.tap[n];
                    t.k = k; t.mL = (b == 0) ? k : 0; t.mR = (b == 1) ? k : 0;
                    t.target = yToGain (shapeAtN (sh.level[b], sh.len, N[b] > 1 ? (k - 1.0) / (N[b] - 1.0) : 0.0));
                    t.semis = effPitch (sh.pitch[b], p.stack, k);
                }
            }
            else
            {
                const int M = N[0];
                for (int k = (b == 0 ? 1 : 2); k <= M; k += 2, ++n)
                {
                    const int side = (k & 1) ? 0 : 1;
                    Tap& t = bk.tap[n];
                    t.k = k; t.mL = (k + 1) / 2; t.mR = k / 2;
                    t.target = yToGain (shapeAtN (sh.level[side], sh.len, M > 1 ? (k - 1.0) / (M - 1.0) : 0.0));
                    t.semis = effPitch (sh.pitch[side], p.stack, k);
                }
            }
            bk.nTap = n;
            for (int j = bk.prevN; j < n; ++j) { bk.gain[j] = 0; bk.z1[j] = bk.z2[j] = 0; bk.ph[j] = 0; }
            bk.prevN = n;
        }
        if (! stereo) bank[1].nTap = 0;
    }

    // ------------------------------------------------------------------ colour (mode + color)
    void ensureColour (const EngineParams& p)
    {
        if (colourKeyMode == p.mode && colourKeyColor == p.color && colourKeySr == sr) return;
        colourKeyMode = p.mode; colourKeyColor = p.color; colourKeySr = sr;

        const ModeChar TAPE   { 11000, 0.38, 1800, 0.35, 1.0, 0.0 };
        const ModeChar TAPENF { 11000, 0.38, 1800, 0.35, 0.0, 0.0 };
        const ModeChar ANALOG { 4800, 0.32, 700, 0.55, 0.0, 1.0 };
        const ModeChar DIGI   { 22000, 0.0, 22000, 0.0, 0.0, 0.0 };
        const ModeChar CLEANF { 22000, 0.0, 22000, 0.0, 1.0, 0.0 };

        auto lerp = [] (double a, double b, double t) { return a + (b - a) * t; };
        auto blend = [&] (const ModeChar& a, const ModeChar& b, double t)
        {
            ModeChar r;
            r.fc1 = std::exp (lerp (std::log (a.fc1), std::log (b.fc1), t));
            r.slope = lerp (a.slope, b.slope, t);
            r.fmin = std::exp (lerp (std::log (a.fmin), std::log (b.fmin), t));
            r.sat = lerp (a.sat, b.sat, t); r.flutter = lerp (a.flutter, b.flutter, t); r.bump = lerp (a.bump, b.bump, t);
            return r;
        };

        const double c = std::min (std::max ((double) p.color, 0.0), 1.0);
        ModeChar m;
        if (p.mode == 0)      m = blend (TAPE, CLEANF, c);
        else if (p.mode == 1) m = blend (ANALOG, blend (TAPENF, DIGI, 0.5), c);
        else                  m = blend (DIGI, TAPENF, c);

        for (int k = 1; k <= kMaxRep; ++k)
        {
            double fc = std::max (m.fc1 * std::pow ((double) k, -m.slope), m.fmin);
            fc = std::min (fc, m.fc1);
            LP32 q;
            if (fc >= 0.45 * sr) { q = LP32{}; }
            else
            {
                const BQ b = rbj (HighCut, sr, fc, 0.0, 0.75);
                q.b0 = (float) b.b0; q.b1 = (float) b.b1; q.b2 = (float) b.b2; q.a1 = (float) b.a1; q.a2 = (float) b.a2;
            }
            lp[k] = q;
            satG[k] = (float) (1.0 + m.sat * 0.9 * std::sqrt ((double) k));
            if (m.sat < 1e-4) satG[k] = 1.0f;
            flutS[k] = std::min (m.flutter * 0.00012 * sr * (0.6 + 0.4 * std::sqrt ((double) k)), 0.0012 * sr);
        }
        bumpOn = m.bump > 0.01;
        if (bumpOn) bumpC = rbj (Bell, sr, 800.0, 3.5 * m.bump, 0.8);
    }

    void ensureEq (const EngineParams& p)
    {
        preEq.update (p.pre, sr);
        postEq.update (p.post, sr);
    }

    // ------------------------------------------------------------------ tap reading
    inline float readH (const float* ln, double D) const
    {
        const double pos = (double) wp - D;
        const double fl = std::floor (pos);
        const int i1 = (int) fl;
        const double fq = (pos - fl) * SincTable::Phases;
        const int q = (int) fq;
        const float a = (float) (fq - q);
        const auto& tb = sincTable();
        const float* h0 = tb.t[q];
        const float* h1 = tb.t[q + 1];
        float acc = 0.f;
        for (int i = 0; i < SincTable::Taps; ++i)
            acc += (h0[i] + a * (h1[i] - h0[i])) * ln[(i1 + i - 5) & mask];
        return acc;
    }

    float runBank (int b, int li)
    {
        Bank& bk = bank[b];
        const float* ln = line[li].data();
        const double sideSign = (b == 0) ? -1.0 : 1.0;
        const double wMax = 0.06 * sr;
        float acc = 0.f;

        for (int j = 0; j < bk.nTap; ++j)
        {
            const Tap& t = bk.tap[j];
            const float g = (bk.gain[j] += (float) gc * (t.target - bk.gain[j]));
            if (g < 1e-5f && t.target == 0.f) continue;

            const int k = t.k;
            double D = t.mL * curT0 + t.mR * curT1;
            if (spreadSamp > 0.0) D += sideSign * std::min (k * spreadSamp * 0.5, 0.5 * D);
            if (flutS[k] > 0.0)
                D += flutS[k] * (lS1 * c1[k] + lC1 * s1[k] + 0.35 * (lS2 * c2[k] + lC2 * s2[k]));
            D -= kOsLat;
            if (D < 8.0) D = 8.0;
            if (D > maxD) D = maxD;

            float x;
            double W = 0.0;
            if (t.semis != 0) W = std::min (wMax, 2.0 * (D - 8.0));
            if (t.semis == 0 || W < 32.0)
            {
                x = readH (ln, D);
            }
            else
            {
                // dual-grain delay-line pitch shifter, applied once to the clean echo
                const double ratio = std::pow (2.0, t.semis / 12.0);
                double p0 = bk.ph[j];
                double pB = p0 + 0.5; if (pB >= 1.0) pB -= 1.0;
                const float wA = 0.5f - 0.5f * std::cos ((float) (2.0 * kPi * p0));
                const float xa = readH (ln, D + W * (p0 - 0.5));
                const float xb = readH (ln, D + W * (pB - 0.5));
                x = wA * xa + (1.0f - wA) * xb;
                p0 += (1.0 - ratio) / W;
                p0 -= std::floor (p0);
                bk.ph[j] = (float) p0;
            }

            const float sg = satG[k];
            if (sg > 1.0001f) x = fastTanh (sg * x) / sg;

            const LP32& c = lp[k];
            const float y = c.b0 * x + bk.z1[j];
            bk.z1[j] = c.b1 * x - c.a1 * y + bk.z2[j];
            bk.z2[j] = c.b2 * x - c.a2 * y;
            acc += g * y;
        }
        return acc;
    }

    // ------------------------------------------------------------------ one chunk
    void processChunk (float* L, float* R, int n, const EngineParams& p, const Shapes& sh)
    {
        const bool st = (R != nullptr);
        const bool pp = p.pingpong && st;
        control (p, sh, st, pp);
        if (pp != lastPP) { resetBanks(); control (p, sh, st, pp); lastPP = pp; }
        ensureColour (p);
        ensureEq (p);

        const double twoPiOverSr = 2.0 * kPi / sr;
        const int nb = st ? 2 : 1;

        for (int i = 0; i < n; ++i)
        {
            const double dl = L[i], dr = st ? R[i] : 0.0;
            dryL[i] = (float) dl; dryR[i] = (float) dr;

            double el = dl, er = dr;
            if (p.preOn) preEq.process (el, er, st);
            ringPre[(ringPreWpLocal++) & (kRing - 1)] = (float) (st ? 0.5 * (el + er) : el);

            if (pp) line[0][(size_t) wp] = (float) (0.5 * (el + er));
            else { line[0][(size_t) wp] = (float) el; if (st) line[1][(size_t) wp] = (float) er; }

            Tsm[0] += tc * (Tt[0] - Tsm[0]);
            Tsm[1] += tc * (Tt[1] - Tsm[1]);
            curT0 = Tsm[0] * sr; curT1 = Tsm[1] * sr;

            ph1 += twoPiOverSr * 0.6;  if (ph1 > 2 * kPi) ph1 -= 2 * kPi;
            ph2 += twoPiOverSr * 5.4;  if (ph2 > 2 * kPi) ph2 -= 2 * kPi;
            lS1 = std::sin (ph1); lC1 = std::cos (ph1); lS2 = std::sin (ph2); lC2 = std::cos (ph2);

            float aL = runBank (0, 0);
            float aR = 0.f;
            if (nb == 2) aR = runBank (1, pp ? 0 : 1);

            if (bumpOn)
            {
                aL = (float) bqProc (bumpC, bump[0], aL);
                if (nb == 2) aR = (float) bqProc (bumpC, bump[1], aR);
            }
            wetL[i] = aL; wetR[i] = aR;
            wp = (wp + 1) & mask;
        }
        ringPreWp.store (ringPreWpLocal & (kRing - 1));

        // ---------------- Drive (4x oversampled, level-matched)
        {
            const float dr = p.drive;
            const float dm = std::min (1.0f, dr * 20.0f);
            const float g = 1.0f + 10.0f * dr;
            const float bias = 0.25f * dr;
            const float tb = std::tanh (bias);
            const float eC = (float) (1.0 - std::exp (-1.0 / (0.1 * sr)));
            const float mC = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < nb; ++c)
                {
                    const float x = c == 0 ? wetL[i] : wetR[i];
                    float a0, a1, b0, b1, b2, b3;
                    up1[c].process (x, a0, a1);
                    up2[c].process (a0, b0, b1);
                    up2[c].process (a1, b2, b3); // second half of the 2x stream
                    float v[4] = { b0, b1, b2, b3 };
                    if (dm > 0.f)
                        for (auto& s : v)
                        {
                            const float sat = std::tanh (g * s + bias) - tb;
                            s = s + dm * (sat - s);
                        }
                    const float d0 = dn2[c].process (v[0], v[1]);
                    const float d1 = dn2[c].process (v[2], v[3]);
                    float y = dn1[c].process (d0, d1);

                    if (c == 0)
                    {
                        envIn += eC * (x * x - envIn);
                        envOut += eC * (y * y - envOut);
                    }
                    if (dm > 0.f)
                    {
                        const float gm = std::sqrt ((envIn + 1e-9f) / (envOut + 1e-9f));
                        if (c == 0) gMatch += mC * (std::min (std::max (gm, 0.1f), 4.0f) - gMatch);
                        y *= 1.0f + dm * (gMatch - 1.0f);
                    }
                    // DC blocker
                    const double yd = y - dcX[c] + 0.9995 * dcY[c];
                    dcX[c] = y; dcY[c] = yd;
                    if (c == 0) wetL[i] = (float) yd; else wetR[i] = (float) yd;
                }
            }
        }

        // ---------------- Post EQ, stereo tools, duck, mix
        const float attC = (float) (1.0 - std::exp (-1.0 / (0.002 * sr)));
        const float relC = (float) (1.0 - std::exp (-1.0 / (0.4 * sr)));
        const float dgC = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
        const double hpC = std::exp (-2.0 * kPi * 250.0 / sr);

        for (int i = 0; i < n; ++i)
        {
            double wl = wetL[i], wr = wetR[i];
            if (p.postOn) postEq.process (wl, wr, st);
            ringPost[(ringPostWpLocal++) & (kRing - 1)] = (float) (st ? 0.5 * (wl + wr) : wl);

            if (st)
            {
                const double mid = 0.5 * (wl + wr);
                double side = 0.5 * (wl - wr) * p.width;
                if (p.imager && p.spread == 0 && p.width > 1.0f)
                {
                    imgHp = hpC * imgHp + (1.0 - hpC) * mid;      // low-passed mid
                    double d = mid - imgHp;                       // high-passed mid (lows stay centred)
                    for (auto& a : imgAp) d = a.process (d);
                    side += d * (p.width - 1.0) * 0.9;
                }
                wl = mid + side; wr = mid - side;
            }

            // duck: follows the dry input
            const float lvl = std::max (std::fabs (dryL[i]), std::fabs (dryR[i]));
            duckEnv += (lvl > duckEnv ? attC : relC) * (lvl - duckEnv);
            float gTarget = 1.0f;
            if (p.duck > 0.f)
            {
                const float envDb = 20.0f * std::log10 (duckEnv + 1e-9f);
                const float red = p.duck * std::max (0.0f, envDb + 60.0f) * 0.5f;
                gTarget = std::pow (10.0f, -red / 20.0f);
            }
            duckGain += dgC * (gTarget - duckGain);
            wl *= duckGain; wr *= duckGain;

            const float m = p.mix;
            const float oL = dryL[i] * (1.0f - m) + (float) wl * m;
            const float oR = dryR[i] * (1.0f - m) + (float) wr * m;
            L[i] = oL;
            if (st) R[i] = oR;

            // scope
            const float dPk = std::max (std::fabs (dryL[i]), std::fabs (dryR[i]));
            const float wPk = std::max (std::fabs ((float) wl), std::fabs ((float) wr));
            updateScope (dPk, wPk, p);
        }
        ringPostWp.store (ringPostWpLocal & (kRing - 1));
        prevPlaying = p.hostPlaying;
    }

    int ringPreWpLocal = 0, ringPostWpLocal = 0;

    void startSweep()
    {
        for (auto& a : scopeDry) a.store (0.f);
        for (auto& a : scopeWet) a.store (0.f);
        scopeCol = 0; scopeCnt = 0; accDry = accWet = 0;
        scopeRunning = true;
        scopePos.store (0);
    }

    void updateScope (float dPk, float wPk, const EngineParams& p)
    {
        if (p.hostKnown)
        {
            if (! p.hostPlaying) return; // frozen while stopped
            if (p.hostPlaying && ! prevPlaying) startSweep();
        }
        if (! scopeRunning && dPk > 0.1f) startSweep();
        if (! scopeRunning) return;
        accDry = std::max (accDry, dPk);
        accWet = std::max (accWet, wPk);
        scopeCnt += 1.0;
        if (scopeCnt >= scopeColLen)
        {
            scopeCnt -= scopeColLen;
            if (scopeCol < kScopeCols)
            {
                scopeDry[scopeCol].store (accDry);
                scopeWet[scopeCol].store (accWet);
                ++scopeCol;
                scopePos.store (scopeCol);
            }
            accDry = accWet = 0.f;
            if (scopeCol >= kScopeCols) scopeRunning = false;
        }
    }
};
} // namespace sd
