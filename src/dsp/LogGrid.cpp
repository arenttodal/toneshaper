#include "LogGrid.h"

#include <algorithm>
#include <cmath>

namespace toneshaper
{

void LogGrid::prepare (int fftSize, double sampleRate, int pointsPerOctave)
{
    bins = fftSize / 2 + 1;
    ppo = pointsPerOctave;
    binHz = (float) (sampleRate / fftSize);

    const float f0 = binHz;
    const float nyquist = (float) (sampleRate * 0.5);
    const int count = (int) std::floor (std::log2 (nyquist / f0) * (float) ppo) + 1;

    log2F0 = std::log2 (f0);
    freqs.resize ((size_t) count);
    log2Freqs.resize ((size_t) count);
    cells.resize ((size_t) count);

    const float halfCell = 0.5f / (float) ppo;

    for (int g = 0; g < count; ++g)
    {
        const float l2 = log2F0 + (float) g / (float) ppo;
        const float f = std::exp2 (l2);
        freqs[(size_t) g] = f;
        log2Freqs[(size_t) g] = l2;

        const float lo = std::exp2 (l2 - halfCell) / binHz;
        const float hi = std::exp2 (l2 + halfCell) / binHz;

        auto& c = cells[(size_t) g];
        c.firstBin = std::max (1, (int) std::ceil (lo));
        c.lastBin = std::min (bins - 1, (int) std::floor (hi));

        // A cell holding fewer than two bins would just copy one bin and produce steps;
        // interpolate at the cell centre instead.
        if (c.lastBin - c.firstBin + 1 < 2)
        {
            c.lastBin = c.firstBin - 1;
            const float pos = std::clamp (f / binHz, 1.0f, (float) (bins - 1));
            c.interpBin = std::min ((int) std::floor (pos), bins - 2);
            c.interpFrac = pos - (float) c.interpBin;
        }
    }

    binMap.resize ((size_t) bins);

    for (int k = 0; k < bins; ++k)
    {
        if (k == 0)
        {
            binMap[0] = { 0, 0.0f };
            continue;
        }

        const float pos = std::clamp ((std::log2 ((float) k * binHz) - log2F0) * (float) ppo,
                                      0.0f, (float) (count - 1));
        const int i = std::min ((int) std::floor (pos), std::max (count - 2, 0));
        binMap[(size_t) k] = { i, count > 1 ? pos - (float) i : 0.0f };
    }
}

void LogGrid::binsToGrid (const float* binValues, float* grid) const noexcept
{
    const int count = size();

    for (int g = 0; g < count; ++g)
    {
        const auto& c = cells[(size_t) g];

        if (c.lastBin >= c.firstBin)
        {
            float sum = 0.0f;

            for (int k = c.firstBin; k <= c.lastBin; ++k)
                sum += binValues[k];

            grid[g] = sum / (float) (c.lastBin - c.firstBin + 1);
        }
        else
        {
            const float a = binValues[c.interpBin];
            const float b = binValues[c.interpBin + 1];
            grid[g] = a + c.interpFrac * (b - a);
        }
    }
}

void LogGrid::gridToBins (const float* grid, float* binValues) const noexcept
{
    const int count = size();

    for (int k = 0; k < bins; ++k)
    {
        const auto& m = binMap[(size_t) k];

        if (count < 2)
            binValues[k] = grid[0];
        else
            binValues[k] = grid[m.index] + m.frac * (grid[m.index + 1] - grid[m.index]);
    }
}

float LogGrid::valueAt (const float* grid, float hz) const noexcept
{
    const int count = size();

    if (count < 2)
        return grid[0];

    const float pos = std::clamp ((std::log2 (std::max (hz, 1.0f)) - log2F0) * (float) ppo,
                                  0.0f, (float) (count - 1));
    const int i = std::min ((int) std::floor (pos), count - 2);
    const float frac = pos - (float) i;
    return grid[i] + frac * (grid[i + 1] - grid[i]);
}

float LogGrid::weightedMean (const float* grid, const float* weights) const noexcept
{
    float sum = 0.0f, wsum = 0.0f;

    for (int g = 0; g < size(); ++g)
    {
        sum += weights[g] * grid[g];
        wsum += weights[g];
    }

    return wsum > 0.0f ? sum / wsum : 0.0f;
}

void GridSmoother::prepare (int gridSize, float sigmaPoints, Edges edgeMode)
{
    size = gridSize;
    edges = edgeMode;
    sigmaPoints = std::max (sigmaPoints, 0.25f);
    halfWidth = std::max (1, (int) std::ceil (3.0f * sigmaPoints));

    kernel.resize ((size_t) halfWidth + 1);

    for (int d = 0; d <= halfWidth; ++d)
        kernel[(size_t) d] = std::exp (-(float) (d * d) / (2.0f * sigmaPoints * sigmaPoints));

    norm.resize ((size_t) size);

    for (int i = 0; i < size; ++i)
    {
        float s = 0.0f;

        for (int d = -halfWidth; d <= halfWidth; ++d)
            if (edges == Edges::pointReflect || (i + d >= 0 && i + d < size))
                s += kernel[(size_t) std::abs (d)];

        norm[(size_t) i] = 1.0f / s;
    }
}

void GridSmoother::process (const float* in, float* out) const noexcept
{
    const int last = size - 1;

    for (int i = 0; i < size; ++i)
    {
        float s = 0.0f;

        if (edges == Edges::renormalise || (i - halfWidth >= 0 && i + halfWidth <= last))
        {
            const int lo = std::max (0, i - halfWidth);
            const int hi = std::min (last, i + halfWidth);

            for (int j = lo; j <= hi; ++j)
                s += kernel[(size_t) std::abs (j - i)] * in[j];
        }
        else
        {
            for (int j = i - halfWidth; j <= i + halfWidth; ++j)
            {
                float v;

                if (j < 0)
                    v = 2.0f * in[0] - in[std::min (-j, last)];
                else if (j > last)
                    v = 2.0f * in[last] - in[std::max (2 * last - j, 0)];
                else
                    v = in[j];

                s += kernel[(size_t) std::abs (j - i)] * v;
            }
        }

        out[i] = s * norm[(size_t) i];
    }
}

} // namespace toneshaper
