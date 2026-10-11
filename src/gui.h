#pragma once

#include <atomic>
#include <cmath>
#include <vector>

#include "raylib.h"

#include "freq_analysis.h"

class GUI {
 public:
  static constexpr unsigned GUI_WIDTH{800};
  static constexpr unsigned GUI_HEIGHT{600};

  GUI(unsigned long sampleRate) : sampleRate(sampleRate) {}
  ~GUI() = default;

  void initialize();

  void mainLoop();

  void setNewSpectrumData(FFTData&& newSpectrum) {
    if (newSpectrumAvailable.load(std::memory_order_acquire)) {
      return;  // Previous data not yet consumed
    }
    backSpectrum = std::move(newSpectrum);
    std::swap(frontSpectrum, backSpectrum);
    newSpectrumAvailable.store(true, std::memory_order_release);
  }

  void setTunerData(const NoteInfo& note) { currentNote = note; }

 private:
  void BuildBinRanges();
  void UpdateSpectrogramData();
  void DrawSpectrogram();
  void DrawGridLines();
  void DrawTuner();

  // Double buffered spectrum data to avoid locking during drawing
  FFTData backSpectrum{};
  FFTData frontSpectrum{};
  std::atomic<bool> newSpectrumAvailable{false};

  unsigned long sampleRate;
  NoteInfo currentNote{};

  // Scrolling spectrogram visualization data
  static constexpr unsigned maxHistorySize = 150;
  static inline constexpr float min_f = 70.0f;
  static inline constexpr float max_f = 4000.0f;
  static inline const float logMin = log10f(min_f);
  static inline const float logMax = log10f(max_f);

  static constexpr unsigned spectrogramHeight = GUI_HEIGHT * 0.8;
  static constexpr unsigned spectrogramWidth = GUI_WIDTH * 0.9;
  static constexpr unsigned widthMargins = GUI_WIDTH - spectrogramWidth;
  static constexpr unsigned heightMargins = GUI_HEIGHT - spectrogramHeight;

  // Under the spectrogram
  static constexpr unsigned tunerHeight = GUI_HEIGHT * 0.2;
  static constexpr unsigned tunerWidth = GUI_WIDTH * 0.9;

  // The spectrogram history is kept in a texture used as a ring buffer: a new
  // spectrum writes a single column, which is all that gets uploaded, and
  // drawing it costs two textured quads instead of one rectangle per bin.
  struct BinRange {
    unsigned begin;
    unsigned end;
  };
  std::vector<BinRange> binRanges;  // One per pixel row, topmost row first
  std::vector<Color> columnPixels;  // Scratch for the column being written
  Texture2D spectrogramTexture{};
  unsigned writeColumn{0};
};
