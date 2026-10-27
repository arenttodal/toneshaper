#pragma once

#include "Filters.h"

#include <array>
#include <cmath>
#include <vector>

namespace toneshaper
{

/** ITU-R BS.1770 short-term loudness (3 s window), used by the "set" buttons for wet trim
    (section 9.3) and squash calibration (section 7.3). */
class ShortTermLoudness
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        blockLength = std::max (1, (int) std::round (sampleRate * 0.1));

        for (auto& ch : stages)
        {
            designShelf (ch[0]);
            designHighPass (ch[1]);
        }

        reset();
    }

    void reset()
    {
        for (auto& ch : stages)
            for (auto& s : ch)
                s.reset();

        blocks.fill (0.0);
        blockAccum = 0.0;
        blockCount = 0;
        blockIndex = 0;
        filledBlocks = 0;
    }

    /** Feeds one block of one or two channels. */
    void process (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, 2);

        for (int i = 0; i < numSamples; ++i)
        {
            double sum = 0.0;

            for (int c = 0; c < numChannels; ++c)
            {
                const float y = stages[(size_t) c][1].process (stages[(size_t) c][0].process (channels[c][i]));
                sum += (double) y * (double) y;
            }

            // A mono signal is treated as being played over two speakers.
            if (numChannels == 1)
                sum *= 2.0;

            blockAccum += sum;

            if (++blockCount == blockLength)
            {
                blocks[(size_t) blockIndex] = blockAccum / blockLength;
                blockIndex = (blockIndex + 1) % numBlocks;
                filledBlocks = std::min (filledBlocks + 1, numBlocks);
                blockAccum = 0.0;
                blockCount = 0;
            }
        }
    }

    /** LUFS over the last 3 s, or -100 when nothing has been measured. */
    float getLufs() const noexcept
    {
        if (filledBlocks == 0)
            return -100.0f;

        double s = 0.0;

        for (int i = 0; i < filledBlocks; ++i)
            s += blocks[(size_t) i];

        s /= filledBlocks;
        return s > 1.0e-10 ? (float) (-0.691 + 10.0 * std::log10 (s)) : -100.0f;
    }

private:
    static constexpr int numBlocks = 30;

    void designShelf (Biquad& b) const
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan (3.14159265358979323846 * f0 / fs);
        const double vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        b.b0 = (float) ((vh + vb * k / q + k * k) / a0);
        b.b1 = (float) (2.0 * (k * k - vh) / a0);
        b.b2 = (float) ((vh - vb * k / q + k * k) / a0);
        b.a1 = (float) (2.0 * (k * k - 1.0) / a0);
        b.a2 = (float) ((1.0 - k / q + k * k) / a0);
    }

    void designHighPass (Biquad& b) const
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan (3.14159265358979323846 * f0 / fs);
        const double a0 = 1.0 + k / q + k * k;
        b.b0 = 1.0f;
        b.b1 = -2.0f;
        b.b2 = 1.0f;
        b.a1 = (float) (2.0 * (k * k - 1.0) / a0);
        b.a2 = (float) ((1.0 - k / q + k * k) / a0);
    }

    double fs = 48000.0;
    std::array<std::array<Biquad, 2>, 2> stages {};
    std::array<double, numBlocks> blocks {};
    double blockAccum = 0.0;
    int blockLength = 4800, blockCount = 0, blockIndex = 0, filledBlocks = 0;
};

} // namespace toneshaper
