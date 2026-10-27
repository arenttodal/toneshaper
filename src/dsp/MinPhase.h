#pragma once

#include "RealFFT.h"

namespace toneshaper
{

/** Minimum-phase reconstruction from a log-magnitude response via the real cepstrum
    (section 8.2): IFFT(log|H|) -> fold the acausal half onto the causal half -> FFT.
    The imaginary part of the result is the minimum-phase response in radians. */
class MinPhase
{
public:
    void prepare (int fftSize);

    /** logMag: natural-log magnitude for bins 0..N/2. phase: output for bins 0..N/2. */
    void computePhase (const float* logMag, float* phase) noexcept;

private:
    RealFFT fft;
    AlignedBuffer buf, ceps;
    int n = 0;
};

} // namespace toneshaper
