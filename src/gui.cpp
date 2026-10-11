#include "gui.h"

#include <algorithm>
#include <string>

#include "raylib.h"

namespace {
// Maps a dB magnitude onto the spectrogram colour ramp. Anything under the
// noise floor becomes background so quiet bins stay invisible.
Color magnitudeToColor(float db) {
  float intensity = std::clamp((db + 100.0f) / 100.0f, 0.0f, 1.0f);
  if (intensity <= 0.15f) return BLACK;
  return ColorFromHSV(240.0f - intensity * 240.0f, 1.0f, intensity);
}
}  // namespace

void GUI::initialize() {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(GUI_WIDTH, GUI_HEIGHT, "Visualizer");
  SetTargetFPS(60);

  BuildBinRanges();
  columnPixels.resize(spectrogramHeight);

  Image blank = GenImageColor(maxHistorySize, spectrogramHeight, BLACK);
  spectrogramTexture = LoadTextureFromImage(blank);
  UnloadImage(blank);
}

// Works out which FFT bins land on each pixel row once, so adding a column
// costs one pass over the rows rather than one pass over every bin.
void GUI::BuildBinRanges() {
  const unsigned long lastBin = paddedSize / 2 - 1;
  const float binsPerHz = static_cast<float>(paddedSize) / static_cast<float>(sampleRate);

  binRanges.resize(spectrogramHeight);
  for (unsigned row = 0; row < spectrogramHeight; ++row) {
    // Row 0 is the top of the spectrogram and carries the highest frequency.
    const float normTop = static_cast<float>(spectrogramHeight - row) / spectrogramHeight;
    const float normBottom = static_cast<float>(spectrogramHeight - row - 1) / spectrogramHeight;

    const float freqTop = std::pow(10.0f, logMin + normTop * (logMax - logMin));
    const float freqBottom = std::pow(10.0f, logMin + normBottom * (logMax - logMin));

    unsigned long begin = static_cast<unsigned long>(freqBottom * binsPerHz);
    unsigned long end = static_cast<unsigned long>(std::ceil(freqTop * binsPerHz));

    // Below roughly 500 Hz a row is narrower than one bin, so the range is
    // widened to keep at least one bin per row.
    begin = std::clamp(begin, 1UL, lastBin);
    end = std::clamp(end, begin + 1, lastBin + 1);

    binRanges[row] = {static_cast<unsigned>(begin), static_cast<unsigned>(end)};
  }
}

void GUI::UpdateSpectrogramData() {
  if (!newSpectrumAvailable.exchange(false, std::memory_order_acquire)) {
    return;  // No new data available
  }

  for (unsigned row = 0; row < spectrogramHeight; ++row) {
    const BinRange range = binRanges[row];

    // Peak over the bins on this row, so a narrow partial survives the
    // downscale instead of being overwritten by whichever bin is drawn last.
    float peak = 0.0f;
    for (unsigned bin = range.begin; bin < range.end; ++bin) {
      peak = std::max(peak, std::abs(frontSpectrum[bin]));
    }

    columnPixels[row] = magnitudeToColor(20.0f * log10f(peak + 1e-6f));
  }

  UpdateTextureRec(spectrogramTexture,
                   Rectangle{static_cast<float>(writeColumn), 0.0f, 1.0f,
                             static_cast<float>(spectrogramHeight)},
                   columnPixels.data());

  writeColumn = (writeColumn + 1) % maxHistorySize;
}

void GUI::DrawGridLines() {
  std::vector<float> octaveFreqs = {55.0f, 110.0f, 220.0f, 440.0f, 880.0f, 1760.0f, 3520.0f};

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
  const float left = widthMargins / 2.0f;
  const float height = static_cast<float>(spectrogramHeight);
  const float columnWidth = static_cast<float>(spectrogramWidth) / maxHistorySize;

  // writeColumn is where the next column lands, which makes it the oldest one.
  const float oldest = static_cast<float>(writeColumn);
  const float oldestCount = static_cast<float>(maxHistorySize) - oldest;

  // Draw the older block and then the wrapped newer one, so time still runs
  // left to right across the seam.
  DrawTexturePro(spectrogramTexture, Rectangle{oldest, 0.0f, oldestCount, height},
                 Rectangle{left, 0.0f, oldestCount * columnWidth, height}, Vector2{0.0f, 0.0f},
                 0.0f, WHITE);

  if (writeColumn > 0) {
    DrawTexturePro(spectrogramTexture, Rectangle{0.0f, 0.0f, oldest, height},
                   Rectangle{left + oldestCount * columnWidth, 0.0f, oldest * columnWidth, height},
                   Vector2{0.0f, 0.0f}, 0.0f, WHITE);
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
    UpdateSpectrogramData();

    BeginDrawing();
    ClearBackground(BLACK);

    DrawSpectrogram();
    DrawGridLines();
    DrawTuner();

    EndDrawing();
  }

  UnloadTexture(spectrogramTexture);
  CloseWindow();
}
