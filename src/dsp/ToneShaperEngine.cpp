#include "ToneShaperEngine.h"

#include <algorithm>
#include <cmath>

namespace toneshaper
{

void ToneShaperEngine::prepare (double sr, int /*maxBlockSize*/)
{
    sampleRate = sr;

    int maxLatency = 0;

    for (int q = 0; q < 3; ++q)
    {
        const auto quality = (mapping::Quality) q;
        cores[(size_t) q].prepare (sr, mapping::fftSizeFor (quality, sr), quality == mapping::Quality::lowLatency);
        maxLatency = std::max (maxLatency, cores[(size_t) q].getLatency());
    }

    for (int c = 0; c < maxChannels; ++c)
    {
        wet[(size_t) c].assign (chunkSize, 0.0f);
        dry[(size_t) c].assign (chunkSize, 0.0f);
        side[(size_t) c].assign (chunkSize, 0.0f);
        dryDelay[(size_t) c].prepare (maxLatency);
        sidechainDelay[(size_t) c].prepare (maxLatency);
    }

    inputMeter.prepare (sr);
    wetMeter.prepare (sr);
    displayInterval = std::max (1, (int) (sr / 60.0));

    activeQualityIndex = -1;
    reset();
}

void ToneShaperEngine::reset()
{
    for (auto& core : cores)
        core.reset();

    for (int c = 0; c < maxChannels; ++c)
    {
        dryDelay[(size_t) c].reset();
        sidechainDelay[(size_t) c].reset();
        lowCut[(size_t) c].reset();
        highCut[(size_t) c].reset();
    }

    inputMeter.reset();
    wetMeter.reset();
    lowCutHz = highCutHz = -1.0f;
    rampsPrimed = false;
    samplesSinceDisplay = 0;
}

void ToneShaperEngine::updateFilters (const EngineParams& p) noexcept
{
    const bool lowOn = p.loCut > 20.5f;
    const bool highOn = p.hiCut < 19990.0f && p.hiCut < sampleRate * 0.45;

    if (lowOn && (! lowCutOn || p.loCut != lowCutHz))
    {
        for (auto& f : lowCut)
        {
            if (! lowCutOn)
                f.reset();

            f.setup (Butterworth3::Type::highPass, sampleRate, p.loCut);
        }

        lowCutHz = p.loCut;
    }

    if (highOn && (! highCutOn || p.hiCut != highCutHz))
    {
        for (auto& f : highCut)
        {
            if (! highCutOn)
                f.reset();

            f.setup (Butterworth3::Type::lowPass, sampleRate, p.hiCut);
        }

        highCutHz = p.hiCut;
    }

    lowCutOn = lowOn;
    highCutOn = highOn;
}

void ToneShaperEngine::process (float* const* io, int numChannels, const float* const* sidechain,
                                int numSidechainChannels, int numSamples, const EngineParams& p) noexcept
{
    numChannels = std::clamp (numChannels, 1, maxChannels);

    const int q = (int) p.quality;

    if (q != activeQualityIndex)
    {
        activeQualityIndex = std::clamp (q, 0, 2);
        cores[(size_t) activeQualityIndex].reset();

        const int latency = cores[(size_t) activeQualityIndex].getLatency();

        for (int c = 0; c < maxChannels; ++c)
        {
            dryDelay[(size_t) c].setDelay (latency);
            sidechainDelay[(size_t) c].setDelay (latency);
        }
    }

    updateFilters (p);

    for (int offset = 0; offset < numSamples; offset += chunkSize)
        processChunk (io, numChannels, sidechain, numSidechainChannels, offset,
                      std::min (chunkSize, numSamples - offset), p);

    inputLufs.store (inputMeter.getLufs(), std::memory_order_relaxed);
    wetLufs.store (wetMeter.getLufs(), std::memory_order_relaxed);
}

void ToneShaperEngine::processChunk (float* const* io, int numChannels, const float* const* sc, int numSc,
                                     int offset, int len, const EngineParams& p) noexcept
{
    auto& core = cores[(size_t) activeQualityIndex];
    const bool stereo = numChannels == 2;
    const bool midSide = stereo && p.stereoMode == StereoMode::midSide;
    const bool split = stereo && p.stereoMode != StereoMode::linked;
    const bool useSidechain = p.scEnabled && sc != nullptr && numSc > 0;

    float* wetPtr[maxChannels] = { wet[0].data(), wet[1].data() };
    float* dryPtr[maxChannels] = { dry[0].data(), dry[1].data() };
    float* sidePtr[maxChannels] = { side[0].data(), side[1].data() };

    for (int c = 0; c < numChannels; ++c)
        std::copy (io[c] + offset, io[c] + offset + len, dryPtr[c]);

    inputMeter.process (dryPtr, numChannels, len);

    // Encode the processing channels.
    if (midSide)
    {
        for (int i = 0; i < len; ++i)
        {
            const float l = dryPtr[0][i], r = dryPtr[1][i];
            wetPtr[0][i] = 0.5f * (l + r);
            wetPtr[1][i] = 0.5f * (l - r);
        }
    }
    else
    {
        for (int c = 0; c < numChannels; ++c)
            std::copy (dryPtr[c], dryPtr[c] + len, wetPtr[c]);
    }

    // Sidechain detector signal, matched to the processing channel layout.
    if (useSidechain)
    {
        const float* s0 = sc[0] + offset;
        const float* s1 = sc[std::min (1, numSc - 1)] + offset;

        if (! stereo)
        {
            for (int i = 0; i < len; ++i)
                sidePtr[0][i] = 0.5f * (s0[i] + s1[i]);
        }
        else if (midSide)
        {
            for (int i = 0; i < len; ++i)
            {
                sidePtr[0][i] = 0.5f * (s0[i] + s1[i]);
                sidePtr[1][i] = 0.5f * (s0[i] - s1[i]);
            }
        }
        else
        {
            std::copy (s0, s0 + len, sidePtr[0]);
            std::copy (s1, s1 + len, sidePtr[1]);
        }
    }

    core.process (wetPtr, numChannels, useSidechain ? sidePtr : nullptr, len, p, split);

    if (midSide)
    {
        for (int i = 0; i < len; ++i)
        {
            const float m = wetPtr[0][i], s = wetPtr[1][i];
            wetPtr[0][i] = m + s;
            wetPtr[1][i] = m - s;
        }
    }

    for (int c = 0; c < numChannels; ++c)
    {
        if (lowCutOn)
            for (int i = 0; i < len; ++i)
                wetPtr[c][i] = lowCut[(size_t) c].process (wetPtr[c][i]);

        if (highCutOn)
            for (int i = 0; i < len; ++i)
                wetPtr[c][i] = highCut[(size_t) c].process (wetPtr[c][i]);
    }

    wetMeter.process (wetPtr, numChannels, len);

    // Latency-compensated dry path (and sidechain, for sc listen).
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < len; ++i)
            dryPtr[c][i] = dryDelay[(size_t) c].process (dryPtr[c][i]);

    const bool listen = useSidechain && p.scListen;

    if (useSidechain)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            const float* s = sc[std::min (c, numSc - 1)] + offset;

            for (int i = 0; i < len; ++i)
                sidePtr[c][i] = sidechainDelay[(size_t) c].process (s[i]);
        }
    }

    const float wetTarget = mapping::dbToGain (p.wetTrimDb);
    const float mixTarget = listen ? 1.0f : std::clamp (p.mix, 0.0f, 1.0f);
    const float outTarget = mapping::dbToGain (p.outputDb);
    const float bypassTarget = p.bypass ? 1.0f : 0.0f;

    if (! rampsPrimed)
    {
        // Start from the targets instead of fading in from zero.
        wetGain.current = wetTarget;
        mixAmount.current = mixTarget;
        outputGain.current = outTarget;
        bypassAmount.current = bypassTarget;
        rampsPrimed = true;
    }

    const float wetStep = wetGain.stepTowards (wetTarget, len);
    const float mixStep = mixAmount.stepTowards (mixTarget, len);
    const float outStep = outputGain.stepTowards (outTarget, len);
    const float bypassStep = bypassAmount.stepTowards (bypassTarget, len);

    for (int c = 0; c < numChannels; ++c)
    {
        float wg = wetGain.current, mx = mixAmount.current, og = outputGain.current, bp = bypassAmount.current;
        const float* processed = listen ? sidePtr[c] : wetPtr[c];
        float* out = io[c] + offset;

        for (int i = 0; i < len; ++i)
        {
            // Sidechain monitoring ignores the wet trim.
            const float wetSample = listen ? processed[i] : processed[i] * wg;
            const float mixed = (mx * wetSample + (1.0f - mx) * dryPtr[c][i]) * og;
            out[i] = mixed + bp * (dryPtr[c][i] - mixed);

            wg += wetStep;
            mx += mixStep;
            og += outStep;
            bp += bypassStep;
        }
    }

    wetGain.current = wetTarget;
    mixAmount.current = mixTarget;
    outputGain.current = outTarget;
    bypassAmount.current = bypassTarget;

    samplesSinceDisplay += len;

    if (core.consumeNewFrame())
    {
        squashMeter.store (core.getSquashMeterDb(), std::memory_order_relaxed);

        if (samplesSinceDisplay >= displayInterval)
        {
            samplesSinceDisplay = 0;
            publishDisplay();
        }
    }
}

void ToneShaperEngine::publishDisplay() noexcept
{
    const auto& core = cores[(size_t) activeQualityIndex];
    const auto& grid = core.getGrid();
    auto& f = display.writeBuffer();

    f.numCurves = core.getNumCurves();

    for (int c = 0; c < f.numCurves; ++c)
    {
        const float* env = core.getEnvelopeDb (c);
        const float* gain = core.getGainDb (c);
        const float* target = core.getTargetDb (c);

        for (int i = 0; i < DisplayFrame::numPoints; ++i)
        {
            const float hz = DisplayFrame::frequencyAt (i);
            f.inputDb[(size_t) c][(size_t) i] = grid.valueAt (env, hz);
            f.gainDb[(size_t) c][(size_t) i] = grid.valueAt (gain, hz);
            f.targetDb[(size_t) c][(size_t) i] = grid.valueAt (target, hz);
        }
    }

    f.squashMeterDb = core.getSquashMeterDb();
    f.valid = true;
    display.publish();
}

} // namespace toneshaper
