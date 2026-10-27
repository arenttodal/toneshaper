#pragma once

#include "Mappings.h"

#include <array>

namespace toneshaper
{

enum class StereoMode
{
    linked = 0,
    midSide = 1,
    leftRight = 2
};

struct BandSettings
{
    float freqNorm = 0.5f; // 0..1, mapped per band with mapping::bandNormToHz
    float level = 0.0f;    // -10..+10, mapped with mapping::bandLevelToDb
};

/** Snapshot of every user parameter, in storage units (section 10). The plugin copies its
    atomics into one of these at the start of each block; the engine never touches atomics. */
struct EngineParams
{
    // 10.1 main
    float amount = 4.0f;
    float attack = 1.5f;
    float release = 5.45f;

    // 10.2 tone controls: [channel][band], channel 0 = main/ch1, 1 = ch2 (split modes only)
    std::array<bool, mapping::numBands> bandOn { true, true, true, true };
    std::array<std::array<BandSettings, mapping::numBands>, 2> bands {};

    // 10.3 processing range and output filters
    float freqLimLo = 20.0f;
    float freqLimHi = 20000.0f;
    bool freqLimFlip = false;
    float loCut = 20.0f;     // 20 Hz = off
    float hiCut = 20000.0f;  // 20 kHz = off

    // 10.4 output and calibration
    float squashCal = 0.0f;  // dB
    float wetTrimDb = 0.0f;
    float mix = 1.0f;        // 0..1
    float outputDb = 0.0f;

    // 10.5 stereo
    StereoMode stereoMode = StereoMode::linked;
    float channelLink = 1.0f;  // 0..1
    float channelFocus = 0.5f; // 0 = ch1 only, 0.5 = equal, 1 = ch2 only

    // 10.6 utility
    bool bypass = false;
    mapping::Quality quality = mapping::Quality::normal;
    bool scEnabled = false;
    bool scListen = false;
    bool bandSolo = false;
    int bandSoloCh = 0;
    int bandSoloBand = 0; // 0..3; which band "band solo" listens to

    EngineParams()
    {
        for (auto& ch : bands)
            for (int b = 0; b < mapping::numBands; ++b)
                ch[(size_t) b].freqNorm = mapping::bandDefaultNorm (b);
    }
};

} // namespace toneshaper
