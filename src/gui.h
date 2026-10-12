#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "raylib.h"

#include "freq_analysis.h"

class Recorder;

class GUI {
 public:
  static constexpr unsigned GUI_WIDTH{1000};
  static constexpr unsigned GUI_HEIGHT{720};

  GUI(unsigned long sampleRate) : sampleRate(sampleRate) {}
  ~GUI() = default;

  void initialize();

  void mainLoop();

  void setDeviceName(std::string name) { deviceName = std::move(name); }

  // Optional; enables the record control. Must outlive the GUI.
  void setRecorder(Recorder* r) { recorder = r; }

  // Read by the analysis thread for every frame.
  float tunerGateDb() const { return gateDb.load(std::memory_order_relaxed); }

  void setNewSpectrumData(FFTData&& newSpectrum) {
    if (newSpectrumAvailable.load(std::memory_order_acquire)) {
      return;  // Previous data not yet consumed
    }
    backSpectrum = std::move(newSpectrum);
    std::swap(frontSpectrum, backSpectrum);
    newSpectrumAvailable.store(true, std::memory_order_release);
  }

  void setTunerData(const PitchResult& result) {
    // Only the plain numbers cross the thread boundary; the note name is
    // derived from the midi number when drawing. A gated frame has midi -1.
    tuner.store(Tuner{result.note.midi, result.note.cents, result.frequency, result.levelDb},
                std::memory_order_release);
  }

 private:
  // Where each part of the window lands, recomputed every frame so the
  // layout follows the window instead of fixed pixel constants.
  struct Layout {
    Rectangle header;
    Rectangle spectrum;
    Rectangle spectrogram;
    Rectangle tuner;
  };

  // The bins of one pixel row or column, [begin, end).
  struct BinRange {
    unsigned begin;
    unsigned end;
  };

  // Tuner readout as a trivially copyable value, so the audio thread can
  // publish it without the GUI thread ever touching a half-written string.
  struct Tuner {
    int midi;
    float cents;
    float freq;
    float levelDb;
  };

  Layout computeLayout() const;
  BinRange logSliceBins(unsigned index, unsigned count) const;
  float peakDb(BinRange range) const;

  void rebuildSpectrogram(unsigned height);
  void rebuildSpectrum(unsigned width);
  void consumeSpectrum();
  void writeSpectrogramColumn();
  void stepSpectrum(float dt);

  void drawHeader(const Layout& layout) const;
  void drawSpectrum(const Layout& layout) const;
  void drawSpectrogram(const Layout& layout) const;
  void drawFrequencyAxis(const Layout& layout) const;
  void drawTuner(const Layout& layout) const;
  void drawControls();
  void toggleRecording();

  // Double buffered spectrum data to avoid locking during drawing
  FFTData backSpectrum{};
  FFTData frontSpectrum{};
  std::atomic<bool> newSpectrumAvailable{false};

  unsigned long sampleRate;
  std::string deviceName{"unknown input"};
  std::atomic<Tuner> tuner{Tuner{-1, 0.0f, 0.0f, -120.0f}};
  std::atomic<float> gateDb{PitchSettings{}.gateDb};

  Recorder* recorder{nullptr};
  std::string lastTake;     // Path of the most recently saved take
  double lastTakeTime{-1};  // GetTime() when it was saved

  // How many spectra the scrollback holds. Columns arrive at the FFT hop
  // rate, so this is time, not pixels.
  static constexpr unsigned historyColumns = 200;

  // The scrollback lives in a texture used as a ring buffer: a new spectrum
  // writes one column, and drawing it costs two textured quads.
  std::vector<BinRange> binRanges;  // One per pixel row, topmost row first
  std::vector<Color> columnPixels;  // Scratch for the column being written
  Texture2D spectrogramTexture{};
  unsigned textureHeight{0};
  unsigned writeColumn{0};
  bool rangeDirty{false};

  // Live spectrum plot, one entry per pixel column, lowest frequency first.
  // The target jumps to each new spectrum; the displayed level rises with it
  // immediately and falls back at spectrumRelease, so the trace reads as
  // motion instead of flicker.
  std::vector<BinRange> spectrumBins;
  std::vector<float> spectrumTarget;
  std::vector<float> spectrumDisplay;

  // Live controls
  float minFreq{70.0f};
  float maxFreq{4000.0f};
  float dbFloor{-100.0f};
  float dbCeiling{0.0f};
  float gate{0.15f};
  float spectrumRelease{48.0f};
  bool showGrid{true};
  bool showSpectrum{true};
  bool showControls{false};
};
