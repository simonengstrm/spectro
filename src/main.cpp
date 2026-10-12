#include <raylib.h>

#include <csignal>
#include <iomanip>
#include <iostream>

#include "audio_engine.h"
#include "freq_analysis.h"
#include "gui.h"
#include "recorder.h"

using std::cout;

bool shutdown{false};

void signalHandler([[maybe_unused]] int signum) { shutdown = true; }

void printTuner(const NoteInfo& info, float freq) {
  int totalWidth = 50;
  std::string bar(totalWidth, '-');

  cout << bar << "\t                     \r";

  if (info.midi != -1) {
    int centerPosition = totalWidth / 2;
    int centsOffset = static_cast<int>(info.cents / 100.0f * (totalWidth / 2.0));
    int markerPosition = centerPosition + centsOffset;
    if (markerPosition < 0) markerPosition = 0;
    if (markerPosition >= totalWidth) markerPosition = totalWidth - 1;

    bar[totalWidth / 2] = '|';
    bar[markerPosition] = '*';
    cout << bar << "\t" << info.name << info.octave << "\t" << std::fixed << std::setprecision(0)
         << freq << "\r";
  }

  std::cout << std::flush;
}

int main(int argc, char* argv[]) {
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  AudioEngine engine{};
  std::string deviceName = argc > 1 ? argv[1] : "Scarlett";
  if (!engine.init(deviceName)) {
    std::cout << "Could not initialize audio engine\n";
    return -1;
  }

  // Get sample rate
  const auto sampleRate = engine.getDeviceInfo()->defaultSampleRate;
  Recorder recorder{static_cast<int>(sampleRate)};
  GUI gui{static_cast<unsigned long>(sampleRate)};
  gui.setDeviceName(engine.getDeviceInfo()->name);
  gui.setRecorder(&recorder);
  gui.initialize();

  auto callback = [&](std::array<SAMPLE, SAMPLES_PER_CALLBACK> buffer, unsigned long bufferSize,
                      int sampleRate) {
    // Record before analysis: analyzeFrame windows the buffer in place.
    recorder.append(buffer.data(), bufferSize);

    FFTData spectrum{};
    const PitchResult result = analyzeFrame(buffer.data(), bufferSize, sampleRate,
                                            PitchSettings{gui.tunerGateDb()}, spectrum);

    gui.setNewSpectrumData(std::move(spectrum));
    gui.setTunerData(result);
  };

  if (!engine.openStream()) {
    std::cout << "Could not open audio stream\n";
    return -1;
  }

  engine.setAudioCallback(callback);

  if (!engine.start()) {
    std::cout << "Could not start audio stream\n";
    return -1;
  }

  gui.mainLoop();

  if (!engine.stop()) {
    std::cout << "Could not stop audio stream\n";
    return -1;
  }
}
