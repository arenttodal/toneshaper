#pragma once

#include <cstddef>
#include <vector>

struct PFFFT_Setup;

namespace toneshaper
{

/** 16-byte aligned float buffer (PFFFT requires SIMD-aligned memory).
    Allocation happens only in resize(), never on the audio thread. */
class AlignedBuffer
{
public:
    AlignedBuffer() = default;
    explicit AlignedBuffer (size_t n) { resize (n); }
    ~AlignedBuffer();

    AlignedBuffer (const AlignedBuffer&) = delete;
    AlignedBuffer& operator= (const AlignedBuffer&) = delete;
    AlignedBuffer (AlignedBuffer&& other) noexcept;
    AlignedBuffer& operator= (AlignedBuffer&& other) noexcept;

    void resize (size_t n);
    void clear() noexcept;

    float* data() noexcept { return ptr; }
    const float* data() const noexcept { return ptr; }
    size_t size() const noexcept { return count; }
    float& operator[] (size_t i) noexcept { return ptr[i]; }
    const float& operator[] (size_t i) const noexcept { return ptr[i]; }

private:
    float* ptr = nullptr;
    size_t count = 0;
};

/** Real-valued FFT built on PFFFT.

    Spectrum layout ("ordered" PFFFT format, N floats):
        [ Re(0), Re(N/2), Re(1), Im(1), Re(2), Im(2), ..., Re(N/2-1), Im(N/2-1) ]

    Neither direction is normalised: inverse (forward (x)) == N * x. */
class RealFFT
{
public:
    RealFFT() = default;
    ~RealFFT();

    RealFFT (const RealFFT&) = delete;
    RealFFT& operator= (const RealFFT&) = delete;

    /** N must be a multiple of 32 with factors 2, 3 and 5 only. */
    void prepare (int fftSize);

    int size() const noexcept { return n; }

    /** in and out may alias. Both must be aligned (use AlignedBuffer). */
    void forward (const float* in, float* out) noexcept;
    void inverse (const float* in, float* out) noexcept;

private:
    PFFFT_Setup* setup = nullptr;
    AlignedBuffer work;
    int n = 0;
};

} // namespace toneshaper
