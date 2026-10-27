#pragma once

#include "LogGrid.h"
#include "MinPhase.h"
#include "Params.h"
#include "RealFFT.h"

#include <array>
#include <vector>

namespace toneshaper
{

/** One overlap-add STFT processor at a fixed FFT size (sections 2-8).

    Runs the whole spectral pipeline: magnitude -> log-frequency envelope -> adaptive gain
    against the tone target -> frequency smoothing -> normalisation -> attack/release ->
    squash -> processing-range mask -> mixed-phase synthesis -> ISTFT.

    Up to two channels are processed. With split curves each channel gets its own gain curve
    (L/R or M/S split modes); otherwise one curve, computed from the power average of all
    analysis channels, is applied to every channel (linked mode). */
class SpectralCore
{
public:
    static constexpr int maxChannels = 2;

    /** Allocates everything. minimumPhaseOnly selects the low-latency synthesis (section 8.2). */
    void prepare (double sampleRate, int fftSize, bool minimumPhaseOnly);
    void reset();

    int getFftSize() const noexcept { return n; }
    int getHopSize() const noexcept { return hop; }

    /** Input-to-output delay in samples. A frame can only be released once every window that
        overlaps it has been analysed, so the delay is one full FFT length. */
    int getLatency() const noexcept { return n; }

    /** Processes numChannels (1 or 2) channels in place. analysis, when non-null, supplies the
        detector signal (sidechain) with the same channel count; otherwise the processed signal
        is analysed. */
    void process (float* const* channels, int numChannels, const float* const* analysis,
                  int numSamples, const EngineParams& params, bool splitCurves) noexcept;

    // Display / metering access (audio thread only; the engine copies these out).
    const LogGrid& getGrid() const noexcept { return grid; }
    int getNumCurves() const noexcept { return activeCurves; }
    const float* getEnvelopeDb (int curve) const noexcept { return curves[(size_t) curve].envelopeDb.data(); }
    const float* getGainDb (int curve) const noexcept { return curves[(size_t) curve].displayGainDb.data(); }
    const float* getTargetDb (int curve) const noexcept { return curves[(size_t) curve].targetDb.data(); }
    float getSquashMeterDb() const noexcept { return squashMeterDb; }

    /** True if at least one frame was processed since the previous call. */
    bool consumeNewFrame() noexcept
    {
        const bool r = newFrame;
        newFrame = false;
        return r;
    }

private:
    struct CurveState
    {
        std::vector<float> toneDb;        // unscaled tone target, auto-compensated (section 4.3)
        std::vector<float> detector;      // short-term averaged power per grid point
        std::vector<float> follow;        // temporal follower state, dB (section 6)
        std::vector<float> squashFollow;  // squash follower state, dB (section 7)
        std::vector<float> envelopeDb;    // display: 1/3-octave band level
        std::vector<float> displayGainDb; // display: final gain curve
        std::vector<float> targetDb;      // display: scaled tone target
        std::vector<float> binGain;       // linear gain per bin
        std::vector<float> binPhase;      // phase per bin, radians
        bool hasPhase = false;
        bool detectorPrimed = false;
    };

    struct ParamCache
    {
        std::array<bool, mapping::numBands> bandOn {};
        std::array<std::array<BandSettings, mapping::numBands>, 2> bands {};
        float attack = -1.0f, release = -1.0f;
        float freqLimLo = -1.0f, freqLimHi = -1.0f;
        bool freqLimFlip = false;
        bool bandSolo = false;
        int bandSoloCh = -1, bandSoloBand = -1;
        float soloFreq = -1.0f;
        bool valid = false;
    };

    void updateCaches (const EngineParams& p) noexcept;
    void computeToneTarget (int curve, const EngineParams& p) noexcept;
    void processFrame (int numChannels, bool externalAnalysis, const EngineParams& p, bool split) noexcept;
    void computeCurve (int curve, const float* power, const EngineParams& p, float focusDepth) noexcept;
    void computePowerSpectrum (const float* spectrum, float* power) const noexcept;

    double sampleRate = 48000.0;
    int n = 0, hop = 0, bins = 0, gridSize = 0;
    bool minPhaseOnly = false;

    RealFFT fft;
    MinPhase minPhase;
    LogGrid grid;
    GridSmoother envelopeSmoother, trendSmoother, gainSmoother, heavySmoother;

    AlignedBuffer window;
    std::array<std::vector<float>, maxChannels> inRing, analysisRing, outRing;
    std::array<AlignedBuffer, maxChannels> spectrum, analysisSpectrum;
    AlignedBuffer frame;
    int ringPos = 0, hopCount = 0;
    float outputScale = 1.0f, powerScale = 1.0f;

    std::array<CurveState, maxChannels> curves;
    int activeCurves = 1;

    // Per-grid constants.
    std::vector<float> perceptualWeight, sensitivity, bandBins, timeScale, detectorCoeff;
    // Per-grid parameter-derived values.
    std::vector<float> attackCoeff, releaseCoeff, mask, soloShapeDb;
    // Grid scratch.
    std::vector<float> gPower, gEnv, gClamped, gTrend, gRaw, gSmooth, gSquash, gTmp, gFinal, gDetail;
    // Bin scratch.
    std::array<std::vector<float>, maxChannels> power;
    std::vector<float> combinedPower, binDb, binLogMag;

    ParamCache cache;
    float squashMeterDb = 0.0f;
    bool newFrame = false;
};

} // namespace toneshaper
