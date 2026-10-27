#include "MinPhase.h"

namespace toneshaper
{

void MinPhase::prepare (int fftSize)
{
    n = fftSize;
    fft.prepare (fftSize);
    buf.resize ((size_t) fftSize);
    ceps.resize ((size_t) fftSize);
}

void MinPhase::computePhase (const float* logMag, float* phase) noexcept
{
    const int half = n / 2;

    // Real, even spectrum in ordered layout.
    buf[0] = logMag[0];
    buf[1] = logMag[half];

    for (int k = 1; k < half; ++k)
    {
        buf[(size_t) (2 * k)] = logMag[k];
        buf[(size_t) (2 * k + 1)] = 0.0f;
    }

    fft.inverse (buf.data(), ceps.data());

    // Fold: keep c[0] and c[N/2], double the causal part, zero the acausal part.
    const float scale = 1.0f / (float) n;
    ceps[0] *= scale;
    ceps[(size_t) half] *= scale;

    for (int i = 1; i < half; ++i)
    {
        ceps[(size_t) i] *= 2.0f * scale;
        ceps[(size_t) (n - i)] = 0.0f;
    }

    fft.forward (ceps.data(), buf.data());

    phase[0] = 0.0f;
    phase[half] = 0.0f;

    for (int k = 1; k < half; ++k)
        phase[k] = buf[(size_t) (2 * k + 1)];
}

} // namespace toneshaper
