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
  SetWindowMinSize(560, 420);
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

  Layout layout{};
  layout.header = {pad, pad, std::max(1.0f, width - 2 * pad), headerHeight};
  layout.spectrogram = {pad + axisWidth, top, std::max(1.0f, width - 2 * pad - axisWidth),
                        std::max(1.0f, bottom - top - spacing)};
  layout.tuner = {pad, bottom, std::max(1.0f, width - 2 * pad), tunerHeight};
  return layout;
}

// Sizes the scrollback texture to the spectrogram pixel height, so one texture
// row is one screen row and nothing is resampled vertically, and works out
// which FFT bins land on each of those rows.
void GUI::rebuildSpectrogram(unsigned height) {
  if (height == 0) height = 1;
  if (spectrogramTexture.id != 0) UnloadTexture(spectrogramTexture);

  textureHeight = height;
  columnPixels.assign(height, COL_BG);

  const unsigned long lastBin = paddedSize / 2 - 1;
  const float binsPerHz = static_cast<float>(paddedSize) / static_cast<float>(sampleRate);
  const float logMin = std::log10(minFreq);
  const float logMax = std::log10(maxFreq);

  binRanges.resize(height);
  for (unsigned row = 0; row < height; ++row) {
    // Row 0 is the top of the spectrogram and carries the highest frequency.
    const float normTop = static_cast<float>(height - row) / height;
    const float normBottom = static_cast<float>(height - row - 1) / height;

    const float freqTop = std::pow(10.0f, logMin + normTop * (logMax - logMin));
    const float freqBottom = std::pow(10.0f, logMin + normBottom * (logMax - logMin));

    unsigned long begin = static_cast<unsigned long>(freqBottom * binsPerHz);
    unsigned long end = static_cast<unsigned long>(std::ceil(freqTop * binsPerHz));

    // Low rows are narrower than a single bin, so the range is widened to
    // keep at least one bin per row.
    begin = std::clamp(begin, 1UL, lastBin);
    end = std::clamp(end, begin + 1, lastBin + 1);

    binRanges[row] = {static_cast<unsigned>(begin), static_cast<unsigned>(end)};
  }

  Image blank = GenImageColor(static_cast<int>(historyColumns), static_cast<int>(height), COL_BG);
  spectrogramTexture = LoadTextureFromImage(blank);
  UnloadImage(blank);

  writeColumn = 0;
  rangeDirty = false;
}

void GUI::updateSpectrogram() {
  if (!newSpectrumAvailable.exchange(false, std::memory_order_acquire)) {
    return;  // No new data available
  }
  if (spectrogramTexture.id == 0) return;

  const float span = std::max(1.0f, dbCeiling - dbFloor);

  for (unsigned row = 0; row < textureHeight; ++row) {
    const BinRange range = binRanges[row];

    // Peak over the bins on this row, so a narrow partial survives the
    // downscale instead of being overwritten by whichever bin came last.
    float peak = 0.0f;
    for (unsigned bin = range.begin; bin < range.end; ++bin) {
      peak = std::max(peak, std::abs(frontSpectrum[bin]));
    }

    const float db = 20.0f * std::log10(peak + 1e-6f);
    const float intensity = std::clamp((db - dbFloor) / span, 0.0f, 1.0f);

    columnPixels[row] =
        intensity <= gate ? COL_BG : colorRamp()[static_cast<size_t>(intensity * 255.0f)];
  }

  UpdateTextureRec(
      spectrogramTexture,
      Rectangle{static_cast<float>(writeColumn), 0.0f, 1.0f, static_cast<float>(textureHeight)},
      columnPixels.data());

  writeColumn = (writeColumn + 1) % historyColumns;
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

    const char* label = freq >= 1000.0f ? TextFormat("%.1fk", static_cast<double>(freq) / 1000.0)
                                        : TextFormat("%.0f", static_cast<double>(freq));
    drawTextRight(label, r.x - 10.0f, y - 4.0f, 10, COL_DIM);
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
    const Color marker = inTune ? COL_ACCENT : (std::abs(note.cents) < 20.0f ? COL_WARN : COL_BAD);
    DrawRectangleRec(Rectangle{x - 1.5f, meterY - 15.0f, 3.0f, 30.0f}, marker);
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
    ImGui::SliderFloat("ceil dB", &dbCeiling, -40.0f, 20.0f, "%.0f");
    ImGui::SliderFloat("gate", &gate, 0.0f, 0.6f, "%.2f");
    ImGui::Checkbox("grid lines", &showGrid);

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

    const Layout layout = computeLayout();

    // One texture row per screen row, so a resize just rebuilds the target.
    const unsigned wantedHeight = static_cast<unsigned>(std::max(1.0f, layout.spectrogram.height));
    if (rangeDirty || wantedHeight != textureHeight) rebuildSpectrogram(wantedHeight);

    updateSpectrogram();

    BeginDrawing();
    ClearBackground(COL_BG);

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
