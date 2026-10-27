#pragma once

#include "Filters.h"
#include "Loudness.h"
#include "Params.h"
#include "SpectralCore.h"
#include "TripleBuffer.h"

#include <array>
#include <atomic>
#include <vector>

namespace toneshaper
{

/** Data handed to the UI: curves resampled onto a fixed 20 Hz - 20 kHz log axis. */
struct DisplayFrame
{
    static constexpr int numPoints = 256;
    static constexpr float minHz = 20.0f;
    static constexpr float maxHz = 20000.0f;

    static float frequencyAt (int i) noexcept
    {
        return minHz * std::pow (maxHz / minHz, (float) i / (float) (numPoints - 1));
    }

    int numCurves = 1;
    std::array<std::array<float, numPoints>, 2> inputDb {};  // 1/3-octave band level, dBFS
    std::array<std::array<float, numPoints>, 2> gainDb {};   // applied correction
    std::array<std::array<float, numPoints>, 2> targetDb {}; // tone-control target (scaled)
    float squashMeterDb = 0.0f;
    bool valid = false;
};

/** Complete signal path of section 1: stereo encode -> STFT core -> decode -> low/high cut ->
    wet trim -> wet/dry mix (latency-compensated dry) -> output gain, with bypass crossfade,
    sidechain routing and loudness metering. Framework independent; the JUCE processor is a
    thin wrapper around this. */
class ToneShaperEngine
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** io: numChannels (1 or 2) channels processed in place.
        sidechain may be null or have 1-2 channels. */
    void process (float* const* io, int numChannels, const float* const* sidechain, int numSidechainChannels,
                  int numSamples, const EngineParams& params) noexcept;

    int getLatencySamples (mapping::Quality q) const noexcept { return cores[(size_t) q].getLatency(); }
    int getActiveLatencySamples() const noexcept { return activeQualityIndex >= 0 ? cores[(size_t) activeQualityIndex].getLatency() : cores[0].getLatency(); }
    int getFftSize (mapping::Quality q) const noexcept { return cores[(size_t) q].getFftSize(); }

    TripleBuffer<DisplayFrame>& getDisplayBuffer() noexcept { return display; }

    float getInputLufs() const noexcept { return inputLufs.load (std::memory_order_relaxed); }
    float getWetLufs() const noexcept { return wetLufs.load (std::memory_order_relaxed); }
    float getSquashMeterDb() const noexcept { return squashMeter.load (std::memory_order_relaxed); }

private:
    struct Ramp
    {
        float current = 0.0f;

        /** Returns the per-sample increment for reaching target over numSamples. */
        float stepTowards (float target, int numSamples) noexcept
        {
            return (target - current) / (float) std::max (numSamples, 1);
        }
    };

    void processChunk (float* const* io, int numChannels, const float* const* sidechain, int numSc,
                       int offset, int len, const EngineParams& p) noexcept;
    void updateFilters (const EngineParams& p) noexcept;
    void publishDisplay() noexcept;

    static constexpr int chunkSize = 512;

    double sampleRate = 48000.0;
    bool prepared = false;
    std::array<SpectralCore, 3> cores; // indexed by mapping::Quality
    int activeQualityIndex = -1;

    std::array<std::vector<float>, maxChannels> wet, dry, side;
    std::array<DelayLine, maxChannels> dryDelay, sidechainDelay;
    std::array<Butterworth3, maxChannels> lowCut, highCut;
    float lowCutHz = -1.0f, highCutHz = -1.0f;
    bool lowCutOn = false, highCutOn = false;

    ShortTermLoudness inputMeter, wetMeter;
    std::atomic<float> inputLufs { -100.0f }, wetLufs { -100.0f }, squashMeter { 0.0f };

    Ramp wetGain, mixAmount, outputGain, bypassAmount;
    bool rampsPrimed = false;

    TripleBuffer<DisplayFrame> display;
    int samplesSinceDisplay = 0, displayInterval = 800;
};

} // namespace toneshaper
