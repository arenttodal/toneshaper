#pragma once

#include <vector>

namespace toneshaper
{

/** A constant-Q (log-frequency) grid laid over the linear FFT bins.

    All spectral smoothing in the engine runs on this grid: a kernel that is constant in
    octaves becomes a constant-width kernel here, so 1/3-octave or 2-octave smoothing costs
    the same at every frequency and every FFT size (sections 3.2 and 5.3). Grid point g sits at
    f0 * 2^(g / pointsPerOctave), with f0 = the first non-DC bin. */
class LogGrid
{
public:
    void prepare (int fftSize, double sampleRate, int pointsPerOctave);

    int size() const noexcept { return (int) freqs.size(); }
    int numBins() const noexcept { return bins; }
    int pointsPerOctave() const noexcept { return ppo; }
    float binWidth() const noexcept { return binHz; }
    float frequency (int g) const noexcept { return freqs[(size_t) g]; }
    const float* frequencies() const noexcept { return freqs.data(); }
    const float* log2Frequencies() const noexcept { return log2Freqs.data(); }

    /** Averages per-bin values into grid cells. Cells narrower than a bin interpolate. */
    void binsToGrid (const float* binValues, float* grid) const noexcept;

    /** Linear interpolation (on the log-frequency axis) from the grid back to every bin. */
    void gridToBins (const float* grid, float* binValues) const noexcept;

    /** Linear interpolation of grid data at an arbitrary frequency. */
    float valueAt (const float* grid, float hz) const noexcept;

    /** Sum(w * x) / Sum(w). */
    float weightedMean (const float* grid, const float* weights) const noexcept;

private:
    struct CellMap
    {
        int firstBin = 0, lastBin = -1; // inclusive range averaged when non-empty
        int interpBin = 1;              // otherwise interpolate between interpBin and interpBin + 1
        float interpFrac = 0.0f;
    };

    struct BinMap
    {
        int index = 0;
        float frac = 0.0f;
    };

    std::vector<float> freqs, log2Freqs;
    std::vector<CellMap> cells;
    std::vector<BinMap> binMap;
    int bins = 0, ppo = 24;
    float binHz = 1.0f, log2F0 = 0.0f;
};

/** Gaussian smoothing on the log grid. */
class GridSmoother
{
public:
    enum class Edges
    {
        /** Renormalise the kernel to the taps inside the grid. Fine for data centred on zero. */
        renormalise,

        /** Extend the data by point reflection about each end value. Linear slopes pass through
            unchanged, so a falling spectrum's broad trend isn't bent upwards near Nyquist. */
        pointReflect
    };

    void prepare (int gridSize, float sigmaPoints, Edges edgeMode = Edges::renormalise);
    void process (const float* in, float* out) const noexcept;

private:
    std::vector<float> kernel; // one side, kernel[0] is the centre tap
    std::vector<float> norm;   // 1 / sum of the taps used, per point
    int halfWidth = 0, size = 0;
    Edges edges = Edges::renormalise;
};

} // namespace toneshaper
