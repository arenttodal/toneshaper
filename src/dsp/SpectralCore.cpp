#include "SpectralCore.h"

#include <algorithm>
#include <cmath>

namespace toneshaper
{

namespace
{
    constexpr int gridPointsPerOctave = 24;

    // Smoothing widths as Gaussian sigma, in octaves.
    constexpr float envelopeSigmaOct = 1.0f / 6.0f; // 1/3-octave kernel half-width (section 3.2)
    constexpr float gainSigmaOct = 0.4f;            // ~0.8-octave kernel half-width (section 5.3)
    constexpr float trendSigmaOct = 1.25f;          // "local trend" the envelope is compared against
    constexpr float heavySigmaOct = 1.25f;          // smooth/detail split for mixed phase (section 8.3)

    constexpr float toneBandSigmaOct = 1.75f;       // section 4.2
    constexpr float soloSigmaOct = 0.5f;

    constexpr float maxCutDb = 24.0f;               // section 5.2
    constexpr float maxBoostDb = 12.0f;
    constexpr float totalMinDb = -30.0f;
    constexpr float totalMaxDb = 18.0f;

    // Short symmetric power averaging ahead of the envelope. A single STFT bin's power is
    // exponentially distributed (~5.6 dB spread); without averaging, the fast-attack /
    // slow-release follower would latch onto the downward fluctuations and cut bass bins,
    // where few bins fall under the smoothing kernel.
    constexpr float detectorSeconds = 0.02f;

    constexpr float silenceDb = -110.0f;            // per-bin power below which a frame counts as silent
    constexpr float floorBelowMeanDb = 45.0f;       // dips deeper than this are noise, not tone

    constexpr float ln10Over20 = 0.11512925464970229f;

    inline float fastDbToGain (float db) noexcept { return std::exp (db * ln10Over20); }
}

void SpectralCore::prepare (double sr, int fftSize, bool minimumPhaseOnly)
{
    sampleRate = sr;
    n = fftSize;
    hop = fftSize / 4;
    bins = fftSize / 2 + 1;
    minPhaseOnly = minimumPhaseOnly;

    fft.prepare (n);
    minPhase.prepare (n);
    grid.prepare (n, sr, gridPointsPerOctave);
    gridSize = grid.size();

    const auto ppo = (float) gridPointsPerOctave;
    envelopeSmoother.prepare (gridSize, envelopeSigmaOct * ppo);
    trendSmoother.prepare (gridSize, trendSigmaOct * ppo, GridSmoother::Edges::pointReflect);
    gainSmoother.prepare (gridSize, gainSigmaOct * ppo);
    heavySmoother.prepare (gridSize, heavySigmaOct * ppo, GridSmoother::Edges::pointReflect);

    // Periodic Hann for analysis and synthesis: Hann^2 at 75 % overlap sums to exactly 1.5,
    // so the overlap-add is perfectly flat (COLA). The symmetric (N-1) form is not.
    window.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * 3.14159265358979f * (float) i / (float) n));

    outputScale = 1.0f / ((float) n * 1.5f);

    // Scales |X|^2 so that a full-scale sine sums to 1.0 (0 dB) across its window main lobe.
    powerScale = 32.0f / (3.0f * (float) n * (float) n);

    frame.resize ((size_t) n);

    for (int c = 0; c < maxChannels; ++c)
    {
        inRing[(size_t) c].assign ((size_t) n, 0.0f);
        analysisRing[(size_t) c].assign ((size_t) n, 0.0f);
        outRing[(size_t) c].assign ((size_t) n, 0.0f);
        spectrum[(size_t) c].resize ((size_t) n);
        analysisSpectrum[(size_t) c].resize ((size_t) n);
        power[(size_t) c].assign ((size_t) bins, 0.0f);

        auto& cv = curves[(size_t) c];
        for (auto* v : { &cv.toneDb, &cv.detector, &cv.follow, &cv.squashFollow, &cv.envelopeDb, &cv.displayGainDb, &cv.targetDb })
            v->assign ((size_t) gridSize, 0.0f);

        cv.binGain.assign ((size_t) bins, 1.0f);
        cv.binPhase.assign ((size_t) bins, 0.0f);
        cv.hasPhase = false;
    }

    for (auto* v : { &perceptualWeight, &sensitivity, &bandBins, &timeScale, &detectorCoeff, &attackCoeff, &releaseCoeff,
                     &mask, &soloShapeDb, &gPower, &gEnv, &gClamped, &gTrend, &gRaw, &gSmooth,
                     &gSquash, &gTmp, &gFinal, &gDetail })
        v->assign ((size_t) gridSize, 0.0f);

    combinedPower.assign ((size_t) bins, 0.0f);
    binDb.assign ((size_t) bins, 0.0f);
    binLogMag.assign ((size_t) bins, 0.0f);

    const float thirdOctaveWidth = std::exp2 (1.0f / 6.0f) - std::exp2 (-1.0f / 6.0f);

    for (int g = 0; g < gridSize; ++g)
    {
        const float f = grid.frequency (g);
        perceptualWeight[(size_t) g] = mapping::perceptualWeight (f);
        sensitivity[(size_t) g] = mapping::loudnessSensitivity (f);
        bandBins[(size_t) g] = std::max (1.0f, thirdOctaveWidth * f / grid.binWidth());
        timeScale[(size_t) g] = mapping::timeScaleForFrequency (f);
        detectorCoeff[(size_t) g] = mapping::followerCoeff (detectorSeconds * timeScale[(size_t) g],
                                                            (float) hop / (float) sr);
    }

    cache.valid = false;
    reset();
}

void SpectralCore::reset()
{
    for (int c = 0; c < maxChannels; ++c)
    {
        std::fill (inRing[(size_t) c].begin(), inRing[(size_t) c].end(), 0.0f);
        std::fill (analysisRing[(size_t) c].begin(), analysisRing[(size_t) c].end(), 0.0f);
        std::fill (outRing[(size_t) c].begin(), outRing[(size_t) c].end(), 0.0f);

        auto& cv = curves[(size_t) c];
        std::fill (cv.follow.begin(), cv.follow.end(), 0.0f);
        std::fill (cv.detector.begin(), cv.detector.end(), 0.0f);
        cv.detectorPrimed = false;
        std::fill (cv.squashFollow.begin(), cv.squashFollow.end(), 0.0f);
        std::fill (cv.displayGainDb.begin(), cv.displayGainDb.end(), 0.0f);
        std::fill (cv.envelopeDb.begin(), cv.envelopeDb.end(), -120.0f);
        std::fill (cv.binGain.begin(), cv.binGain.end(), 1.0f);
        std::fill (cv.binPhase.begin(), cv.binPhase.end(), 0.0f);
        cv.hasPhase = false;
    }

    ringPos = 0;
    hopCount = 0;
    squashMeterDb = 0.0f;
    activeCurves = 1;
}

void SpectralCore::computeToneTarget (int curve, const EngineParams& p) noexcept
{
    auto& tone = curves[(size_t) curve].toneDb;
    const auto& bands = p.bands[(size_t) curve];
    const float* l2f = grid.log2Frequencies();

    std::fill (tone.begin(), tone.end(), 0.0f);

    for (int b = 0; b < mapping::numBands; ++b)
    {
        if (! p.bandOn[(size_t) b])
            continue;

        const float levelDb = mapping::bandLevelToDb (bands[(size_t) b].level);

        if (levelDb == 0.0f)
            continue;

        const float centre = std::log2 (mapping::bandNormToHz (b, bands[(size_t) b].freqNorm));
        const float inv2s2 = 1.0f / (2.0f * toneBandSigmaOct * toneBandSigmaOct);

        for (int g = 0; g < gridSize; ++g)
        {
            const float d = l2f[g] - centre;
            tone[(size_t) g] += levelDb * std::exp (-d * d * inv2s2);
        }
    }

    // Auto-compensation: remove the perceptually weighted mean (section 4.3).
    const float mean = grid.weightedMean (tone.data(), perceptualWeight.data());

    for (auto& t : tone)
        t -= mean;
}

void SpectralCore::updateCaches (const EngineParams& p) noexcept
{
    auto bandsEqual = [] (const auto& a, const auto& b)
    {
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].freqNorm != b[i].freqNorm || a[i].level != b[i].level)
                return false;

        return true;
    };

    const bool toneChanged = ! cache.valid || cache.bandOn != p.bandOn
                             || ! bandsEqual (cache.bands[0], p.bands[0])
                             || ! bandsEqual (cache.bands[1], p.bands[1]);

    if (toneChanged)
    {
        computeToneTarget (0, p);
        computeToneTarget (1, p);
        cache.bandOn = p.bandOn;
        cache.bands = p.bands;
    }

    if (! cache.valid || cache.attack != p.attack || cache.release != p.release)
    {
        const float hopSeconds = (float) hop / (float) sampleRate;
        const float att = mapping::attackSeconds (p.attack);
        const float rel = mapping::releaseSeconds (p.release);

        for (int g = 0; g < gridSize; ++g)
        {
            attackCoeff[(size_t) g] = mapping::followerCoeff (att * timeScale[(size_t) g], hopSeconds);
            releaseCoeff[(size_t) g] = mapping::followerCoeff (rel * timeScale[(size_t) g], hopSeconds);
        }

        cache.attack = p.attack;
        cache.release = p.release;
    }

    if (! cache.valid || cache.freqLimLo != p.freqLimLo || cache.freqLimHi != p.freqLimHi
        || cache.freqLimFlip != p.freqLimFlip)
    {
        // Section 9.1: sloped processing-range mask, steepening as the range narrows.
        const float lo = std::max (1.0f, std::min (p.freqLimLo, p.freqLimHi));
        const float hi = std::max (lo * 1.0001f, std::max (p.freqLimLo, p.freqLimHi));
        const float rangeOct = std::log2 (hi / lo);
        const float slope = 12.0f * std::clamp (1.0f + (2.0f - rangeOct) / 2.0f, 1.0f, 2.0f);

        for (int g = 0; g < gridSize; ++g)
        {
            const float f = grid.frequency (g);
            const float mLo = f < lo ? mapping::dbToGain (-slope * std::log2 (lo / f)) : 1.0f;
            const float mHi = f > hi ? mapping::dbToGain (-slope * std::log2 (f / hi)) : 1.0f;
            const float m = mLo * mHi;
            mask[(size_t) g] = p.freqLimFlip ? 1.0f - m : m;
        }

        cache.freqLimLo = p.freqLimLo;
        cache.freqLimHi = p.freqLimHi;
        cache.freqLimFlip = p.freqLimFlip;
    }

    const int soloCh = std::clamp (p.bandSoloCh, 0, 1);
    const int soloBand = std::clamp (p.bandSoloBand, 0, mapping::numBands - 1);
    const float soloFreq = mapping::bandNormToHz (soloBand, p.bands[(size_t) soloCh][(size_t) soloBand].freqNorm);

    if (p.bandSolo && (! cache.valid || ! cache.bandSolo || cache.soloFreq != soloFreq))
    {
        const float centre = std::log2 (soloFreq);
        const float k = 20.0f * std::log10 (std::exp (1.0f)) / (2.0f * soloSigmaOct * soloSigmaOct);

        for (int g = 0; g < gridSize; ++g)
        {
            const float d = grid.log2Frequencies()[g] - centre;
            soloShapeDb[(size_t) g] = std::max (-80.0f, -k * d * d);
        }
    }

    cache.bandSolo = p.bandSolo;
    cache.bandSoloCh = soloCh;
    cache.bandSoloBand = soloBand;
    cache.soloFreq = soloFreq;
    cache.valid = true;
}

void SpectralCore::process (float* const* channels, int numChannels, const float* const* analysis,
                            int numSamples, const EngineParams& p, bool splitCurves) noexcept
{
    numChannels = std::clamp (numChannels, 1, maxChannels);
    const bool split = splitCurves && numChannels == 2;

    if (split && activeCurves == 1)
    {
        // Entering a split mode: start the second curve from the linked state.
        curves[1].follow = curves[0].follow;
        curves[1].squashFollow = curves[0].squashFollow;
    }

    updateCaches (p);

    int pos = 0;

    while (pos < numSamples)
    {
        const int todo = std::min (numSamples - pos, hop - hopCount);

        for (int c = 0; c < numChannels; ++c)
        {
            float* x = channels[c] + pos;
            const float* a = analysis != nullptr ? analysis[c] + pos : nullptr;
            float* in = inRing[(size_t) c].data();
            float* ain = analysisRing[(size_t) c].data();
            float* out = outRing[(size_t) c].data();
            int rp = ringPos;

            for (int i = 0; i < todo; ++i)
            {
                in[rp] = x[i];

                if (a != nullptr)
                    ain[rp] = a[i];

                x[i] = out[rp];
                out[rp] = 0.0f;

                if (++rp == n)
                    rp = 0;
            }
        }

        ringPos = (ringPos + todo) % n;
        hopCount += todo;
        pos += todo;

        if (hopCount == hop)
        {
            hopCount = 0;
            processFrame (numChannels, analysis != nullptr, p, split);
        }
    }
}

void SpectralCore::computePowerSpectrum (const float* spec, float* pw) const noexcept
{
    const int half = n / 2;
    pw[0] = spec[0] * spec[0] * powerScale;
    pw[half] = spec[1] * spec[1] * powerScale;

    for (int k = 1; k < half; ++k)
    {
        const float re = spec[2 * k], im = spec[2 * k + 1];
        pw[k] = (re * re + im * im) * powerScale;
    }
}

void SpectralCore::processFrame (int numChannels, bool externalAnalysis, const EngineParams& p, bool split) noexcept
{
    const float* w = window.data();
    const int firstPart = n - ringPos;

    auto windowedForward = [&] (const std::vector<float>& ring, AlignedBuffer& dest)
    {
        // Unwrap the ring (oldest sample first) while applying the analysis window.
        float* f = frame.data();

        for (int i = 0; i < firstPart; ++i)
            f[i] = ring[(size_t) (ringPos + i)] * w[i];

        for (int i = firstPart; i < n; ++i)
            f[i] = ring[(size_t) (i - firstPart)] * w[i];

        fft.forward (f, dest.data());
    };

    for (int c = 0; c < numChannels; ++c)
    {
        windowedForward (inRing[(size_t) c], spectrum[(size_t) c]);

        if (externalAnalysis)
            windowedForward (analysisRing[(size_t) c], analysisSpectrum[(size_t) c]);

        computePowerSpectrum (externalAnalysis ? analysisSpectrum[(size_t) c].data() : spectrum[(size_t) c].data(),
                              power[(size_t) c].data());
    }

    squashMeterDb = 0.0f;

    if (split)
    {
        // Section 9.6: channel link blends each channel's detector towards the average.
        const float link = std::clamp (p.channelLink, 0.0f, 1.0f);
        auto& p0 = power[0];
        auto& p1 = power[1];

        for (int k = 0; k < bins; ++k)
        {
            const float avg = 0.5f * (p0[(size_t) k] + p1[(size_t) k]);
            p0[(size_t) k] += link * (avg - p0[(size_t) k]);
            p1[(size_t) k] += link * (avg - p1[(size_t) k]);
        }

        const float focus = std::clamp (p.channelFocus, 0.0f, 1.0f);
        computeCurve (0, p0.data(), p, std::min (1.0f, 2.0f * (1.0f - focus)));
        computeCurve (1, p1.data(), p, std::min (1.0f, 2.0f * focus));
        activeCurves = 2;
    }
    else
    {
        const float inv = 1.0f / (float) numChannels;

        for (int k = 0; k < bins; ++k)
        {
            float s = 0.0f;

            for (int c = 0; c < numChannels; ++c)
                s += power[(size_t) c][(size_t) k];

            combinedPower[(size_t) k] = s * inv;
        }

        computeCurve (0, combinedPower.data(), p, 1.0f);
        activeCurves = 1;
    }

    const int half = n / 2;

    for (int c = 0; c < numChannels; ++c)
    {
        const auto& cv = curves[(size_t) (split ? c : 0)];
        float* s = spectrum[(size_t) c].data();
        const float* g = cv.binGain.data();
        const float* ph = cv.binPhase.data();

        s[0] *= g[0];
        s[1] *= g[half];

        if (cv.hasPhase)
        {
            for (int k = 1; k < half; ++k)
            {
                const float hr = g[k] * std::cos (ph[k]);
                const float hi = g[k] * std::sin (ph[k]);
                const float re = s[2 * k], im = s[2 * k + 1];
                s[2 * k] = re * hr - im * hi;
                s[2 * k + 1] = re * hi + im * hr;
            }
        }
        else
        {
            for (int k = 1; k < half; ++k)
            {
                s[2 * k] *= g[k];
                s[2 * k + 1] *= g[k];
            }
        }

        float* f = frame.data();
        fft.inverse (s, f);

        // Synthesis window + overlap-add, written so the frame's first sample lands one full
        // FFT length after it entered (see getLatency()).
        float* out = outRing[(size_t) c].data();
        const float scale = outputScale;

        for (int i = 0; i < firstPart; ++i)
            out[ringPos + i] += f[i] * w[i] * scale;

        for (int i = firstPart; i < n; ++i)
            out[i - firstPart] += f[i] * w[i] * scale;
    }

    newFrame = true;
}

void SpectralCore::computeCurve (int curve, const float* pw, const EngineParams& p, float focusDepth) noexcept
{
    auto& cv = curves[(size_t) curve];
    const int G = gridSize;

    // --- Section 3: spectral envelope ---------------------------------------------------------
    // Smoothing is done on power and converted to dB afterwards: averaging dB values of
    // exponentially distributed bin powers is biased low by ~2.5 dB wherever few bins are
    // averaged (i.e. at low frequencies), which would tilt the correction.
    grid.binsToGrid (pw, gPower.data());

    if (! cv.detectorPrimed)
    {
        std::copy (gPower.begin(), gPower.end(), cv.detector.begin());
        cv.detectorPrimed = true;
    }

    for (int g = 0; g < G; ++g)
        cv.detector[(size_t) g] += detectorCoeff[(size_t) g] * (gPower[(size_t) g] - cv.detector[(size_t) g]);

    envelopeSmoother.process (cv.detector.data(), gPower.data()); // smoothed power density per bin

    float maxEnv = -200.0f;

    for (int g = 0; g < G; ++g)
    {
        gEnv[(size_t) g] = 10.0f * std::log10 (gPower[(size_t) g] + 1.0e-16f);
        maxEnv = std::max (maxEnv, gEnv[(size_t) g]);
        cv.envelopeDb[(size_t) g] = gEnv[(size_t) g] + 10.0f * std::log10 (bandBins[(size_t) g]);
    }

    const bool silent = maxEnv < silenceDb;
    const float meanEnv = grid.weightedMean (gEnv.data(), perceptualWeight.data());
    const float floorDb = std::max (meanEnv - floorBelowMeanDb, -120.0f);

    // Dips far below the programme level are noise floor, not tone: don't chase them.
    for (int g = 0; g < G; ++g)
        gClamped[(size_t) g] = std::max (gEnv[(size_t) g], floorDb);

    // Residual = envelope relative to its own broad local trend. Positive = resonant bump,
    // negative = hole.
    trendSmoother.process (gClamped.data(), gTrend.data());

    // --- Section 5: adaptive gain -------------------------------------------------------------
    const float scale = mapping::amountScale (p.amount);

    for (int g = 0; g < G; ++g)
    {
        const float residual = gClamped[(size_t) g] - gTrend[(size_t) g];
        const float raw = scale * (cv.toneDb[(size_t) g] - residual);
        gRaw[(size_t) g] = std::clamp (raw, -maxCutDb, maxBoostDb);
        cv.targetDb[(size_t) g] = scale * cv.toneDb[(size_t) g];
    }

    gainSmoother.process (gRaw.data(), gSmooth.data());
    const float gainMean = grid.weightedMean (gSmooth.data(), perceptualWeight.data());

    // --- Section 6: attack / release per grid point ------------------------------------------
    for (int g = 0; g < G; ++g)
    {
        const float target = silent ? 0.0f : gSmooth[(size_t) g] - gainMean;
        float& st = cv.follow[(size_t) g];
        const float delta = target - st;
        st += (delta < 0.0f ? attackCoeff[(size_t) g] : releaseCoeff[(size_t) g]) * delta;
    }

    // Attack and release differ, so the follower state can drift away from zero mean; re-centre
    // it so the level-independent range really stays level independent (section 5.4).
    const float followMean = grid.weightedMean (cv.follow.data(), perceptualWeight.data());

    // --- Section 7: squash ---------------------------------------------------------------------
    const float sq = mapping::squashAmount (p.amount);

    if (sq > 0.0f && ! silent)
    {
        const float ratio = 2.0f + 4.0f * sq;
        const float slopeFactor = 1.0f - 1.0f / ratio;

        for (int g = 0; g < G; ++g)
        {
            const float s = sensitivity[(size_t) g];
            const float bandLevel = cv.envelopeDb[(size_t) g]; // 1/3-octave band level, dBFS
            const float threshold = -12.0f - 18.0f * s - p.squashCal; // higher cal = lower threshold
            const float knee = 3.0f + 9.0f * s;
            const float over = bandLevel - threshold;

            float reduction = 0.0f;

            if (over >= knee * 0.5f)
                reduction = over * slopeFactor;
            else if (over > -knee * 0.5f)
                reduction = (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee) * slopeFactor;

            float upward = 0.0f;

            if (over < 0.0f)
            {
                const float gate = std::clamp ((bandLevel + 90.0f) / 10.0f, 0.0f, 1.0f);
                upward = std::min (-over * 0.3f * sq, maxBoostDb) * gate;
            }

            gSquash[(size_t) g] = upward - reduction;
            gTmp[(size_t) g] = std::pow (10.0f, bandLevel * 0.1f); // keep band power for the meter
        }

        gainSmoother.process (gSquash.data(), gDetail.data());
    }
    else
    {
        std::fill (gDetail.begin(), gDetail.end(), 0.0f);
        std::fill (gTmp.begin(), gTmp.end(), 0.0f);
    }

    float meterNum = 0.0f, meterDen = 0.0f;

    for (int g = 0; g < G; ++g)
    {
        float& st = cv.squashFollow[(size_t) g];
        const float delta = gDetail[(size_t) g] - st;
        st += (delta < 0.0f ? attackCoeff[(size_t) g] : releaseCoeff[(size_t) g]) * delta;

        const float contribution = sq * st;
        meterNum += std::abs (contribution) * gTmp[(size_t) g];
        meterDen += gTmp[(size_t) g];

        // Combine (section 7.5), then range mask (9.1) and channel focus (9.6).
        const float total = std::clamp (cv.follow[(size_t) g] - followMean + contribution, totalMinDb, totalMaxDb);
        gFinal[(size_t) g] = total * mask[(size_t) g] * focusDepth;
        cv.displayGainDb[(size_t) g] = gFinal[(size_t) g];
    }

    if (meterDen > 0.0f)
        squashMeterDb = std::max (squashMeterDb, meterNum / meterDen);

    // --- Section 8: mixed-phase synthesis ------------------------------------------------------
    if (minPhaseOnly)
    {
        grid.gridToBins (gFinal.data(), binDb.data());

        for (int k = 0; k < bins; ++k)
            binLogMag[(size_t) k] = binDb[(size_t) k] * ln10Over20;

        minPhase.computePhase (binLogMag.data(), cv.binPhase.data());
        cv.hasPhase = true;
    }
    else
    {
        heavySmoother.process (gFinal.data(), gSmooth.data());

        float detailEnergy = 0.0f, smoothEnergy = 0.0f;

        for (int g = 0; g < G; ++g)
        {
            gDetail[(size_t) g] = gFinal[(size_t) g] - gSmooth[(size_t) g];
            detailEnergy += gDetail[(size_t) g] * gDetail[(size_t) g];
            smoothEnergy += gSmooth[(size_t) g] * gSmooth[(size_t) g];
        }

        // 0 = broad curve, keep zero phase; 1 = detailed curve, minimum phase (section 8.4).
        const float blend = std::clamp (detailEnergy / (smoothEnergy + 0.25f * (float) G), 0.0f, 1.0f);

        if (blend > 0.01f)
        {
            grid.gridToBins (gDetail.data(), binDb.data());

            for (int k = 0; k < bins; ++k)
                binLogMag[(size_t) k] = binDb[(size_t) k] * ln10Over20;

            minPhase.computePhase (binLogMag.data(), cv.binPhase.data());

            for (int k = 0; k < bins; ++k)
                cv.binPhase[(size_t) k] *= blend;

            cv.hasPhase = true;
        }
        else
        {
            cv.hasPhase = false;
        }
    }

    // Band solo is a listening aid: a zero-phase band-pass around the soloed band's centre.
    if (p.bandSolo)
        for (int g = 0; g < G; ++g)
            gFinal[(size_t) g] += soloShapeDb[(size_t) g];

    grid.gridToBins (gFinal.data(), binDb.data());

    for (int k = 0; k < bins; ++k)
        cv.binGain[(size_t) k] = fastDbToGain (binDb[(size_t) k]);
}

} // namespace toneshaper
