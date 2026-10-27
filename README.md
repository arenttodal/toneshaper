# ToneShaper

An adaptive tone shaper plugin (AU / VST3 / Standalone, macOS universal). It analyses the
spectrum of the incoming audio in real time, compares its broad tonal shape against a target
you set with four tone bands, and applies a smooth, continuously varying EQ that pushes the
signal towards that target. Resonant bumps are pulled down, holes are filled, and the
correction follows the music frame by frame with attack/release ballistics. Above amount 7 it
adds "squash": frequency-dependent compression weighted by equal-loudness contours.

## Download (macOS)

Every push builds on GitHub Actions (`.github/workflows/build-macos.yml`) and publishes a
release under **Releases** with:

- `ToneShaper-<version>-macOS.pkg`: installer (AU + VST3 in `/Library/Audio/Plug-Ins`, app in `/Applications`)
- `ToneShaper-<version>-macOS.zip`: the bundles plus `Install ToneShaper.command` for a per-user install

The builds are ad-hoc signed, not notarised. On first open, right-click the file and choose
**Open**, or use **System Settings → Privacy & Security → Open Anyway**. See
`packaging/README-macOS.txt` for details.

## Building

Requirements: CMake ≥ 3.22, a C++17 compiler, git (JUCE 8.0.9 is fetched at configure time).

```bash
# macOS universal binary (AU, VST3, Standalone)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
cmake --build build --parallel
./build/toneshaper_tests            # DSP test suite
packaging/package_macos.sh build/src/plugin/ToneShaper_artefacts/Release dist 1.0.0

# DSP core and tests only (any platform, no JUCE)
cmake -S . -B build-tests -DTONESHAPER_BUILD_PLUGIN=OFF && cmake --build build-tests
./build-tests/toneshaper_tests
```

Options: `TONESHAPER_BUILD_PLUGIN` (ON), `TONESHAPER_BUILD_TESTS` (ON),
`TONESHAPER_BUILD_DEV_TOOLS` (OFF; builds `toneshaper_ui_snapshot`, which runs test audio
through the processor and renders the editor to a PNG without a DAW).

## Layout

| Path | Contents |
|---|---|
| `src/dsp/` | Framework-independent engine (C++17, no JUCE) |
| `src/dsp/SpectralCore.*` | STFT, envelope, adaptive gain, ballistics, squash, mask, mixed-phase synthesis (spec §2–8) |
| `src/dsp/ToneShaperEngine.*` | Stereo modes, sidechain, output filters, latency-compensated mix, metering (§9) |
| `src/dsp/LogGrid.*` | Constant-Q grid all smoothing runs on |
| `src/dsp/MinPhase.*` | Cepstral minimum-phase reconstruction (§8.2) |
| `src/dsp/Mappings.h` | Parameter mappings and weighting curves (§4.4–4.6, §6.2, §7.2) |
| `src/plugin/` | JUCE processor, parameters (§10), JSON presets (§10.7), editor |
| `tests/DspTests.cpp` | Test suite (§11.9 plus latency/null, stereo, sidechain, squash, filters, loudness) |
| `third_party/pffft/` | PFFFT (Julien Pommier, BSD-style FFTPACK licence) |
| `packaging/` | `.pkg` / `.zip` packaging and install script |

## Signal flow

Input → STFT (periodic Hann, 75 % overlap, PFFFT) → power spectrum → log-frequency grid
(24 points/octave) → short detector average → 1/3-octave envelope → residual against the
envelope's own broad trend → gain = tone target − residual (scaled by amount, clamped
−24/+12 dB) → ~0.8-octave smoothing → perceptually weighted mean removal → per-point
attack/release (frequency dependent) → squash (amount > 7) → processing-range mask and channel
focus → mixed-phase synthesis → ISTFT → low/high cut (18 dB/oct Butterworth) → wet trim →
latency-compensated wet/dry mix → output gain.

Quality modes use FFT sizes of 1024 (normal), 2048 (high) and 256 (low latency), doubled at
88.2/96 kHz and quadrupled at 176.4/192 kHz. Measured CPU on one x86 core at 48 kHz, stereo
split: about 3–5 % (normal), 2–3 % (high), 6–10 % (low latency, which runs minimum-phase
synthesis on every frame).

## Where the implementation departs from the spec, and why

Each of these was needed for the plugin to behave the way the spec describes, or fixes an
internal inconsistency in it. Tests that cover the behaviour are named in brackets.

1. **Latency is one FFT length, not one hop** (1024 samples = 21.3 ms in normal mode at
   48 kHz; 256 = 5.3 ms in low latency). A windowed overlap-add frame can only be output once
   every window that overlaps it has been analysed, so a symmetric STFT cannot deliver one-hop
   latency. The reported latency is exact and the dry path is aligned to it
   [`Latency and null test`, `Mix and bypass alignment`].
2. **Periodic Hann window** (`N` in the denominator rather than `N−1`). Only the periodic form
   is exactly COLA at 75 % overlap; the processing nulls against the input to below −80 dB.
3. **Residual = envelope − the envelope's broad trend (~2.5 octaves)**, not raw magnitude −
   envelope. The raw-magnitude residual averages to ≈ 0 under the wider gain smoothing of §5.3,
   which leaves no adaptive correction at all. Comparing the envelope with its local trend is
   what §1 and §3 describe ("peaks in this envelope that stick out above the local trend")
   [`Resonance produces a local cut`, `Sloped spectrum is left alone`].
4. **The envelope is smoothed as power, after a 20 ms detector average, and the follower output
   is re-centred.** A single bin's power is exponentially distributed; averaging dB values is
   biased about 2.5 dB low where few bins are averaged (the bass), and the fast-attack /
   slow-release follower then latches onto the downward fluctuations. Without these three
   changes, white noise came out with a ~2 dB bass cut. With them it changes by < 1 dB
   [`Noise integration`].
5. **The broad-trend smoother extends the data by point reflection at the grid edges**, so a
   falling spectrum isn't read as a dip near Nyquist (which would boost the top octave).
6. **Linked stereo averages the two channels' power spectra** rather than analysing L+R, so
   out-of-phase content can't cancel out of the detector.
7. **Squash detects on 1/3-octave band level** (dBFS, a full-scale sine reads 0 dB), so the
   §7.3 thresholds mean the same at every FFT size. `squash cal` *lowers* the threshold
   (`threshold −= cal`), which is the behaviour §7.3 describes ("higher cal = more squashing")
   even though its formula adds. Upward gain is capped at +12 dB and gated below −90 dBFS so
   silence and noise floors aren't pulled up. The squash gain runs through the same
   attack/release follower so it doesn't jump frame to frame.
8. **Mask, focus and amount scaling operate on the dB curve** rather than interpolating
   linear gain, so boosts and cuts scale symmetrically.
9. **Band frequency defaults land on the §4.1 default centres** (100 / 350 / 3500 / 6300 Hz).
   The normalised value 0.5 in §10.2 maps to other frequencies through the §4.5 curve.
10. **Ranges:** attack 0–10 (1 ms – 1 s), release 0–8 (5 ms – 7.9 s), amount scale held at 1.0
    above 7 while squash takes over.
11. **Added one parameter, `band solo band`** (which band "band solo" listens to); the spec's
    `band solo` / `band solo ch` don't say which band.
12. **Gain normalisation weights** use A-weighting applied as an amplitude factor on a
    log-frequency (pink) axis: between the two options §5.4 allows, so auto-compensation is
    "slight" as §4.3 intends rather than dominated by 2–5 kHz.

## Presets

Presets are JSON in the §10.7 format with the spec's key names (`"freq 1 (main/ch1)"`,
`"level 2 (ch2)"`, …); missing keys reset to defaults. Factory presets are built in; user
presets are saved to `~/Library/Audio/Presets/Arent Todal/ToneShaper/`.
