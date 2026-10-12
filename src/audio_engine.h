#pragma once
#include <array>
#include <functional>
#include <string>
#include <thread>

#include "freq_analysis.h"
#include "pa_ringbuffer.h"
#include "portaudio.h"

constexpr unsigned RING_BUFFER_SIZE{16384};
// One callback delivers one analysis frame, so the size lives with the analysis.
constexpr unsigned SAMPLES_PER_CALLBACK{analysisFrameSize};
constexpr unsigned PA_SAMPLE_TYPE{paFloat32};

using SAMPLE = float;
using audioCallback_t =
    std::function<void(std::array<SAMPLE, SAMPLES_PER_CALLBACK> buffer, unsigned long, int)>;

struct AudioEngine {
  bool init(std::string deviceNameHint);

  static int findDevice(std::string deviceNameHint);

  bool openStream();

  bool start();

  bool stop();

  bool isActive();

  const PaDeviceInfo* getDeviceInfo() { return Pa_GetDeviceInfo(deviceIndex); }

  PaUtilRingBuffer* getRingBuffer() { return &ringBuffer; }

  void setAudioCallback(audioCallback_t callback) { audioCallback = callback; }

  ~AudioEngine();

 private:
  bool initialized = false;
  int deviceIndex = -1;
  // Channel layout of the opened stream, and which of its channels we capture
  int channelCount = 0;
  int captureChannel = 0;
  PaStream* inStream{nullptr};
  PaStreamParameters inStreamParameters{};
  std::array<SAMPLE, RING_BUFFER_SIZE> ringBufferData{};
  // Scratch space for de-interleaving inside the PortAudio callback. Owned by
  // the engine so the callback never allocates.
  std::array<SAMPLE, SAMPLES_PER_CALLBACK> captureScratch{};
  std::jthread audioThread;
  audioCallback_t audioCallback;
  PaUtilRingBuffer ringBuffer{};

  static int paRecordCallback(const void* inputBuffer, void* outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags, void* userData);
};
