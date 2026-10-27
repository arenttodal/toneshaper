#pragma once

#include <atomic>

namespace toneshaper
{

/** Lock-free single-producer / single-consumer triple buffer. The audio thread writes into
    writeBuffer() and calls publish(); the UI thread calls fetch() and reads readBuffer().
    Neither side ever blocks, and the reader always sees a complete frame. */
template <typename T>
class TripleBuffer
{
public:
    T& writeBuffer() noexcept { return buffers[writeIndex]; }

    void publish() noexcept
    {
        const int previous = middle.exchange (writeIndex | dirtyFlag, std::memory_order_acq_rel);
        writeIndex = previous & indexMask;
    }

    /** Returns true if a new frame was swapped in. */
    bool fetch() noexcept
    {
        if ((middle.load (std::memory_order_acquire) & dirtyFlag) == 0)
            return false;

        const int previous = middle.exchange (readIndex, std::memory_order_acq_rel);
        readIndex = previous & indexMask;
        return true;
    }

    const T& readBuffer() const noexcept { return buffers[readIndex]; }

private:
    static constexpr int dirtyFlag = 4, indexMask = 3;

    T buffers[3] {};
    std::atomic<int> middle { 2 };
    int writeIndex = 0, readIndex = 1;
};

} // namespace toneshaper
