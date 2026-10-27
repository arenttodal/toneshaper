#pragma once

#include <algorithm>
#include <array>
#include <cmath>

/** Pure parameter-mapping and weighting functions shared by the DSP engine and the UI. */
namespace toneshaper::mapping
{

constexpr int numBands = 4;

struct BandRange
{
    float minHz, maxHz, defaultHz;
    const char* name;
};

// Section 4.1: tone band ranges and default centres.
constexpr std::array<BandRange, numBands> bandRanges { {
    { 27.0f,   750.0f,   100.0f,  "Lows" },
    { 63.0f,   2500.0f,  350.0f,  "Lo-mids" },
    { 350.0f,  20000.0f, 3500.0f, "Hi-mids" },
    { 1000.0f, 20000.0f, 6300.0f, "Highs" },
} };

/** Section 4.5: normalised 0..1 -> Hz on a logarithmic curve. */
inline float bandNormToHz (int band, float p) noexcept
{
    const auto& r = bandRanges[(size_t) band];
    return r.minHz * std::pow (r.maxHz / r.minHz, std::clamp (p, 0.0f, 1.0f));
}

inline float bandHzToNorm (int band, float hz) noexcept
{
    const auto& r = bandRanges[(size_t) band];
    return std::clamp (std::log (hz / r.minHz) / std::log (r.maxHz / r.minHz), 0.0f, 1.0f);
}

/** Normalised value that lands exactly on the band's default centre frequency. */
inline float bandDefaultNorm (int band) noexcept
{
    return bandHzToNorm (band, bandRanges[(size_t) band].defaultHz);
}

constexpr float maxBandGainDb = 12.0f;

/** Section 4.6: level (-10..+10, not dB) -> dB through a 1.5 power curve. */
inline float bandLevelToDb (float level) noexcept
{
    const float l = std::clamp (level, -10.0f, 10.0f);
    const float mag = std::pow (std::abs (l) / 10.0f, 1.5f) * maxBandGainDb;
    return l < 0.0f ? -mag : mag;
}

/** Section 4.4: amount (0..10) -> correction scale. Quadratic below 3, linear up to 7,
    then held at 1.0 while the squash stage takes over. */
inline float amountScale (float amount) noexcept
{
    const float a = std::clamp (amount, 0.0f, 10.0f);

    if (a <= 3.0f)
    {
        const float x = a / 3.0f;
        return x * x * (3.0f / 7.0f);
    }

    return std::min (a / 7.0f, 1.0f);
}

/** Section 7.1: 0 at amount 7, 1 at amount 10. */
inline float squashAmount (float amount) noexcept
{
    return std::clamp ((amount - 7.0f) / 3.0f, 0.0f, 1.0f);
}

/** Section 6.2: attack parameter -> time constant in seconds (~1 ms .. ~1 s). */
inline float attackSeconds (float param) noexcept
{
    return 0.001f * std::pow (10.0f, param / 3.33f);
}

/** Section 6.2: release parameter -> time constant in seconds (~5 ms .. ~8 s). */
inline float releaseSeconds (float param) noexcept
{
    return 0.005f * std::pow (10.0f, param / 2.5f);
}

/** Exponential follower coefficient for a time constant, updated every hop. */
inline float followerCoeff (float timeConstantSeconds, float hopSeconds) noexcept
{
    return 1.0f - std::exp (-hopSeconds / std::max (timeConstantSeconds, 1.0e-6f));
}

/** Section 6.4: frequency-dependent time scaling (bass slower, treble faster). */
inline float timeScaleForFrequency (float hz) noexcept
{
    const float fs = std::clamp (std::log2 (std::max (hz, 1.0f) / 1000.0f) * 0.3f, -0.5f, 0.5f);
    return std::pow (2.0f, -fs);
}

enum class Quality
{
    normal = 0,
    high = 1,
    lowLatency = 2
};

/** Section 2: FFT size per quality mode, doubled per sample-rate octave above 48 kHz. */
inline int fftSizeFor (Quality q, double sampleRate) noexcept
{
    int base = 1024;

    switch (q)
    {
        case Quality::normal:     base = 1024; break;
        case Quality::high:       base = 2048; break;
        case Quality::lowLatency: base = 256;  break;
    }

    int factor = 1;

    if (sampleRate > 100000.0)      factor = 4;
    else if (sampleRate > 50000.0)  factor = 2;

    return base * factor;
}

/** Plugin latency for a quality mode: one full FFT frame (see SpectralCore::getLatency()). */
inline int latencySamplesFor (Quality q, double sampleRate) noexcept
{
    return fftSizeFor (q, sampleRate);
}

/** IEC 61672 A-weighting in dB. */
inline float aWeightingDb (float f) noexcept
{
    const double f2 = (double) f * (double) f;
    const double num = 12194.0 * 12194.0 * f2 * f2;
    const double den = (f2 + 20.6 * 20.6)
                     * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9))
                     * (f2 + 12194.0 * 12194.0);

    if (den <= 0.0 || num <= 0.0)
        return -200.0f;

    return (float) (20.0 * std::log10 (num / den) + 2.0);
}

/** Perceptual weight used for auto-compensation and gain normalisation (sections 4.3, 5.4):
    A-weighting applied as an amplitude factor on a log-frequency (pink) axis, limited to the
    audible band. */
inline float perceptualWeight (float f) noexcept
{
    if (f < 20.0f || f > 20000.0f)
        return 0.0f;

    return std::pow (10.0f, aWeightingDb (f) / 20.0f);
}

/** Section 7.2: approximate equal-loudness sensitivity, 0..1, interpolated on log frequency. */
inline float loudnessSensitivity (float f) noexcept
{
    static constexpr std::array<float, 10> freqs { 32.0f, 63.0f, 125.0f, 250.0f, 500.0f,
                                                   1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    static constexpr std::array<float, 10> sens { 0.05f, 0.15f, 0.30f, 0.50f, 0.75f,
                                                  0.85f, 0.95f, 1.00f, 0.80f, 0.30f };

    if (f <= freqs.front()) return sens.front();
    if (f >= freqs.back())  return sens.back();

    const float lf = std::log2 (f);

    for (size_t i = 1; i < freqs.size(); ++i)
    {
        if (f <= freqs[i])
        {
            const float l0 = std::log2 (freqs[i - 1]);
            const float l1 = std::log2 (freqs[i]);
            const float t = (lf - l0) / (l1 - l0);
            return sens[i - 1] + t * (sens[i] - sens[i - 1]);
        }
    }

    return sens.back();
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-10f)); }

} // namespace toneshaper::mapping
