# Spectro
Pitch detection and real-time spectrogram visualization using C++, PortAudio, and Raylib.
Made it because my tuner broke and I needed a replacement.

![Untitled](https://github.com/user-attachments/assets/01c78a38-ac04-4078-a3c0-0ae75fb80102)

## Building

Requires CMake 3.24+, a C++23 compiler and Git. PortAudio comes from the
submodule; raylib is used from the system if installed and downloaded otherwise.
Dear ImGui and rlImGui are fetched at configure time.

```sh
git submodule update --init --recursive
cmake -B build
cmake --build build --config RelWithDebInfo
```

The executable lands in `build/` (`build/PitchDetector.exe` on Windows).

### Windows

Install the Visual Studio 2022 Build Tools with the "Desktop development with
C++" workload (`winget install Microsoft.VisualStudio.2022.BuildTools`). The
default Visual Studio generator picks the toolchain up on its own, so the
commands above work from any shell. For the Ninja generator instead, run them
from a *Developer Command Prompt for VS 2022*:

```sh
cmake -B build -G Ninja
cmake --build build
```

Builds with WMME, DirectSound, WASAPI and WDM/KS host APIs; ASIO is off by
default and needs the Steinberg SDK (`-DPA_USE_ASIO=ON`).

## Running

```sh
./build/PitchDetector [device name hint]
```

The argument is a substring matched against the input device names PortAudio
reports; it defaults to `Scarlett`. If nothing matches, the system default input
device is used. On a device with two or more inputs channel 1 is analysed
(instrument input on a Scarlett 2i2), otherwise channel 0.

### Controls

| key | action |
| --- | --- |
| `tab` | show/hide the controls panel |
| `g` | toggle grid lines |
| `s` | show/hide the spectrum plot |
| `r` | start/stop recording a take |

The panel adjusts the displayed frequency range, the dB window the colour ramp
and spectrum plot span, the display gate, and how fast the spectrum trace falls
back. Changing the frequency range rebuilds the spectrogram, which clears the
scrollback. Hovering the spectrum plot shows the frequency and level under the
cursor; levels are relative to the raw FFT magnitude, not calibrated dBFS.

The tuner has its own noise gate (`gate dBFS` in the panel, -50 by default):
frames whose RMS level is below it show `--` instead of a note. The meter in the
tuner strip shows the input level against the gate, marked in amber.

## Testing the pitch detector

`r` records exactly the frames the tuner analyses to
`recordings/take-<timestamp>.wav` (32-bit float, next to wherever the app was
started). `pitch_test` replays a file through the same `analyzeFrame()` the app
calls, frame for frame, and shows what the tuner would have read and why:

```sh
./build/pitch_test recordings/take-20261012-200234.wav --expect E2
```

Each frame lists the detected note, whether it was right, and the strongest
spectral peaks, so a misreading shows which partial won: `x2` means the 2nd
harmonic (an octave high), `x3` the 3rd (an octave and a fifth). Other options:
`--channel` for multichannel files, `--gate` to replay with a different gate,
`--peaks` for more or fewer peaks.

The synthetic suite runs known signals (sines, and plucked strings modelled as
heard through bridge and neck pickups) and fails on any case that misses its
threshold. It is registered with CTest:

```sh
ctest --test-dir build --output-on-failure
```

`pitch_test --synth --verbose` shows failing frames; `--write-synth <dir>`
writes the suite's signals out as WAV to listen to or replay.
