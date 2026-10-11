#include "gui.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "imgui.h"
#include "rlImGui.h"

namespace {

// A restrained terminal-ish palette: near-black ground, hairline rules, and
// one accent colour that is only ever used to mean "in tune".
constexpr Color COL_BG{0x0B, 0x0E, 0x14, 255};
constexpr Color COL_LINE{0x22, 0x2B, 0x3A, 255};
constexpr Color COL_LINE_BRIGHT{0x3A, 0x46, 0x5A, 255};
constexpr Color COL_GRID{0xFF, 0xFF, 0xFF, 16};
constexpr Color COL_DIM{0x5A, 0x66, 0x78, 255};
constexpr Color COL_TEXT{0xC3, 0xCB, 0xD9, 255};
constexpr Color COL_ACCENT{0x6F, 0xD6, 0xB4, 255};
constexpr Color COL_WARN{0xD9, 0xA2, 0x4A, 255};
constexpr Color COL_BAD{0xE0, 0x6C, 0x75, 255};

// The spectrum trace borrows the teal from the middle of the spectrogram
// ramp, so the two views read as the same data.
constexpr Color COL_TRACE{0x4F, 0xA8, 0x97, 255};
constexpr Color COL_TRACE_FILL{0x4F, 0xA8, 0x97, 34};

constexpr const char* NOTE_NAMES[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                        "F#", "G",  "G#", "A",  "A#", "B"};

// Spectrogram ramp: silence sits on the background colour and climbs through
// blue and teal to a warm highlight, so loud partials read as warm lines.
struct Stop {
  float t;
  unsigned char r, g, b;
};
constexpr Stop RAMP_STOPS[] = {
    {0.00f, 0x0B, 0x0E, 0x14}, {0.20f, 0x16, 0x2A, 0x42}, {0.42f, 0x1F, 0x63, 0x7D},
    {0.62f, 0x4F, 0xA8, 0x97}, {0.80f, 0xD2, 0xA2, 0x4A}, {1.00f, 0xF7, 0xF2, 0xE7},
};

const std::array<Color, 256>& colorRamp() {
  static const std::array<Color, 256> ramp = [] {
    std::array<Color, 256> out{};
    for (int i = 0; i < 256; ++i) {
      const float t = i / 255.0f;
      size_t s = 0;
      while (s + 2 < std::size(RAMP_STOPS) && t > RAMP_STOPS[s + 1].t) ++s;

      const Stop& a = RAMP_STOPS[s];
      const Stop& b = RAMP_STOPS[s + 1];
      const float k = std::clamp((t - a.t) / (b.t - a.t), 0.0f, 1.0f);

      out[i] = Color{static_cast<unsigned char>(a.r + (b.r - a.r) * k),
                     static_cast<unsigned char>(a.g + (b.g - a.g) * k),
                     static_cast<unsigned char>(a.b + (b.b - a.b) * k), 255};
    }
    return out;
  }();
  return ramp;
}

// How far off pitch a note is, as the colour the tuner uses for it.
Color tuneColor(float cents) {
  const float off = std::abs(cents);
  return off < 5.0f ? COL_ACCENT : (off < 20.0f ? COL_WARN : COL_BAD);
}

const char* frequencyLabel(float freq) {
  return freq >= 1000.0f ? TextFormat("%.1fk", static_cast<double>(freq) / 1000.0)
                         : TextFormat("%.0f", static_cast<double>(freq));
}

void drawTextRight(const char* text, float right, float y, int size, Color color) {
  DrawText(text, static_cast<int>(right) - MeasureText(text, size), static_cast<int>(y), size,
           color);
}

void drawTextCentered(const char* text, float centerX, float y, int size, Color color) {
  DrawText(text, static_cast<int>(centerX) - MeasureText(text, size) / 2, static_cast<int>(y),
           size, color);
}

ImVec4 toImVec4(Color c) {
  return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

// Square corners and hairline borders, to match the rest of the window.
void styleImGui() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.FrameRounding = 0.0f;
  style.GrabRounding = 0.0f;
  style.ScrollbarRounding = 0.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.WindowPadding = ImVec2(12.0f, 12.0f);
  style.ItemSpacing = ImVec2(8.0f, 7.0f);
  style.GrabMinSize = 9.0f;

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_WindowBg] = toImVec4(Color{0x0D, 0x11, 0x18, 242});
  colors[ImGuiCol_Border] = toImVec4(COL_LINE);
  colors[ImGuiCol_Text] = toImVec4(COL_TEXT);
  colors[ImGuiCol_TextDisabled] = toImVec4(COL_DIM);
  colors[ImGuiCol_FrameBg] = toImVec4(Color{0x14, 0x1A, 0x24, 255});
  colors[ImGuiCol_FrameBgHovered] = toImVec4(Color{0x1B, 0x23, 0x30, 255});
  colors[ImGuiCol_FrameBgActive] = toImVec4(Color{0x20, 0x2A, 0x3A, 255});
  colors[ImGuiCol_TitleBg] = toImVec4(Color{0x0D, 0x11, 0x18, 255});
  colors[ImGuiCol_TitleBgActive] = toImVec4(Color{0x11, 0x17, 0x20, 255});
  colors[ImGuiCol_SliderGrab] = toImVec4(COL_ACCENT);
  colors[ImGuiCol_SliderGrabActive] = toImVec4(COL_ACCENT);
  colors[ImGuiCol_CheckMark] = toImVec4(COL_ACCENT);
  colors[ImGuiCol_Separator] = toImVec4(COL_LINE);
  colors[ImGuiCol_Header] = toImVec4(Color{0x18, 0x20, 0x2C, 255});
}

}  // namespace

void GUI::initialize() {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(GUI_WIDTH, GUI_HEIGHT, "spectro");
  SetWindowMinSize(560, 480);
  SetTargetFPS(60);

  rlImGuiSetup(true);
  styleImGui();
}

GUI::Layout GUI::computeLayout() const {
  const float width = static_cast<float>(GetScreenWidth());
  const float height = static_cast<float>(GetScreenHeight());

  constexpr float pad = 16.0f;
  constexpr float headerHeight = 22.0f;
  constexpr float tunerHeight = 104.0f;
  constexpr float axisWidth = 44.0f;
  constexpr float spacing = 12.0f;

  const float top = pad + headerHeight + spacing;
  const float bottom = height - pad - tunerHeight;
  const float plotX = pad + axisWidth;
  const float plotWidth = std::max(1.0f, width - 2 * pad - axisWidth);

  Layout layout{};
  layout.header = {pad, pad, std::max(1.0f, width - 2 * pad), headerHeight};

  // The spectrum plot takes a share of the middle band and the spectrogram
  // gets the rest. Both share the gutter for their axis labels.
  float y = top;
  if (showSpectrum) {
    const float plotHeight = std::clamp((bottom - top - spacing) * 0.28f, 80.0f, 200.0f);
    layout.spectrum = {plotX, y, plotWidth, plotHeight};
    y += plotHeight + spacing;
  } else {
    layout.spectrum = {plotX, y, plotWidth, 0.0f};
  }

  layout.spectrogram = {plotX, y, plotWidth, std::max(1.0f, bottom - y - spacing)};
  layout.tuner = {pad, bottom, std::max(1.0f, width - 2 * pad), tunerHeight};
  return layout;
}

// The bins under slice `index` of `count` equal slices of the displayed range
// on a log scale, slice 0 being the lowest. Shared by the spectrogram rows and
// the spectrum columns, so both views put a frequency in the same place.
GUI::BinRange GUI::logSliceBins(unsigned index, unsigned count) const {
  const unsigned long lastBin = paddedSize / 2 - 1;
  const float binsPerHz = static_cast<float>(paddedSize) / static_cast<float>(sampleRate);
  const float logMin = std::log10(minFreq);
  const float logMax = std::log10(maxFreq);

  const float low = std::pow(10.0f, logMin + static_cast<float>(index) / count * (logMax - logMin));
  const float high =
      std::pow(10.0f, logMin + static_cast<float>(index + 1) / count * (logMax - logMin));

  unsigned long begin = static_cast<unsigned long>(low * binsPerHz);
  unsigned long end = static_cast<unsigned long>(std::ceil(high * binsPerHz));

  // Low slices are narrower than a single bin, so the range is widened to
  // keep at least one bin per slice.
  begin = std::clamp(begin, 1UL, lastBin);
  end = std::clamp(end, begin + 1, lastBin + 1);

  return {static_cast<unsigned>(begin), static_cast<unsigned>(end)};
}

// Peak over the bins in the range, so a narrow partial survives the downscale
// instead of being overwritten by whichever bin came last.
float GUI::peakDb(BinRange range) const {
  float peak = 0.0f;
  for (unsigned bin = range.begin; bin < range.end; ++bin) {
    peak = std::max(peak, std::abs(frontSpectrum[bin]));
  }
  return 20.0f * std::log10(peak + 1e-6f);
}

// Sizes the scrollback texture to the spectrogram pixel height, so one texture
// row is one screen row and nothing is resampled vertically.
void GUI::rebuildSpectrogram(unsigned height) {
  if (height == 0) height = 1;
  if (spectrogramTexture.id != 0) UnloadTexture(spectrogramTexture);

  textureHeight = height;
  columnPixels.assign(height, COL_BG);

  // Row 0 is the top of the spectrogram and carries the highest frequency.
  binRanges.resize(height);
  for (unsigned row = 0; row < height; ++row) {
    binRanges[row] = logSliceBins(height - row - 1, height);
  }

  Image blank = GenImageColor(static_cast<int>(historyColumns), static_cast<int>(height), COL_BG);
  spectrogramTexture = LoadTextureFromImage(blank);
  UnloadImage(blank);

  writeColumn = 0;
}

void GUI::rebuildSpectrum(unsigned width) {
  if (width == 0) width = 1;

  spectrumBins.resize(width);
  for (unsigned column = 0; column < width; ++column) {
    spectrumBins[column] = logSliceBins(column, width);
  }

  spectrumTarget.assign(width, dbFloor);
  spectrumDisplay.assign(width, dbFloor);
}

void GUI::consumeSpectrum() {
  if (!newSpectrumAvailable.load(std::memory_order_acquire)) {
    return;  // No new data available
  }

  for (size_t column = 0; column < spectrumBins.size(); ++column) {
    spectrumTarget[column] = peakDb(spectrumBins[column]);
  }
  writeSpectrogramColumn();

  // Only hand the buffer back once we are done reading it: while the flag is
  // set the audio thread leaves frontSpectrum alone.
  newSpectrumAvailable.store(false, std::memory_order_release);
}

void GUI::writeSpectrogramColumn() {
  if (spectrogramTexture.id == 0) return;

  const float span = std::max(1.0f, dbCeiling - dbFloor);

  for (unsigned row = 0; row < textureHeight; ++row) {
    const float intensity = std::clamp((peakDb(binRanges[row]) - dbFloor) / span, 0.0f, 1.0f);
    columnPixels[row] =
        intensity <= gate ? COL_BG : colorRamp()[static_cast<size_t>(intensity * 255.0f)];
  }

  UpdateTextureRec(
      spectrogramTexture,
      Rectangle{static_cast<float>(writeColumn), 0.0f, 1.0f, static_cast<float>(textureHeight)},
      columnPixels.data());

  writeColumn = (writeColumn + 1) % historyColumns;
}

// Rise instantly, fall at spectrumRelease dB per second.
void GUI::stepSpectrum(float dt) {
  const float fall = spectrumRelease * dt;
  for (size_t column = 0; column < spectrumDisplay.size(); ++column) {
    spectrumDisplay[column] = std::max(spectrumTarget[column], spectrumDisplay[column] - fall);
  }
}

void GUI::drawHeader(const Layout& layout) const {
  const Rectangle r = layout.header;

  DrawText("SPECTRO", static_cast<int>(r.x), static_cast<int>(r.y), 20, COL_ACCENT);
  DrawText(deviceName.c_str(), static_cast<int>(r.x) + MeasureText("SPECTRO", 20) + 14,
           static_cast<int>(r.y) + 6, 10, COL_DIM);

  drawTextRight(
      TextFormat("%lu Hz   %u pt   tab: controls", sampleRate, static_cast<unsigned>(paddedSize)),
      r.x + r.width, r.y + 6.0f, 10, COL_DIM);

  const int ruleY = static_cast<int>(r.y + r.height);
  DrawLine(static_cast<int>(r.x), ruleY, static_cast<int>(r.x + r.width), ruleY, COL_LINE);
}

void GUI::drawSpectrum(const Layout& layout) const {
  const Rectangle r = layout.spectrum;
  if (r.height < 1.0f || spectrumDisplay.empty()) return;

  DrawRectangleRec(r, COL_BG);

  const float bottom = r.y + r.height;
  const float span = std::max(1.0f, dbCeiling - dbFloor);
  const float logMin = std::log10(minFreq);
  const float logMax = std::log10(maxFreq);

  const auto yForDb = [&](float db) {
    return bottom - std::clamp((db - dbFloor) / span, 0.0f, 1.0f) * r.height;
  };
  const auto xForFreq = [&](float freq) {
    return r.x + (std::log10(freq) - logMin) / (logMax - logMin) * r.width;
  };

  // Level rules every 20 dB, labelled in the gutter like the frequency axis
  // below. The floor itself is left unlabelled so it cannot collide with the
  // spectrogram's top label.
  for (float db = std::ceil(dbFloor / 20.0f) * 20.0f; db <= dbCeiling; db += 20.0f) {
    const int y = static_cast<int>(yForDb(db));
    if (showGrid) {
      DrawLine(static_cast<int>(r.x) + 1, y, static_cast<int>(r.x + r.width) - 1, y, COL_GRID);
    }
    DrawLine(static_cast<int>(r.x) - 5, y, static_cast<int>(r.x), y, COL_LINE_BRIGHT);
    if (db > dbFloor + 1.0f) {
      drawTextRight(TextFormat("%.0f", static_cast<double>(db)), r.x - 10.0f, y - 4.0f, 10,
                    COL_DIM);
    }
  }

  // Octaves of A along the frequency axis, matching the spectrogram.
  if (showGrid) {
    for (float freq = 27.5f; freq <= maxFreq; freq *= 2.0f) {
      if (freq < minFreq) continue;
      const int x = static_cast<int>(xForFreq(freq));
      DrawLine(x, static_cast<int>(r.y) + 1, x, static_cast<int>(bottom) - 1, COL_GRID);
    }
  }

  // Filled trace, one pixel column per entry.
  float previousY = yForDb(spectrumDisplay[0]);
  for (size_t column = 0; column < spectrumDisplay.size(); ++column) {
    const int x = static_cast<int>(r.x) + static_cast<int>(column);
    const float y = yForDb(spectrumDisplay[column]);
    DrawRectangle(x, static_cast<int>(y), 1, static_cast<int>(bottom - y), COL_TRACE_FILL);
    if (column > 0) {
      DrawLine(x - 1, static_cast<int>(previousY), x, static_cast<int>(y), COL_TRACE);
    }
    previousY = y;
  }

  // Labels go on top of the trace so a loud spectrum cannot swallow them.
  for (float freq = 27.5f; freq <= maxFreq; freq *= 2.0f) {
    if (freq < minFreq) continue;
    DrawText(frequencyLabel(freq), static_cast<int>(xForFreq(freq)) + 4, static_cast<int>(r.y) + 4,
             10, COL_DIM);
  }

  // Where the tuner thinks the fundamental is.
  const Tuner note = tuner.load(std::memory_order_acquire);
  if (note.midi >= 0 && note.freq >= minFreq && note.freq <= maxFreq) {
    const int x = static_cast<int>(xForFreq(note.freq));
    DrawLine(x, static_cast<int>(r.y) + 1, x, static_cast<int>(bottom) - 1,
             Fade(tuneColor(note.cents), 0.7f));
  }

  // Hover readout: a crosshair and the level under the cursor.
  const Vector2 mouse = GetMousePosition();
  if (!ImGui::GetIO().WantCaptureMouse && CheckCollisionPointRec(mouse, r)) {
    const float offset = std::clamp(mouse.x - r.x, 0.0f, r.width - 1.0f);
    const size_t column = std::min(spectrumDisplay.size() - 1, static_cast<size_t>(offset));
    const float freq = std::pow(10.0f, logMin + offset / r.width * (logMax - logMin));

    DrawLine(static_cast<int>(mouse.x), static_cast<int>(r.y) + 1, static_cast<int>(mouse.x),
             static_cast<int>(bottom) - 1, COL_LINE_BRIGHT);

    const char* readout = TextFormat("%.1f Hz   %.0f dB", static_cast<double>(freq),
                                     static_cast<double>(spectrumDisplay[column]));
    const int textWidth = MeasureText(readout, 10);
    const int textX = static_cast<int>(r.x + r.width) - textWidth - 8;
    const int textY = static_cast<int>(r.y) + 6;
    DrawRectangle(textX - 5, textY - 3, textWidth + 10, 16, COL_BG);
    DrawRectangleLines(textX - 5, textY - 3, textWidth + 10, 16, COL_LINE);
    DrawText(readout, textX, textY, 10, COL_TEXT);
  }

  DrawRectangleLinesEx(r, 1.0f, COL_LINE);
}

void GUI::drawSpectrogram(const Layout& layout) const {
  const Rectangle r = layout.spectrogram;
  DrawRectangleRec(r, COL_BG);

  if (spectrogramTexture.id != 0) {
    const float columnWidth = r.width / static_cast<float>(historyColumns);
    const float texHeight = static_cast<float>(textureHeight);

    // writeColumn is where the next column lands, which makes it the oldest.
    const float oldest = static_cast<float>(writeColumn);
    const float oldestCount = static_cast<float>(historyColumns) - oldest;

    // The older block, then the wrapped newer one, so time runs left to right.
    DrawTexturePro(spectrogramTexture, Rectangle{oldest, 0.0f, oldestCount, texHeight},
                   Rectangle{r.x, r.y, oldestCount * columnWidth, r.height}, Vector2{0.0f, 0.0f},
                   0.0f, WHITE);

    if (writeColumn > 0) {
      DrawTexturePro(
          spectrogramTexture, Rectangle{0.0f, 0.0f, oldest, texHeight},
          Rectangle{r.x + oldestCount * columnWidth, r.y, oldest * columnWidth, r.height},
          Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }
  }

  DrawRectangleLinesEx(r, 1.0f, COL_LINE);
}

void GUI::drawFrequencyAxis(const Layout& layout) const {
  const Rectangle r = layout.spectrogram;
  const float logMin = std::log10(minFreq);
  const float logMax = std::log10(maxFreq);

  // Octaves of A, which is what matters when tuning.
  for (float freq = 27.5f; freq <= maxFreq; freq *= 2.0f) {
    if (freq < minFreq) continue;

    const float norm = (std::log10(freq) - logMin) / (logMax - logMin);
    const float y = r.y + r.height - norm * r.height;

    if (showGrid) {
      DrawLine(static_cast<int>(r.x) + 1, static_cast<int>(y),
               static_cast<int>(r.x + r.width) - 1, static_cast<int>(y), COL_GRID);
    }
    DrawLine(static_cast<int>(r.x) - 5, static_cast<int>(y), static_cast<int>(r.x),
             static_cast<int>(y), COL_LINE_BRIGHT);

    drawTextRight(frequencyLabel(freq), r.x - 10.0f, y - 4.0f, 10, COL_DIM);
  }
}

void GUI::drawTuner(const Layout& layout) const {
  const Rectangle r = layout.tuner;
  const Tuner note = tuner.load(std::memory_order_acquire);
  const bool detected = note.midi >= 0;
  const bool inTune = detected && std::abs(note.cents) < 5.0f;

  DrawLine(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.x + r.width),
           static_cast<int>(r.y), COL_LINE);

  const float centerX = r.x + r.width / 2.0f;

  // The note name is the one thing worth reading from across the room.
  const Color noteColor = !detected ? COL_DIM : (inTune ? COL_ACCENT : COL_TEXT);
  drawTextCentered(
      detected ? TextFormat("%s%d", NOTE_NAMES[note.midi % 12], note.midi / 12 - 1) : "--",
      centerX, r.y + 14.0f, 40, noteColor);

  if (detected) {
    DrawText(TextFormat("%+.0f ct", static_cast<double>(note.cents)), static_cast<int>(r.x),
             static_cast<int>(r.y) + 26, 10, COL_DIM);
    drawTextRight(TextFormat("%.1f Hz", static_cast<double>(note.freq)), r.x + r.width,
                  r.y + 26.0f, 10, COL_DIM);
  }

  // Cents meter, ticked every 10 cents.
  const float meterY = r.y + 72.0f;
  const float half = r.width / 2.0f;

  DrawLine(static_cast<int>(r.x), static_cast<int>(meterY), static_cast<int>(r.x + r.width),
           static_cast<int>(meterY), COL_LINE);
  for (int cents = -50; cents <= 50; cents += 10) {
    const float x = centerX + (cents / 50.0f) * half;
    const float tick = (cents == 0) ? 9.0f : 5.0f;
    DrawLine(static_cast<int>(x), static_cast<int>(meterY - tick), static_cast<int>(x),
             static_cast<int>(meterY + tick), cents == 0 ? COL_LINE_BRIGHT : COL_LINE);
  }

  if (detected) {
    const float x = centerX + (std::clamp(note.cents, -50.0f, 50.0f) / 50.0f) * half;
    DrawRectangleRec(Rectangle{x - 1.5f, meterY - 15.0f, 3.0f, 30.0f}, tuneColor(note.cents));
  }

  DrawText("-50", static_cast<int>(r.x), static_cast<int>(meterY) + 20, 10, COL_DIM);
  drawTextCentered("0", centerX, meterY + 20.0f, 10, COL_DIM);
  drawTextRight("+50", r.x + r.width, meterY + 20.0f, 10, COL_DIM);
}

void GUI::drawControls() {
  ImGui::SetNextWindowPos(ImVec2(24.0f, 70.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(286.0f, 0.0f), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("controls", &showControls, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SeparatorText("range");
    bool rangeChanged = false;
    rangeChanged |= ImGui::SliderFloat("min Hz", &minFreq, 20.0f, 400.0f, "%.0f");
    rangeChanged |= ImGui::SliderFloat("max Hz", &maxFreq, 800.0f, 12000.0f, "%.0f");
    if (rangeChanged) {
      // Keep the range sane before it reaches log10 in the rebuild.
      minFreq = std::clamp(minFreq, 20.0f, maxFreq - 50.0f);
      rangeDirty = true;
    }

    ImGui::SeparatorText("levels");
    ImGui::SliderFloat("floor dB", &dbFloor, -140.0f, -40.0f, "%.0f");
    // Loud input lands well above 0 on this uncalibrated scale, so leave room.
    ImGui::SliderFloat("ceil dB", &dbCeiling, -40.0f, 60.0f, "%.0f");
    ImGui::SliderFloat("gate", &gate, 0.0f, 0.6f, "%.2f");
    ImGui::Checkbox("grid lines", &showGrid);

    ImGui::SeparatorText("spectrum");
    ImGui::Checkbox("spectrum plot", &showSpectrum);
    ImGui::SliderFloat("release dB/s", &spectrumRelease, 6.0f, 240.0f, "%.0f");

    ImGui::SeparatorText("input");
    ImGui::TextDisabled("%s", deviceName.c_str());
    ImGui::TextDisabled("%lu Hz / %u pt", sampleRate, static_cast<unsigned>(paddedSize));
    ImGui::TextDisabled("%d fps / %u cols", GetFPS(), historyColumns);
  }
  ImGui::End();
}

void GUI::mainLoop() {
  while (!WindowShouldClose()) {
    const bool imguiHasKeys = ImGui::GetIO().WantCaptureKeyboard;
    if (!imguiHasKeys && IsKeyPressed(KEY_TAB)) showControls = !showControls;
    if (!imguiHasKeys && IsKeyPressed(KEY_G)) showGrid = !showGrid;
    if (!imguiHasKeys && IsKeyPressed(KEY_S)) showSpectrum = !showSpectrum;

    const Layout layout = computeLayout();

    // One texture row per screen row and one plot entry per screen column, so
    // a resize or a range change just rebuilds the lookups.
    const unsigned wantedHeight = static_cast<unsigned>(std::max(1.0f, layout.spectrogram.height));
    const unsigned wantedWidth = static_cast<unsigned>(std::max(1.0f, layout.spectrum.width));
    if (rangeDirty || wantedHeight != textureHeight) rebuildSpectrogram(wantedHeight);
    if (rangeDirty || wantedWidth != spectrumBins.size()) rebuildSpectrum(wantedWidth);
    rangeDirty = false;

    consumeSpectrum();
    stepSpectrum(GetFrameTime());

    BeginDrawing();
    ClearBackground(COL_BG);

    drawSpectrum(layout);
    drawSpectrogram(layout);
    drawFrequencyAxis(layout);
    drawHeader(layout);
    drawTuner(layout);

    rlImGuiBegin();
    if (showControls) drawControls();
    rlImGuiEnd();

    EndDrawing();
  }

  rlImGuiShutdown();
  if (spectrogramTexture.id != 0) UnloadTexture(spectrogramTexture);
  CloseWindow();
}
