#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace toneshaper
{

/** Transposed direct form II biquad. */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }
};

/** 3rd-order Butterworth (18 dB/oct) high- or low-pass: one first-order section plus one
    biquad with Q = 1 (section 9.2). Coefficients from the bilinear transform with prewarping. */
class Butterworth3
{
public:
    enum class Type { highPass, lowPass };

    void setup (Type t, double sampleRate, float cutoffHz) noexcept
    {
        const double fc = std::clamp ((double) cutoffHz, 1.0, sampleRate * 0.45);
        const double k = std::tan (3.14159265358979323846 * fc / sampleRate);

        // First-order section.
        {
            const double a0 = k + 1.0;
            auto& s = first;
            s.a1 = (float) ((k - 1.0) / a0);
            s.a2 = 0.0f;

            if (t == Type::lowPass) { s.b0 = (float) (k / a0); s.b1 = s.b0; }
            else                    { s.b0 = (float) (1.0 / a0); s.b1 = -s.b0; }

            s.b2 = 0.0f;
        }

        // Second-order section, Q = 1.
        {
            const double q = 1.0;
            const double k2 = k * k;
            const double a0 = 1.0 + k / q + k2;
            auto& s = second;
            s.a1 = (float) (2.0 * (k2 - 1.0) / a0);
            s.a2 = (float) ((1.0 - k / q + k2) / a0);

            if (t == Type::lowPass)
            {
                s.b0 = (float) (k2 / a0);
                s.b1 = 2.0f * s.b0;
                s.b2 = s.b0;
            }
            else
            {
                s.b0 = (float) (1.0 / a0);
                s.b1 = -2.0f * s.b0;
                s.b2 = s.b0;
            }
        }
    }

    float process (float x) noexcept { return second.process (first.process (x)); }

    void reset() noexcept
    {
        first.reset();
        second.reset();
    }

private:
    Biquad first, second;
};

/** Fixed-capacity delay line; the delay may change without reallocation. */
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        buffer.assign ((size_t) maxDelay + 1, 0.0f);
        writePos = 0;
    }

    void setDelay (int samples) noexcept { delay = std::clamp (samples, 0, (int) buffer.size() - 1); }
    int getDelay() const noexcept { return delay; }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    float process (float x) noexcept
    {
        const int size = (int) buffer.size();
        buffer[(size_t) writePos] = x;
        int readPos = writePos - delay;

        if (readPos < 0)
            readPos += size;

        const float y = buffer[(size_t) readPos];

        if (++writePos == size)
            writePos = 0;

        return y;
    }

private:
    std::vector<float> buffer { 0.0f };
    int writePos = 0, delay = 0;
};

} // namespace toneshaper
