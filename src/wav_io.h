#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct WavData {
  int sampleRate = 0;
  int channels = 0;
  std::vector<float> samples;  // Interleaved, scaled to [-1, 1]
};

// Reads PCM (16, 24 or 32-bit) and IEEE float (32 or 64-bit) WAV files,
// including WAVE_FORMAT_EXTENSIBLE. On failure returns false and says why.
bool readWav(const std::string& path, WavData& out, std::string& error);

// Writes mono 32-bit float, so a recorded take replays bit for bit. raylib's
// own loader converts WAV to 16-bit, which is why this exists at all.
bool writeWavMono(const std::string& path, const float* samples, std::size_t count,
                  int sampleRate);
