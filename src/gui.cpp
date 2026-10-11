#include "gui.h"

#include <algorithm>
#include <string>

#include "raylib.h"

void GUI::initialize() {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(GUI_WIDTH, GUI_HEIGHT, "Visualizer");
  SetTargetFPS(60);
}

void GUI::UpdateSpectrogramData() {
  if (!newSpectrumAvailable.exchange(false, std::memory_order_acquire)) {
    return;  // No new data available
  }

  std::vector<float> magnitudes;

  const size_t fftSize = frontSpectrum.size();
  const size_t halfSize = fftSize / 2;
  magnitudes.resize(halfSize);

  for (unsigned long i = 0; i < halfSize; ++i) {
    float magnitude = std::abs(frontSpectrum[i]);
    float dbMagnitude = 20.0f * log10f(magnitude + 1e-6f);
    magnitudes[i] = dbMagnitude;
  }

  if (spectrogramHistory.size() >= maxHistorySize) {
    spectrogramHistory.erase(spectrogramHistory.begin());
  }

  spectrogramHistory.push_back(magnitudes);
}

void GUI::DrawGridLines() {
  std::vector<float> octaveFreqs = {55.0f, 110.0f, 220.0f, 440.0f, 880.0f, 1760.0f, 3520.0f};
  const float logMin = log10f(min_f);
  const float logMax = log10f(max_f);

  for (float freq : octaveFreqs) {
    if (freq < min_f || freq > max_f) continue;

    float yNorm = (log10f(freq) - logMin) / (logMax - logMin);
    float y = spectrogramHeight - (yNorm * spectrogramHeight);

    DrawLine(widthMargins / 2, (int)y, spectrogramWidth + widthMargins / 2, (int)y, GRAY);

    // Optional label
    DrawText(TextFormat("%.1f Hz", freq), widthMargins / 2 - 30, (int)y - 5, 10, LIGHTGRAY);
  }
}

void GUI::DrawSpectrogram() {
  const float xStep = (float)(spectrogramWidth) / (float)maxHistorySize;
  for (size_t t = 0; t < spectrogramHistory.size(); ++t) {
    const auto& row = spectrogramHistory[t];
    float x = t * xStep;

    const size_t halfSize = row.size();
    const float fftSize = halfSize * 2.0f;

    for (size_t bin = 1; bin < halfSize - 1; ++bin) {
      float freq = bin * sampleRate / fftSize;
      float nextFreq = (bin + 1) * sampleRate / fftSize;

      if (freq < min_f || freq > max_f) continue;

      // Map to log
      float yNorm = (log10f(freq) - logMin) / (logMax - logMin);
      float yNextNorm = (log10f(nextFreq) - logMin) / (logMax - logMin);

      if (yNorm < 0.0f || yNorm > 1.0f) continue;

      float y = spectrogramHeight - (yNorm * spectrogramHeight);
      float yNext = spectrogramHeight - (yNextNorm * spectrogramHeight);

      float rectHeight = std::abs(yNext - y);
      if (rectHeight < 1.0f) rectHeight = 1.0f;

      // Color calc
      float db = row[bin];
      float intensity = (db + 100.0f) / 100.0f;
      intensity = std::clamp(intensity, 0.0f, 1.0f);

      Color c = ColorFromHSV(240.0f - intensity * 240.0f, 1.0f, intensity);
      if (intensity > 0.15f) {
        DrawRectangle((int)x + widthMargins / 2, (int)yNext, (int)xStep + 1, (int)rectHeight + 1,
                      c);
      }
    }
  }
}

void GUI::DrawTuner() {
  std::string noteText = "Note: " + currentNote.name + std::to_string(currentNote.octave);
  DrawText(noteText.c_str(), widthMargins / 2, spectrogramHeight + heightMargins / 2 + 10, 20,
           LIGHTGRAY);

  // Draw tuning bar
  DrawLine(widthMargins / 2, spectrogramHeight + heightMargins / 2, tunerWidth + widthMargins / 2,
           spectrogramHeight + heightMargins / 2, GRAY);

  int centerX = tunerWidth / 2;
  int centsOffset = static_cast<int>(currentNote.cents / 100.0f * (tunerWidth / 2.0));
  int markerPosition = centerX + centsOffset;

  DrawLine(widthMargins / 2 + centerX, spectrogramHeight + heightMargins / 2 - 10,
           widthMargins / 2 + centerX, spectrogramHeight + heightMargins / 2 + 10, LIGHTGRAY);

  bool inTune = std::abs(currentNote.cents) < 5.0f;  // Within 5 cents is considered in tune
  DrawLine(widthMargins / 2 + markerPosition, spectrogramHeight + heightMargins / 2 - 15,
           widthMargins / 2 + markerPosition, spectrogramHeight + heightMargins / 2 + 15,
           inTune ? GREEN : RED);
}

void GUI::mainLoop() {
  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(BLACK);
    UpdateSpectrogramData();

    DrawSpectrogram();
    DrawGridLines();
    DrawTuner();

    EndDrawing();
  }
}