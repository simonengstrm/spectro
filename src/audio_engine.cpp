#include "audio_engine.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

#include "pa_ringbuffer.h"
#include "portaudio.h"

bool AudioEngine::init(std::string deviceNameHint) {
  PaError err = Pa_Initialize();
  if (err != paNoError) {
    std::cout << "Could not initialize audio engine: " << Pa_GetErrorText(err) << std::endl;
    return false;
  }
  initialized = true;

  if (deviceIndex = AudioEngine::findDevice(deviceNameHint); deviceIndex == paNoDevice) {
    // No device matched the hint, so fall back to whatever the host API offers
    // as the default input.
    deviceIndex = Pa_GetDefaultInputDevice();
    if (deviceIndex == paNoDevice) {
      std::cout << "Could not find any input device" << std::endl;
      return false;
    }
    std::cout << "No input device matching \"" << deviceNameHint << "\", falling back to default"
              << std::endl;
  }

  const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
  std::cout << "Using input device: " << deviceInfo->name << " ("
            << Pa_GetHostApiInfo(deviceInfo->hostApi)->name << ")" << std::endl;

  if (PaUtil_InitializeRingBuffer(&ringBuffer, sizeof(SAMPLE), RING_BUFFER_SIZE,
                                  ringBufferData.data()) < 0) {
    std::cout << "Could not initialize ring buffer" << std::endl;
    return false;
  }

  return true;
}

int AudioEngine::findDevice(std::string deviceNameHint) {
  if (deviceNameHint.empty()) return paNoDevice;

  int deviceCount = Pa_GetDeviceCount();
  for (int i = 0; i < deviceCount; i++) {
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
    std::string deviceName(deviceInfo->name);
    if (deviceName.contains(deviceNameHint) && deviceInfo->maxInputChannels > 0) return i;
  }

  return paNoDevice;
}

bool AudioEngine::openStream() {
  if (inStream) {
    return true;
  }
  const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);

  // Open at most two channels; more than that only wastes conversion work since
  // we analyse a single channel.
  channelCount = std::min(deviceInfo->maxInputChannels, 2);
  // On a 2-in interface (e.g. a Scarlett 2i2) channel 0 is the mic input and
  // channel 1 the instrument input, so prefer channel 1 when it exists.
  captureChannel = channelCount >= 2 ? 1 : 0;

  inStreamParameters.device = deviceIndex;
  inStreamParameters.channelCount = channelCount;
  inStreamParameters.sampleFormat = PA_SAMPLE_TYPE;
  inStreamParameters.suggestedLatency = deviceInfo->defaultLowInputLatency;
  inStreamParameters.hostApiSpecificStreamInfo = NULL;
  std::cout << "default sample rate: " << deviceInfo->defaultSampleRate << std::endl;
  std::cout << "input channels: " << channelCount << ", analysing channel " << captureChannel
            << std::endl;

  PaError err =
      Pa_OpenStream(&inStream, &inStreamParameters, NULL, deviceInfo->defaultSampleRate,
                    SAMPLES_PER_CALLBACK, paClipOff, &AudioEngine::paRecordCallback, this);
  if (err != paNoError) {
    std::cout << Pa_GetErrorText(err) << std::endl;
    return false;
  }

  return true;
}

bool AudioEngine::start() {
  audioThread = std::jthread([this](std::stop_token stopToken) {
    while (stopToken.stop_requested() == false) {
      std::array<SAMPLE, SAMPLES_PER_CALLBACK> tempBuffer;
      if (PaUtil_GetRingBufferReadAvailable(&ringBuffer) >= SAMPLES_PER_CALLBACK) {
        PaUtil_ReadRingBuffer(&ringBuffer, tempBuffer.data(), SAMPLES_PER_CALLBACK);
        if (audioCallback) {
          audioCallback(tempBuffer, SAMPLES_PER_CALLBACK,
                        static_cast<int>(getDeviceInfo()->defaultSampleRate));
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
  });

  return inStream && Pa_StartStream(inStream) == paNoError;
}

bool AudioEngine::stop() {
  if (audioThread.joinable()) {
    audioThread.request_stop();
    audioThread.join();
  }
  return inStream && Pa_StopStream(inStream) == paNoError;
}

bool AudioEngine::isActive() { return inStream && Pa_IsStreamActive(inStream) == 1; }

// Picks a single channel out of the interleaved input and hands it to the ring
// buffer. See openStream() for how that channel is chosen.
int AudioEngine::paRecordCallback(const void* inputBuffer, [[maybe_unused]] void* outputBuffer,
                                  unsigned long framesPerBuffer,
                                  [[maybe_unused]] const PaStreamCallbackTimeInfo* timeInfo,
                                  [[maybe_unused]] PaStreamCallbackFlags statusFlags,
                                  void* userData) {
  AudioEngine* self = static_cast<AudioEngine*>(userData);
  const unsigned long frames = std::min<unsigned long>(framesPerBuffer, SAMPLES_PER_CALLBACK);
  SAMPLE* wptr = self->captureScratch.data();

  if (inputBuffer == NULL) {
    // Dropped input: feed silence so the analysis thread keeps its cadence.
    std::fill_n(wptr, frames, 0.0f);
  } else {
    const SAMPLE* rptr = static_cast<const SAMPLE*>(inputBuffer);
    for (unsigned long i = 0; i < frames; i++) {
      wptr[i] = rptr[i * self->channelCount + self->captureChannel];
    }
  }

  PaUtil_WriteRingBuffer(self->getRingBuffer(), self->captureScratch.data(),
                         static_cast<ring_buffer_size_t>(frames));

  return paContinue;
}

AudioEngine::~AudioEngine() {
  if (inStream) {
    Pa_StopStream(inStream);
    Pa_CloseStream(inStream);
  }
  if (initialized) {
    Pa_Terminate();
  }
}
