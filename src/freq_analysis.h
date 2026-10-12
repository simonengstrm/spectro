#pragma once

#include <array>
#include <complex>
#include <string>

// Samples per analysis frame. The audio engine delivers frames of this size,
// and the test harness cuts files into the same frames.
constexpr unsigned long analysisFrameSize = 4096;

constexpr unsigned long paddedSize = 16384;
using FFTData = std::array<std::complex<float>, paddedSize>;

void hannWindow(float* buffer, unsigned long bufferSize);

void fft(const float* buffer, unsigned long bufferSize, FFTData& output);

float findPeakFrequency(const FFTData& fftData, int sampleRate);

void harmonicProductSpectrum(FFTData& fftData, unsigned long factor);

float findMaxAmplitude(const float* buffer, unsigned long bufferSize);

// RMS level of a frame in dBFS, floored at -120.
float rmsDb(const float* buffer, unsigned long bufferSize);

struct NoteInfo {
  std::string name;
  int octave = 0;
  float cents = 0.0f;
  float noteFreq = 0.0f;
  int midi = -1;
  float inputFreq = 0.0f;
};
NoteInfo freqToNote(float frequency);

struct PitchSettings {
  // Frames whose RMS level is below this, in dBFS, report no pitch.
  float gateDb = -50.0f;
};

struct PitchResult {
  float levelDb = -120.0f;  // RMS level of the frame before windowing
  bool gated = true;
  float frequency = 0.0f;   // 0 when gated
  NoteInfo note{};          // midi is -1 when gated
};

// The tuner pipeline for one frame: level check, Hann window, FFT, peak pick.
// The app and the test harness both go through here, so a replayed recording
// is analysed by exactly the code a live input is. Windows `frame` in place,
// and leaves the spectrum in `spectrum` whether or not the frame is gated.
PitchResult analyzeFrame(float* frame, unsigned long frameSize, int sampleRate,
                         const PitchSettings& settings, FFTData& spectrum);
