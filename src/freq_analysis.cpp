#include "freq_analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

void hannWindow(float* buffer, unsigned long bufferSize) {
  for (unsigned long i = 0; i < bufferSize; ++i) {
    buffer[i] = static_cast<float>(buffer[i] *
                                   (0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / (bufferSize - 1))));
  }
}

void fft(const float* buffer, unsigned long bufferSize, FFTData& output) {
  for (unsigned long i = 0; i < output.size(); ++i) {
    if (i < bufferSize) {
      output[i] = std::complex<float>(buffer[i], 0.0f);
    } else {
      output[i] = std::complex<float>(0.0f, 0.0f);  // Zero padding
    }
  }

  const int n = static_cast<int>(output.size());
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }

    j ^= bit;
    if (i < j) {
      std::swap(output[i], output[j]);
    }
  }

  for (int len = 2; len <= n; len <<= 1) {
    const float angle = static_cast<float>(-2 * std::numbers::pi / len);
    std::complex<float> wlen(cos(angle), sin(angle));

    for (int i = 0; i < n; i += len) {
      std::complex<float> w(1);
      for (int j = 0; j < len / 2; j++) {
        std::complex<float> u = output[i + j];
        std::complex<float> v = output[i + j + len / 2] * w;
        output[i + j] = u + v;
        output[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

void harmonicProductSpectrum(FFTData& fftData, unsigned long factor) {
  const unsigned long size = static_cast<unsigned long>(fftData.size());
  for (unsigned long i = 0; i < size / factor; ++i) {
    for (unsigned long j = 2; j <= factor; ++j) {
      fftData[i] *= fftData[i * j];
    }
  }
}

float findPeakFrequency(const FFTData& fftData, int sampleRate) {
  const unsigned long bufferSize = static_cast<unsigned long>(fftData.size());
  float maxMagnitude = 0.0f;
  unsigned long peakIndex = 0;

  for (unsigned long i = 0; i < fftData.size(); ++i) {
    float magnitude = std::abs(fftData[i]);
    if (magnitude > maxMagnitude) {
      maxMagnitude = magnitude;
      peakIndex = i;
    }
  }

  // Neighboring magnitudes
  float magL = (peakIndex > 0) ? std::abs(fftData[peakIndex - 1]) : 0.0f;
  float magC = std::abs(fftData[peakIndex]);
  float magR = (peakIndex < bufferSize / 2 - 1) ? std::abs(fftData[peakIndex + 1]) : 0.0f;

  // Interpolation to find a more accurate peak
  float delta = 0.5f * (magL - magR) / (magL - 2 * magC + magR);

  float frequency = (peakIndex + delta) * sampleRate / fftData.size();

  return frequency;
}

float findMaxAmplitude(const float* buffer, unsigned long bufferSize) {
  float maxAmplitude = 0.0f;
  for (unsigned long i = 0; i < bufferSize; ++i) {
    maxAmplitude = std::max(maxAmplitude, std::abs(buffer[i]));
  }
  return maxAmplitude;
}

float rmsDb(const float* buffer, unsigned long bufferSize) {
  double sum = 0.0;
  for (unsigned long i = 0; i < bufferSize; ++i) {
    sum += static_cast<double>(buffer[i]) * buffer[i];
  }
  const double rms = std::sqrt(sum / std::max(1UL, bufferSize));
  return static_cast<float>(std::max(-120.0, 20.0 * std::log10(rms + 1e-12)));
}

PitchResult analyzeFrame(float* frame, unsigned long frameSize, int sampleRate,
                         const PitchSettings& settings, FFTData& spectrum) {
  PitchResult result;
  result.levelDb = rmsDb(frame, frameSize);

  hannWindow(frame, frameSize);
  fft(frame, frameSize, spectrum);

  // Below the gate there is nothing worth naming; the spectrum is still
  // computed above because the display wants it regardless.
  result.gated = result.levelDb < settings.gateDb;
  if (result.gated) return result;

  result.frequency = findPeakFrequency(spectrum, sampleRate);
  result.note = freqToNote(result.frequency);
  return result;
}

NoteInfo freqToNote(float f) {
  constexpr std::array<std::string_view, 12> NAMES = {"C",  "C#", "D",  "D#", "E",  "F",
                                                      "F#", "G",  "G#", "A",  "A#", "B"};

  if (f <= 0.0f) {
    return {"", 0, 0.0f, 0.0f, -1, f};
  }

  float m = 69.0f + 12.0f * std::log2(f / 440.0f);
  if (!std::isfinite(m)) {
    return {"", 0, 0.0f, 0.0f, -1, f};
  }

  int midi = (int)std::lround(m);
  midi = std::clamp(midi, 0, 127);  // MIDI standard range

  int idx = midi % 12;
  int oct = midi / 12 - 1;

  float noteFreq = 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
  float cents = 1200.0f * std::log2(f / noteFreq);

  return {std::string(NAMES[idx]), oct, cents, noteFreq, midi, f};
}
