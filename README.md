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

The panel adjusts the displayed frequency range, the dB window the colour ramp
and spectrum plot span, the noise gate, and how fast the spectrum trace falls
back. Changing the frequency range rebuilds the spectrogram, which clears the
scrollback. Hovering the spectrum plot shows the frequency and level under the
cursor; levels are relative to the raw FFT magnitude, not calibrated dBFS.
