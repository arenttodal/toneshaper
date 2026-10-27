// DSP test suite for the ToneShaper engine (spec section 11.9).
// Self-contained: no test framework, exits non-zero on failure.

#include "Filters.h"
#include "LogGrid.h"
#include "Loudness.h"
#include "Mappings.h"
#include "MinPhase.h"
#include "RealFFT.h"
#include "SpectralCore.h"
#include "ToneShaperEngine.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace toneshaper;

namespace
{

int failures = 0;
int checks = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        ++checks;                                                            \
        if (! (cond)) {                                                      \
            ++failures;                                                      \
            std::printf ("    FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                       \
            std::printf ("\n");                                              \
        }                                                                    \
    } while (0)

constexpr double pi = 3.14159265358979323846;

struct Noise
{
    uint32_t state = 0x12345678u;

    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) ((int32_t) state) / 2147483648.0f;
    }
};

std::vector<float> whiteNoise (int n, float amplitude, uint32_t seed = 1)
{
    Noise rng;
    rng.state = seed;
    std::vector<float> x ((size_t) n);

    for (auto& v : x)
        v = amplitude * rng.next();

    return x;
}

/** RBJ peaking EQ, used to put a resonance into test signals. */
void applyPeak (std::vector<float>& x, double fs, double f0, double gainDb, double q)
{
    const double a = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f0 / fs;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha / a;
    Biquad b;
    b.b0 = (float) ((1.0 + alpha * a) / a0);
    b.b1 = (float) (-2.0 * std::cos (w0) / a0);
    b.b2 = (float) ((1.0 - alpha * a) / a0);
    b.a1 = b.b1;
    b.a2 = (float) ((1.0 - alpha / a) / a0);

    for (auto& v : x)
        v = b.process (v);
}

/** Welch power spectrum (Hann, 50 % overlap) -> 1/3-octave band power density in dB at the
    given centre frequencies. */
std::vector<double> thirdOctaveLevels (const float* x, int n, double fs, const std::vector<double>& centres)
{
    const int N = 8192;
    RealFFT fft;
    fft.prepare (N);
    AlignedBuffer frame ((size_t) N), spec ((size_t) N);
    std::vector<double> power ((size_t) N / 2 + 1, 0.0);
    int frames = 0;

    for (int start = 0; start + N <= n; start += N / 2)
    {
        for (int i = 0; i < N; ++i)
            frame[(size_t) i] = x[start + i] * (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / N));

        fft.forward (frame.data(), spec.data());

        for (int k = 1; k < N / 2; ++k)
            power[(size_t) k] += (double) spec[(size_t) (2 * k)] * spec[(size_t) (2 * k)]
                               + (double) spec[(size_t) (2 * k + 1)] * spec[(size_t) (2 * k + 1)];
        ++frames;
    }

    std::vector<double> levels;

    for (double fc : centres)
    {
        const double lo = fc * std::pow (2.0, -1.0 / 6.0), hi = fc * std::pow (2.0, 1.0 / 6.0);
        double s = 0.0;

        for (int k = 1; k < N / 2; ++k)
        {
            const double f = k * fs / N;

            if (f >= lo && f < hi)
                s += power[(size_t) k];
        }

        // Per-Hz density, so white noise reads flat across bands.
        levels.push_back (10.0 * std::log10 (s / std::max (frames, 1) / (hi - lo) + 1e-30));
    }

    return levels;
}

std::vector<double> standardCentres()
{
    std::vector<double> c;

    for (double f = 100.0; f <= 12000.0; f *= std::pow (2.0, 1.0 / 3.0))
        c.push_back (f);

    return c;
}

double stddev (const std::vector<double>& v)
{
    double m = 0.0;

    for (double x : v)
        m += x;

    m /= (double) v.size();
    double s = 0.0;

    for (double x : v)
        s += (x - m) * (x - m);

    return std::sqrt (s / (double) v.size());
}

/** Runs a mono or stereo signal through the engine in blocks of blockSize. */
std::vector<std::vector<float>> runEngine (ToneShaperEngine& engine, std::vector<std::vector<float>> io,
                                           const EngineParams& p, int blockSize,
                                           const std::vector<std::vector<float>>* sidechain = nullptr)
{
    const int numCh = (int) io.size();
    const int n = (int) io[0].size();

    for (int start = 0; start < n; start += blockSize)
    {
        const int len = std::min (blockSize, n - start);
        float* ptrs[2] = { io[0].data() + start, numCh > 1 ? io[1].data() + start : nullptr };
        const float* sc[2] = { nullptr, nullptr };

        if (sidechain != nullptr)
            for (size_t c = 0; c < sidechain->size(); ++c)
                sc[c] = (*sidechain)[c].data() + start;

        engine.process (ptrs, numCh, sidechain != nullptr ? sc : nullptr,
                        sidechain != nullptr ? (int) sidechain->size() : 0, len, p);
    }

    return io;
}

double rmsDb (const float* x, int n)
{
    double s = 0.0;

    for (int i = 0; i < n; ++i)
        s += (double) x[i] * x[i];

    return 10.0 * std::log10 (s / std::max (n, 1) + 1e-30);
}

/** Error between output and input delayed by `delay`, relative to the input, in dB. */
double nullDb (const std::vector<float>& in, const std::vector<float>& out, int delay, int skip)
{
    double err = 0.0, ref = 0.0;

    for (size_t i = (size_t) (skip + delay); i < out.size(); ++i)
    {
        const double e = out[i] - in[i - (size_t) delay];
        err += e * e;
        ref += (double) in[i - (size_t) delay] * in[i - (size_t) delay];
    }

    return 10.0 * std::log10 (err / (ref + 1e-30) + 1e-30);
}

int argmaxCrossCorrelation (const std::vector<float>& a, const std::vector<float>& b, int maxLag)
{
    int best = 0;
    double bestValue = -1e300;

    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double s = 0.0;

        for (size_t i = 0; i + (size_t) lag < b.size() && i < a.size(); ++i)
            s += (double) a[i] * b[i + (size_t) lag];

        if (s > bestValue)
        {
            bestValue = s;
            best = lag;
        }
    }

    return best;
}

EngineParams neutralParams()
{
    EngineParams p;
    p.amount = 0.0f;
    return p;
}

// ---------------------------------------------------------------------------------------------

void testFft()
{
    const int n = 1024;
    RealFFT fft;
    fft.prepare (n);
    AlignedBuffer x ((size_t) n), X ((size_t) n), y ((size_t) n);

    for (int i = 0; i < n; ++i)
        x[(size_t) i] = (float) (std::cos (2.0 * pi * 5.0 * i / n) + 0.25);

    fft.forward (x.data(), X.data());

    CHECK (std::abs (X[0] - 0.25f * n) < 1e-2f * n, "DC bin %f", X[0]);
    CHECK (std::abs (X[10] - 0.5f * n) < 1e-2f * n, "Re(bin 5) %f", X[10]);
    CHECK (std::abs (X[11]) < 1e-2f * n, "Im(bin 5) %f", X[11]);
    CHECK (std::abs (X[12]) < 1e-2f * n, "Re(bin 6) %f", X[12]);

    fft.inverse (X.data(), y.data());
    float maxErr = 0.0f;

    for (int i = 0; i < n; ++i)
        maxErr = std::max (maxErr, std::abs (y[(size_t) i] / n - x[(size_t) i]));

    CHECK (maxErr < 1e-5f, "round trip error %g", maxErr);
}

void testMinPhase()
{
    const int n = 1024, bins = n / 2 + 1;
    MinPhase mp;
    mp.prepare (n);

    // A smooth 12 dB low-shelf-like magnitude.
    std::vector<float> logMag ((size_t) bins), phase ((size_t) bins);

    for (int k = 0; k < bins; ++k)
    {
        const double x = (double) k / 20.0;
        logMag[(size_t) k] = (float) (std::log (std::pow (10.0, 12.0 / 20.0)) / (1.0 + x * x));
    }

    mp.computePhase (logMag.data(), phase.data());

    // Build H = |H| e^{j phase}, inverse FFT, check energy is front-loaded (causal, minimum phase)
    // and that the magnitude survives.
    RealFFT fft;
    fft.prepare (n);
    AlignedBuffer H ((size_t) n), h ((size_t) n), H2 ((size_t) n);
    H[0] = std::exp (logMag[0]);
    H[1] = std::exp (logMag[(size_t) n / 2]);

    for (int k = 1; k < n / 2; ++k)
    {
        const float m = std::exp (logMag[(size_t) k]);
        H[(size_t) (2 * k)] = m * std::cos (phase[(size_t) k]);
        H[(size_t) (2 * k + 1)] = m * std::sin (phase[(size_t) k]);
    }

    fft.inverse (H.data(), h.data());

    double early = 0.0, late = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double e = (double) h[(size_t) i] * h[(size_t) i];
        (i < n / 8 ? early : late) += e;
    }

    CHECK (early / (early + late) > 0.99, "min-phase energy in first 1/8: %f", early / (early + late));

    // Phase must be non-trivial for a non-flat magnitude.
    float maxPhase = 0.0f;

    for (float v : phase)
        maxPhase = std::max (maxPhase, std::abs (v));

    CHECK (maxPhase > 0.05f, "phase magnitude %f", maxPhase);
}

void testEnvelope()
{
    // A harmonic tone: the envelope must be smooth (no harmonic comb) yet follow the broad
    // shape (harmonic amplitudes fall at 6 dB/oct).
    const double fs = 48000.0;
    const int n = 1024;
    LogGrid grid;
    grid.prepare (n, fs, 24);
    GridSmoother smoother;
    smoother.prepare (grid.size(), 24.0f / 6.0f);

    RealFFT fft;
    fft.prepare (n);
    AlignedBuffer x ((size_t) n), X ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        double s = 0.0;

        for (int h = 1; h <= 40; ++h)
            s += std::sin (2.0 * pi * 220.0 * h * i / fs) / h;

        x[(size_t) i] = (float) (s * (0.5 - 0.5 * std::cos (2.0 * pi * i / n)));
    }

    fft.forward (x.data(), X.data());
    std::vector<float> power ((size_t) n / 2 + 1), env ((size_t) grid.size());

    for (int k = 1; k < n / 2; ++k)
        power[(size_t) k] = X[(size_t) (2 * k)] * X[(size_t) (2 * k)] + X[(size_t) (2 * k + 1)] * X[(size_t) (2 * k + 1)];

    // Same order as the engine: average power on the log grid, then convert to dB.
    std::vector<float> gp ((size_t) grid.size()), smoothed ((size_t) grid.size());
    grid.binsToGrid (power.data(), gp.data());
    smoother.process (gp.data(), smoothed.data());

    for (int g = 0; g < grid.size(); ++g)
        env[(size_t) g] = 10.0f * std::log10 (smoothed[(size_t) g] + 1e-12f);

    // Smoothness between neighbouring grid points (1/24 octave). Only where the 220 Hz
    // harmonics are closer together than the 1/3-octave kernel (from the ~7th harmonic up);
    // below that no 1/3-octave smoother can, or should, hide individual partials.
    float maxStep = 0.0f;

    for (int g = 1; g < grid.size(); ++g)
        if (grid.frequency (g) > 1500.0f && grid.frequency (g) < 8000.0f)
            maxStep = std::max (maxStep, std::abs (env[(size_t) g] - env[(size_t) g - 1]));

    std::printf ("    max envelope step %.2f dB per 1/24 octave\n", maxStep);
    CHECK (maxStep < 1.5f, "envelope step between grid points %.2f dB", maxStep);

    // Broad shape: harmonic amplitudes fall as 1/h, i.e. 6 dB/oct. With evenly spaced partials
    // the per-bin power density follows the same slope: ~18 dB from 880 Hz to 7040 Hz.
    const float drop = grid.valueAt (env.data(), 880.0f) - grid.valueAt (env.data(), 7040.0f);
    CHECK (drop > 15.0f && drop < 21.0f, "envelope drop over 3 octaves %.2f dB", drop);
}

/** Feeds the core with a signal and returns the converged display gain at a frequency. */
float convergedGainAt (const std::vector<float>& signal, double fs, const EngineParams& p, float hz,
                       std::vector<float>* curveOut = nullptr, const LogGrid** gridOut = nullptr)
{
    static SpectralCore core; // reused; prepare() resets everything
    core.prepare (fs, mapping::fftSizeFor (p.quality, fs), p.quality == mapping::Quality::lowLatency);

    std::vector<float> buf = signal;
    float* ptrs[1] = { buf.data() };
    core.process (ptrs, 1, nullptr, (int) buf.size(), p, false);

    if (curveOut != nullptr)
    {
        const auto& g = core.getGrid();
        curveOut->assign (core.getGainDb (0), core.getGainDb (0) + g.size());
    }

    if (gridOut != nullptr)
        *gridOut = &core.getGrid();

    return core.getGrid().valueAt (core.getGainDb (0), hz);
}

void testResonanceGain()
{
    const double fs = 48000.0;
    auto x = whiteNoise ((int) fs * 3, 0.1f);
    applyPeak (x, fs, 2000.0, 12.0, 2.0);

    EngineParams p;
    p.amount = 7.0f;

    std::vector<float> curve;
    const float atRes = convergedGainAt (x, fs, p, 2000.0f, &curve);
    const float atLow = convergedGainAt (x, fs, p, 250.0f);
    const float atHigh = convergedGainAt (x, fs, p, 12000.0f);

    std::printf ("    gain @2k %.2f dB, @250 %.2f dB, @12k %.2f dB\n", atRes, atLow, atHigh);
    CHECK (atRes < -3.0f, "resonance should be cut, got %.2f dB", atRes);
    CHECK (atRes < atLow - 2.0f && atRes < atHigh - 2.0f, "cut should be local");

    // Gain curve must be broad: no sharp features between adjacent 1/24-octave points.
    float maxStep = 0.0f;

    for (size_t i = 1; i < curve.size(); ++i)
        maxStep = std::max (maxStep, std::abs (curve[i] - curve[i - 1]));

    CHECK (maxStep < 1.0f, "gain curve step %.2f dB", maxStep);

    // Amount 0 = no processing.
    EngineParams off;
    off.amount = 0.0f;
    CHECK (std::abs (convergedGainAt (x, fs, off, 2000.0f)) < 0.01f, "amount 0 must be neutral");

    // Amount scaling is monotonic.
    EngineParams mid;
    mid.amount = 4.0f;
    const float atMid = convergedGainAt (x, fs, mid, 2000.0f);
    CHECK (atMid < 0.0f && atMid > atRes, "amount 4 cut %.2f should be between 0 and %.2f", atMid, atRes);
}

void testToneControls()
{
    const double fs = 48000.0;
    auto x = whiteNoise ((int) fs * 2, 0.1f);

    EngineParams p;
    p.amount = 7.0f;
    p.bands[0][0].level = 10.0f; // lows +12 dB

    const float low = convergedGainAt (x, fs, p, 100.0f);
    const float high = convergedGainAt (x, fs, p, 8000.0f);
    std::printf ("    lows +10: gain @100 %.2f dB, @8k %.2f dB\n", low, high);
    CHECK (low - high > 6.0f, "lows band should tilt the curve (%.2f vs %.2f)", low, high);

    p.bands[0][0].level = -10.0f;
    const float lowCut = convergedGainAt (x, fs, p, 100.0f);
    CHECK (lowCut < convergedGainAt (x, fs, p, 8000.0f) - 6.0f, "negative level should cut lows (%.2f)", lowCut);

    p.bands[0][0].level = 10.0f;
    p.bandOn[0] = false;
    CHECK (std::abs (convergedGainAt (x, fs, p, 100.0f) - convergedGainAt (x, fs, p, 8000.0f)) < 2.0f,
           "disabled band must not shape the curve");

    CHECK (std::abs (mapping::bandNormToHz (0, mapping::bandDefaultNorm (0)) - 100.0f) < 0.1f, "default lows centre");
    CHECK (std::abs (mapping::bandNormToHz (3, mapping::bandDefaultNorm (3)) - 6300.0f) < 1.0f, "default highs centre");
    CHECK (std::abs (mapping::bandLevelToDb (10.0f) - 12.0f) < 1e-4f && std::abs (mapping::bandLevelToDb (-5.0f) + 12.0f * std::pow (0.5f, 1.5f)) < 1e-4f,
           "level mapping");
}

void testTemporalSmoothing()
{
    // Coefficient maths: after tau seconds the follower reaches 63.2 % of a step.
    const float hopSeconds = 256.0f / 48000.0f;

    for (float param : { 1.0f, 3.0f, 6.0f })
    {
        const float tau = mapping::releaseSeconds (param);
        const float a = mapping::followerCoeff (tau, hopSeconds);
        const int steps = (int) std::round (tau / hopSeconds);
        float state = 0.0f;

        for (int i = 0; i < steps; ++i)
            state += a * (1.0f - state);

        if (steps >= 4)
            CHECK (std::abs (state - 0.632f) < 0.03f, "release %.1f: %.3f after tau", param, state);
    }

    CHECK (std::abs (mapping::attackSeconds (0.0f) - 0.001f) < 1e-6f, "attack min 1 ms");
    CHECK (std::abs (mapping::attackSeconds (10.0f) - 1.0f) < 0.02f, "attack 10 -> ~1 s");
    CHECK (std::abs (mapping::releaseSeconds (0.0f) - 0.005f) < 1e-6f, "release min 5 ms");

    // In the engine: a resonance switched on, then off. The cut must appear quickly (attack) and
    // recover slowly (release).
    const double fs = 48000.0;
    auto quiet = whiteNoise ((int) fs, 0.1f, 7);
    auto loud = quiet;
    applyPeak (loud, fs, 2000.0, 15.0, 2.0);

    SpectralCore core;
    core.prepare (fs, 1024, false);
    EngineParams p;
    p.amount = 7.0f;
    p.attack = 1.0f;  // ~2 ms
    p.release = 5.0f; // ~0.5 s

    auto runFor = [&] (std::vector<float> sig, int samples)
    {
        sig.resize ((size_t) samples);
        float* ptrs[1] = { sig.data() };
        core.process (ptrs, 1, nullptr, samples, p, false);
        return core.getGrid().valueAt (core.getGainDb (0), 2000.0f);
    };

    runFor (quiet, (int) fs);                     // settle on plain noise
    const float baseline = runFor (quiet, 4800);
    const float afterAttack = runFor (loud, 4800);       // 100 ms into the resonance
    const float settled = runFor (loud, (int) fs / 2);
    const float shortlyAfter = runFor (quiet, 4800);     // 100 ms after it stops
    const float later = runFor (quiet, (int) fs);        // ~1.1 s after

    std::printf ("    2k gain: base %.2f, +100ms %.2f, settled %.2f, release +100ms %.2f, +1.1s %.2f\n",
                 baseline, afterAttack, settled, shortlyAfter, later);
    CHECK (afterAttack < baseline - 0.8f * (baseline - settled), "fast attack should reach most of the cut");
    CHECK (shortlyAfter < settled + 0.5f * (baseline - settled), "slow release should still hold the cut");
    CHECK (std::abs (later - baseline) < 0.5f * std::abs (baseline - settled), "cut should release");
}

void testNoiseIntegration()
{
    // White noise with a broad resonance, amount 5, flat tone controls: output 1/3-octave
    // spectrum must be flatter than the input.
    const double fs = 48000.0;
    const int n = (int) fs * 4;
    auto x = whiteNoise (n, 0.1f, 3);
    applyPeak (x, fs, 1500.0, 9.0, 1.5);

    ToneShaperEngine engine;
    engine.prepare (fs, 512);
    EngineParams p;
    p.amount = 5.0f;

    auto out = runEngine (engine, { x }, p, 512)[0];
    const int skip = (int) fs; // let the followers settle
    const auto centres = standardCentres();
    const auto inLevels = thirdOctaveLevels (x.data() + skip, n - skip, fs, centres);
    const auto outLevels = thirdOctaveLevels (out.data() + skip, n - skip, fs, centres);

    const double sdIn = stddev (inLevels), sdOut = stddev (outLevels);
    std::printf ("    1/3-oct spread: in %.2f dB, out %.2f dB\n", sdIn, sdOut);
    CHECK (sdOut < sdIn * 0.8, "output should be flatter (%.2f vs %.2f)", sdOut, sdIn);

    // Level independence (amount < 7): overall loudness roughly preserved.
    const double dIn = rmsDb (x.data() + skip, n - skip), dOut = rmsDb (out.data() + skip, n - skip);
    CHECK (std::abs (dIn - dOut) < 3.0, "level change %.2f dB", dOut - dIn);

    // Plain white noise: already balanced, so it should be left (nearly) alone.
    auto white = whiteNoise (n, 0.1f, 11);
    ToneShaperEngine e2;
    e2.prepare (fs, 512);
    auto wout = runEngine (e2, { white }, p, 512)[0];
    const auto wIn = thirdOctaveLevels (white.data() + skip, n - skip, fs, centres);
    const auto wOut = thirdOctaveLevels (wout.data() + skip, n - skip, fs, centres);
    double maxDev = 0.0;

    for (size_t i = 0; i < wIn.size(); ++i)
        maxDev = std::max (maxDev, std::abs (wOut[i] - wIn[i]));

    std::printf ("    white noise max band change %.2f dB\n", maxDev);
    CHECK (maxDev < 3.0, "white noise changed by %.2f dB", maxDev);
}

void testLatencyAndNull()
{
    // Amount 0 must be transparent, delayed by exactly the reported latency, for every quality
    // mode and sample rate, with awkward host block sizes.
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        for (int q = 0; q < 3; ++q)
        {
            ToneShaperEngine engine;
            engine.prepare (fs, 1024);
            EngineParams p = neutralParams();
            p.quality = (mapping::Quality) q;

            const int latency = engine.getLatencySamples (p.quality);
            const int n = latency * 6 + 5000;
            auto x = whiteNoise (n, 0.25f, (uint32_t) (q + 1));
            auto out = runEngine (engine, { x, x }, p, 333);

            const int lag = argmaxCrossCorrelation (x, out[0], latency + 200);
            const double err = nullDb (x, out[0], latency, latency);

            CHECK (lag == latency, "fs %.0f q %d: correlation peak %d, reported %d", fs, q, lag, latency);
            CHECK (err < -80.0, "fs %.0f q %d: null %.1f dB", fs, q, err);
        }
    }

    ToneShaperEngine engine;
    engine.prepare (48000.0, 512);
    std::printf ("    latency @48k: normal %d, high %d, low-latency %d samples\n",
                 engine.getLatencySamples (mapping::Quality::normal),
                 engine.getLatencySamples (mapping::Quality::high),
                 engine.getLatencySamples (mapping::Quality::lowLatency));
    CHECK (engine.getFftSize (mapping::Quality::normal) == 1024, "normal FFT size");
    CHECK (engine.getFftSize (mapping::Quality::lowLatency) == 256, "low-latency FFT size");
    engine.prepare (96000.0, 512);
    CHECK (engine.getFftSize (mapping::Quality::high) == 4096, "high @96k doubles");
    CHECK (engine.getFftSize (mapping::Quality::lowLatency) == 512, "low-latency @96k");
    engine.prepare (192000.0, 512);
    CHECK (engine.getFftSize (mapping::Quality::lowLatency) == 1024, "low-latency @192k");
}

void testMixAndBypass()
{
    // With processing active, the dry path must stay time-aligned: 50 % mix of a processed
    // signal must not comb-filter. Checked by nulling: out - 0.5*dry_delayed = 0.5*wet.
    const double fs = 48000.0;
    const int n = 48000;
    auto x = whiteNoise (n, 0.2f, 5);

    ToneShaperEngine e1, e2;
    e1.prepare (fs, 256);
    e2.prepare (fs, 256);

    EngineParams wetOnly;
    wetOnly.amount = 6.0f;
    EngineParams half = wetOnly;
    half.mix = 0.5f;

    const auto wet = runEngine (e1, { x }, wetOnly, 256)[0];
    const auto mixed = runEngine (e2, { x }, half, 256)[0];
    const int latency = e1.getActiveLatencySamples();

    std::vector<float> expected ((size_t) n);

    for (int i = 0; i < n; ++i)
        expected[(size_t) i] = 0.5f * wet[(size_t) i] + 0.5f * (i >= latency ? x[(size_t) (i - latency)] : 0.0f);

    CHECK (nullDb (expected, mixed, 0, latency) < -80.0, "50%% mix mismatch %.1f dB", nullDb (expected, mixed, 0, latency));

    // Bypass: output is the latency-compensated input.
    ToneShaperEngine e3;
    e3.prepare (fs, 256);
    EngineParams bypassed = wetOnly;
    bypassed.bypass = true;
    const auto byp = runEngine (e3, { x }, bypassed, 256)[0];
    CHECK (nullDb (x, byp, latency, 0) < -100.0, "bypass should be clean");

    // Output gain and wet trim.
    ToneShaperEngine e4;
    e4.prepare (fs, 256);
    EngineParams gains = neutralParams();
    gains.outputDb = -6.0f;
    gains.wetTrimDb = -6.0f;
    const auto g = runEngine (e4, { x }, gains, 256)[0];
    const double d = rmsDb (g.data() + latency, n - latency) - rmsDb (x.data(), n - latency);
    CHECK (std::abs (d + 12.0) < 0.2, "trim + output should give -12 dB, got %.2f", d);
}

void testSquash()
{
    const double fs = 48000.0;
    const int n = (int) fs * 3;
    auto loud = whiteNoise (n, 0.9f, 9);
    applyPeak (loud, fs, 3000.0, 6.0, 1.0);

    EngineParams normal;
    normal.amount = 7.0f;
    EngineParams squash;
    squash.amount = 10.0f;

    ToneShaperEngine e1, e2;
    e1.prepare (fs, 512);
    e2.prepare (fs, 512);
    const auto a = runEngine (e1, { loud }, normal, 512)[0];
    const auto b = runEngine (e2, { loud }, squash, 512)[0];

    const int skip = (int) fs;
    const double la = rmsDb (a.data() + skip, n - skip), lb = rmsDb (b.data() + skip, n - skip);
    std::printf ("    loud noise: amount 7 %.2f dB, amount 10 %.2f dB, meter %.2f dB\n", la, lb, e2.getSquashMeterDb());
    CHECK (lb < la - 1.0, "squash should reduce loud material");
    CHECK (e2.getSquashMeterDb() > 1.0f, "squash meter %.2f", e2.getSquashMeterDb());
    CHECK (e1.getSquashMeterDb() < 0.01f, "no squash below amount 7");

    // Squash calibration: higher cal = lower threshold = more reduction.
    EngineParams cal = squash;
    cal.squashCal = 12.0f;
    ToneShaperEngine e3;
    e3.prepare (fs, 512);
    const auto c = runEngine (e3, { loud }, cal, 512)[0];
    CHECK (rmsDb (c.data() + skip, n - skip) < lb - 0.5, "higher cal should squash more");

    CHECK (mapping::squashAmount (7.0f) == 0.0f && mapping::squashAmount (10.0f) == 1.0f, "squash amount mapping");
}

void testStereoModes()
{
    const double fs = 48000.0;
    const int n = 48000;
    auto l = whiteNoise (n, 0.2f, 21);
    auto r = whiteNoise (n, 0.2f, 22);

    for (int mode = 0; mode < 3; ++mode)
    {
        ToneShaperEngine engine;
        engine.prepare (fs, 512);
        EngineParams p = neutralParams();
        p.stereoMode = (StereoMode) mode;
        const auto out = runEngine (engine, { l, r }, p, 512);
        const int lat = engine.getActiveLatencySamples();
        CHECK (nullDb (l, out[0], lat, lat) < -80.0 && nullDb (r, out[1], lat, lat) < -80.0,
               "stereo mode %d not transparent at amount 0", mode);
    }

    // Linked mode applies the same curve to both channels: identical inputs stay identical.
    {
        auto lr = l;
        applyPeak (lr, fs, 1000.0, 10.0, 2.0);
        ToneShaperEngine engine;
        engine.prepare (fs, 512);
        EngineParams p;
        p.amount = 7.0f;
        const auto out = runEngine (engine, { lr, lr }, p, 512);
        CHECK (nullDb (out[0], out[1], 0, 0) < -120.0, "linked: L and R should match");
    }

    // L/R split with focus fully on ch1: ch2 must pass through untouched.
    {
        auto lr = l;
        applyPeak (lr, fs, 1000.0, 10.0, 2.0);
        ToneShaperEngine engine;
        engine.prepare (fs, 512);
        EngineParams p;
        p.amount = 7.0f;
        p.stereoMode = StereoMode::leftRight;
        p.channelFocus = 0.0f;
        const auto out = runEngine (engine, { lr, lr }, p, 512);
        const int lat = engine.getActiveLatencySamples();
        CHECK (nullDb (lr, out[1], lat, lat) < -80.0, "focus 0: ch2 should be untouched");
        CHECK (nullDb (lr, out[0], lat, lat) > -40.0, "focus 0: ch1 should be processed");
    }

    // M/S: a mono (side-free) input stays mono.
    {
        auto m = l;
        applyPeak (m, fs, 1000.0, 10.0, 2.0);
        ToneShaperEngine engine;
        engine.prepare (fs, 512);
        EngineParams p;
        p.amount = 7.0f;
        p.stereoMode = StereoMode::midSide;
        p.channelLink = 0.0f;
        const auto out = runEngine (engine, { m, m }, p, 512);
        CHECK (nullDb (out[0], out[1], 0, 0) < -100.0, "M/S: mono input should stay mono");
    }
}

void testSidechain()
{
    // The detector listens to the sidechain: a resonance in the sidechain causes a cut in a
    // flat main signal.
    const double fs = 48000.0;
    const int n = (int) fs * 3;
    auto main = whiteNoise (n, 0.1f, 31);
    auto sc = whiteNoise (n, 0.1f, 32);
    applyPeak (sc, fs, 2000.0, 15.0, 2.0);

    EngineParams p;
    p.amount = 7.0f;
    p.scEnabled = true;

    ToneShaperEngine engine;
    engine.prepare (fs, 512);
    std::vector<std::vector<float>> side { sc };
    const auto out = runEngine (engine, { main }, p, 512, &side)[0];

    const int skip = (int) fs;
    const std::vector<double> at { 2000.0, 250.0 };
    const auto inL = thirdOctaveLevels (main.data() + skip, n - skip, fs, at);
    const auto outL = thirdOctaveLevels (out.data() + skip, n - skip, fs, at);
    const double cut2k = outL[0] - inL[0], cut250 = outL[1] - inL[1];
    std::printf ("    sidechain-driven change: 2k %.2f dB, 250 Hz %.2f dB\n", cut2k, cut250);
    CHECK (cut2k < cut250 - 3.0, "sidechain resonance should cut the main signal at 2 kHz");

    // sc listen: output is the (delayed) sidechain.
    p.scListen = true;
    ToneShaperEngine e2;
    e2.prepare (fs, 512);
    const auto listen = runEngine (e2, { main }, p, 512, &side)[0];
    const int lat = e2.getActiveLatencySamples();
    CHECK (nullDb (sc, listen, lat, 0) < -100.0, "sc listen should output the sidechain");
}

void testRangeMask()
{
    const double fs = 48000.0;
    auto x = whiteNoise ((int) fs * 2, 0.1f);
    applyPeak (x, fs, 3000.0, 12.0, 2.0);

    EngineParams p;
    p.amount = 7.0f;
    const float full = convergedGainAt (x, fs, p, 3000.0f);

    p.freqLimLo = 100.0f;
    p.freqLimHi = 800.0f;
    const float excluded = convergedGainAt (x, fs, p, 3000.0f);

    p.freqLimFlip = true; // exclude mode: process outside 100-800 Hz
    const float flipped = convergedGainAt (x, fs, p, 3000.0f);

    std::printf ("    3k gain: full range %.2f, range 100-800 %.2f, flipped %.2f\n", full, excluded, flipped);
    CHECK (std::abs (excluded) < 0.25f * std::abs (full), "outside range should be mostly untouched");
    CHECK (std::abs (flipped - full) < 0.25f * std::abs (full), "exclude mode should process 3 kHz");
}

void testLowLatencyMinPhase()
{
    const double fs = 48000.0;
    auto x = whiteNoise ((int) fs * 2, 0.1f);
    applyPeak (x, fs, 2000.0, 12.0, 2.0);
    EngineParams p;
    p.amount = 7.0f;
    p.quality = mapping::Quality::lowLatency;
    const float g = convergedGainAt (x, fs, p, 2000.0f);
    CHECK (g < -2.0f, "low latency mode should also cut the resonance (%.2f)", g);

    p.quality = mapping::Quality::high;
    CHECK (convergedGainAt (x, fs, p, 2000.0f) < -3.0f, "high quality cut");
}

void testFilters()
{
    const double fs = 48000.0;

    auto responseDb = [&] (Butterworth3::Type t, float fc, double f)
    {
        Butterworth3 filter;
        filter.setup (t, fs, fc);
        const int n = 48000;
        double sIn = 0.0, sOut = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const float x = (float) std::sin (2.0 * pi * f * i / fs);
            const float y = filter.process (x);

            if (i > n / 2)
            {
                sIn += (double) x * x;
                sOut += (double) y * y;
            }
        }

        return 10.0 * std::log10 (sOut / sIn);
    };

    const double atCut = responseDb (Butterworth3::Type::highPass, 200.0f, 200.0);
    const double octaveBelow = responseDb (Butterworth3::Type::highPass, 200.0f, 50.0);
    const double pass = responseDb (Butterworth3::Type::highPass, 200.0f, 2000.0);
    CHECK (std::abs (atCut + 3.01) < 0.2, "HP -3 dB at cutoff: %.2f", atCut);
    CHECK (std::abs (octaveBelow + 36.0) < 1.5, "HP 2 octaves below ~ -36 dB: %.2f", octaveBelow);
    CHECK (std::abs (pass) < 0.1, "HP passband %.2f", pass);

    const double lpCut = responseDb (Butterworth3::Type::lowPass, 5000.0f, 5000.0);
    CHECK (std::abs (lpCut + 3.01) < 0.2, "LP -3 dB at cutoff: %.2f", lpCut);
}

void testLoudness()
{
    const double fs = 48000.0;
    ShortTermLoudness meter;
    meter.prepare (fs);
    std::vector<float> s ((size_t) fs * 4);

    for (size_t i = 0; i < s.size(); ++i)
        s[i] = 0.1f * (float) std::sin (2.0 * pi * 1000.0 * (double) i / fs);

    const float* ch[2] = { s.data(), s.data() };
    meter.process (ch, 2, (int) s.size());
    const float lufs = meter.getLufs();
    std::printf ("    1 kHz sine at -20 dBFS (stereo): %.2f LUFS\n", lufs);
    CHECK (std::abs (lufs + 20.0f) < 0.3f, "expected ~ -20 LUFS, got %.2f", lufs);
}

void testRobustness()
{
    // Silence, DC, full-scale square, every sample rate: no NaN / Inf, bounded output.
    for (double fs : { 44100.0, 96000.0, 192000.0 })
    {
        ToneShaperEngine engine;
        engine.prepare (fs, 2048);
        EngineParams p;
        p.amount = 10.0f;
        p.squashCal = 24.0f;
        p.stereoMode = StereoMode::midSide;
        p.loCut = 80.0f;
        p.hiCut = 12000.0f;

        const int n = (int) fs;
        std::vector<float> l ((size_t) n), r ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            l[(size_t) i] = i < n / 3 ? 0.0f : (i < 2 * n / 3 ? 0.5f : ((i / 50) % 2 ? 1.0f : -1.0f));
            r[(size_t) i] = -l[(size_t) i];
        }

        const auto out = runEngine (engine, { l, r }, p, 1777);
        bool finite = true;
        float peak = 0.0f;

        for (const auto& ch : out)
            for (float v : ch)
            {
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
            }

        CHECK (finite, "fs %.0f: non-finite output", fs);
        CHECK (peak < 16.0f, "fs %.0f: output peak %.2f", fs, peak);
    }

    // Quality switching mid-stream.
    ToneShaperEngine engine;
    engine.prepare (48000.0, 512);
    EngineParams p;
    auto x = whiteNoise (48000, 0.2f);

    for (int q = 0; q < 6; ++q)
    {
        p.quality = (mapping::Quality) (q % 3);
        auto out = runEngine (engine, { x }, p, 512)[0];

        for (float v : out)
            if (! std::isfinite (v))
            {
                CHECK (false, "non-finite after quality switch");
                break;
            }
    }
}

void benchmark()
{
    const double fs = 48000.0;
    const int seconds = 10;
    auto l = whiteNoise ((int) fs * seconds, 0.2f, 41);
    auto r = whiteNoise ((int) fs * seconds, 0.2f, 42);

    for (int q = 0; q < 3; ++q)
    {
        for (float amount : { 5.0f, 9.0f })
        {
            ToneShaperEngine engine;
            engine.prepare (fs, 512);
            EngineParams p;
            p.amount = amount;
            p.quality = (mapping::Quality) q;
            p.stereoMode = StereoMode::leftRight;

            const auto t0 = std::chrono::steady_clock::now();
            runEngine (engine, { l, r }, p, 512);
            const auto t1 = std::chrono::steady_clock::now();
            const double secs = std::chrono::duration<double> (t1 - t0).count();
            const char* names[] = { "normal", "high", "low-latency" };
            std::printf ("    %-11s amount %.0f, stereo split @48k: %.2f%% of one core\n",
                         names[q], amount, 100.0 * secs / seconds);
        }
    }
}

} // namespace

int main()
{
    struct Test
    {
        const char* name;
        std::function<void()> fn;
    };

    const Test tests[] = {
        { "FFT ordering and round trip", testFft },
        { "Minimum-phase reconstruction", testMinPhase },
        { "Spectral envelope", testEnvelope },
        { "Resonance produces a local cut", testResonanceGain },
        { "Tone controls", testToneControls },
        { "Temporal smoothing", testTemporalSmoothing },
        { "Noise integration", testNoiseIntegration },
        { "Latency and null test", testLatencyAndNull },
        { "Mix and bypass alignment", testMixAndBypass },
        { "Squash", testSquash },
        { "Stereo modes", testStereoModes },
        { "Sidechain", testSidechain },
        { "Processing range mask", testRangeMask },
        { "Low latency / high quality", testLowLatencyMinPhase },
        { "Output filters", testFilters },
        { "Loudness meter", testLoudness },
        { "Robustness", testRobustness },
        { "Benchmark", benchmark },
    };

    for (const auto& t : tests)
    {
        const int before = failures;
        std::printf ("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf ("[ %s ] %s\n", failures == before ? " OK " : "FAIL", t.name);
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
