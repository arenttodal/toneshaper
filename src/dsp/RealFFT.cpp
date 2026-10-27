#include "RealFFT.h"

#include "pffft.h"

#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace toneshaper
{

AlignedBuffer::~AlignedBuffer()
{
    if (ptr != nullptr)
        pffft_aligned_free (ptr);
}

AlignedBuffer::AlignedBuffer (AlignedBuffer&& other) noexcept
    : ptr (std::exchange (other.ptr, nullptr)), count (std::exchange (other.count, 0))
{
}

AlignedBuffer& AlignedBuffer::operator= (AlignedBuffer&& other) noexcept
{
    if (this != &other)
    {
        if (ptr != nullptr)
            pffft_aligned_free (ptr);

        ptr = std::exchange (other.ptr, nullptr);
        count = std::exchange (other.count, 0);
    }

    return *this;
}

void AlignedBuffer::resize (size_t n)
{
    if (n == count)
    {
        clear();
        return;
    }

    if (ptr != nullptr)
        pffft_aligned_free (ptr);

    ptr = nullptr;
    count = 0;

    if (n > 0)
    {
        ptr = static_cast<float*> (pffft_aligned_malloc (n * sizeof (float)));

        if (ptr == nullptr)
            throw std::bad_alloc();

        count = n;
        clear();
    }
}

void AlignedBuffer::clear() noexcept
{
    if (ptr != nullptr)
        std::memset (ptr, 0, count * sizeof (float));
}

RealFFT::~RealFFT()
{
    if (setup != nullptr)
        pffft_destroy_setup (setup);
}

void RealFFT::prepare (int fftSize)
{
    if (fftSize == n && setup != nullptr)
        return;

    if (setup != nullptr)
        pffft_destroy_setup (setup);

    setup = pffft_new_setup (fftSize, PFFFT_REAL);

    if (setup == nullptr)
        throw std::invalid_argument ("Unsupported FFT size");

    n = fftSize;
    work.resize (static_cast<size_t> (fftSize));
}

void RealFFT::forward (const float* in, float* out) noexcept
{
    pffft_transform_ordered (setup, in, out, work.data(), PFFFT_FORWARD);
}

void RealFFT::inverse (const float* in, float* out) noexcept
{
    pffft_transform_ordered (setup, in, out, work.data(), PFFFT_BACKWARD);
}

} // namespace toneshaper
