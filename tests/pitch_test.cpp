// Replays audio through the tuner pipeline frame by frame, to find out what it
// detects and why.
//
//   pitch_test --synth [--verbose]       run the synthetic suite; exits non-zero
//                                        if any case misses its threshold
//   pitch_test --write-synth <dir>       write the suite's signals out as WAV
//   pitch_test <file.wav> [options]      analyse a recording
//
// Options for a file:
//   --expect <note>   the note that was played, e.g. E2 or F#3; classifies errors
//   --channel <n>     channel to analyse in a multichannel file (default 0)
//   --gate <dBFS>     tuner gate (default: the app's default)
//   --peaks <n>       strongest spectral peaks listed per frame (default 4)
//
// Files are cut into the same analysisFrameSize frames the app analyses, and
// each one goes through analyzeFrame(), the function the app itself calls, so
// what this prints is what the tuner would have shown.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "freq_analysis.h"
#include "wav_io.h"

namespace {

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

constexpr const char* NOTE_NAMES[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                        "F#", "G",  "G#", "A",  "A#", "B"};

std::string noteName(int midi) {
  if (midi < 0) return "--";
  return std::string(NOTE_NAMES[midi % 12]) + std::to_string(midi / 12 - 1);
}

double midiToFreq(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

// "E2", "F#3", "Bb1" -> midi number, or -1.
int parseNote(const std::string& text) {
  if (text.empty()) return -1;
  constexpr int SEMITONES[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
  const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  if (letter < 'A' || letter > 'G') return -1;

  int semitone = SEMITONES[letter - 'A'];
  size_t pos = 1;
  if (pos < text.size() && text[pos] == '#') {
    ++semitone;
    ++pos;
  } else if (pos < text.size() && text[pos] == 'b') {
    --semitone;
    ++pos;
  }

  char* end = nullptr;
  const long octave = std::strtol(text.c_str() + pos, &end, 10);
  if (end == text.c_str() + pos || *end != '\0') return -1;
  return static_cast<int>((octave + 1) * 12 + semitone);
}

// ---------------------------------------------------------------------------
// Classifying a detection against the note that was played
// ---------------------------------------------------------------------------

enum class Outcome { Correct, Gated, Harmonic, Subharmonic, Neighbour, Other };

struct Verdict {
  Outcome outcome;
  int factor = 0;  // Harmonic or subharmonic number
};

// A wrong note is rarely random: picking the 2nd harmonic reads an octave
// high, the 3rd an octave and a fifth high. Naming the relationship is most of
// the diagnosis.
Verdict classify(const PitchResult& result, int expectedMidi) {
  if (result.gated) return {Outcome::Gated};
  if (result.note.midi == expectedMidi) return {Outcome::Correct};

  const double ratio = result.frequency / midiToFreq(expectedMidi);
  const auto closeTo = [](double value, int whole) {
    return std::abs(1200.0 * std::log2(value / whole)) < 50.0;
  };

  if (ratio > 1.0) {
    const int h = static_cast<int>(std::lround(ratio));
    if (h >= 2 && closeTo(ratio, h)) return {Outcome::Harmonic, h};
  } else if (ratio > 0.0) {
    const int s = static_cast<int>(std::lround(1.0 / ratio));
    if (s >= 2 && closeTo(1.0 / ratio, s)) return {Outcome::Subharmonic, s};
  }
  if (std::abs(result.note.midi - expectedMidi) == 1) return {Outcome::Neighbour};
  return {Outcome::Other};
}

std::string verdictLabel(const Verdict& v) {
  switch (v.outcome) {
    case Outcome::Correct: return "ok";
    case Outcome::Gated: return "gated";
    case Outcome::Harmonic: return "x" + std::to_string(v.factor);
    case Outcome::Subharmonic: return "/" + std::to_string(v.factor);
    case Outcome::Neighbour: return "+-1st";
    case Outcome::Other: return "other";
  }
  return "?";
}

// ---------------------------------------------------------------------------
// Spectral peaks, to show what the detector was choosing between
// ---------------------------------------------------------------------------

struct Peak {
  double freq;
  double dbRel;  // dB relative to the strongest peak listed
};

std::vector<Peak> strongestPeaks(const FFTData& spectrum, int sampleRate, size_t count) {
  const size_t half = spectrum.size() / 2;
  const double hzPerBin = static_cast<double>(sampleRate) / spectrum.size();
  const size_t firstBin = std::max<size_t>(2, static_cast<size_t>(20.0 / hzPerBin));

  std::vector<double> mag(half);
  for (size_t k = 0; k < half; ++k) mag[k] = std::abs(spectrum[k]);

  std::vector<size_t> maxima;
  for (size_t k = firstBin; k + 1 < half; ++k) {
    if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1]) maxima.push_back(k);
  }
  std::sort(maxima.begin(), maxima.end(), [&](size_t a, size_t b) { return mag[a] > mag[b]; });

  // Zero padding puts several local maxima on one partial's main lobe; keep
  // only the strongest within 15 Hz.
  std::vector<Peak> peaks;
  const double strongest = maxima.empty() ? 1.0 : mag[maxima.front()];
  for (size_t k : maxima) {
    if (peaks.size() >= count) break;
    const double l = mag[k - 1], c = mag[k], r = mag[k + 1];
    const double denom = l - 2 * c + r;
    const double delta = denom != 0.0 ? 0.5 * (l - r) / denom : 0.0;
    const double freq = (k + delta) * hzPerBin;

    const bool tooClose = std::any_of(peaks.begin(), peaks.end(), [&](const Peak& p) {
      return std::abs(p.freq - freq) < 15.0;
    });
    if (!tooClose) peaks.push_back({freq, 20.0 * std::log10(c / strongest + 1e-12)});
  }
  return peaks;
}

std::string formatPeaks(const std::vector<Peak>& peaks) {
  std::string out;
  char buffer[48];
  for (const Peak& p : peaks) {
    std::snprintf(buffer, sizeof(buffer), "%8.1f (%+5.1f)", p.freq, p.dbRel);
    out += buffer;
  }
  return out;
}

// ---------------------------------------------------------------------------
// Running signals through analyzeFrame
// ---------------------------------------------------------------------------

struct FrameReport {
  size_t index;
  double time;
  PitchResult result;
  const FFTData* spectrum;
};

void analyzeSignal(const std::vector<float>& samples, int sampleRate,
                   const PitchSettings& settings,
                   const std::function<void(const FrameReport&)>& onFrame) {
  // 128 KB: kept off the stack.
  auto spectrum = std::make_unique<FFTData>();
  std::vector<float> frame(analysisFrameSize);

  size_t index = 0;
  for (size_t start = 0; start + analysisFrameSize <= samples.size();
       start += analysisFrameSize, ++index) {
    std::copy_n(samples.begin() + static_cast<std::ptrdiff_t>(start), analysisFrameSize,
                frame.begin());
    const PitchResult result =
        analyzeFrame(frame.data(), analysisFrameSize, sampleRate, settings, *spectrum);
    onFrame({index, static_cast<double>(start) / sampleRate, result, spectrum.get()});
  }
}

struct Tally {
  int frames = 0;
  int gated = 0;
  int correct = 0;
  int neighbour = 0;
  int other = 0;
  std::map<int, int> harmonics;     // factor -> count
  std::map<int, int> subharmonics;  // factor -> count
  double centsError = 0.0;          // Sum of |cents| on correct frames vs the true pitch
  double worstCents = 0.0;

  int voiced() const { return frames - gated; }
  int harmonicTotal() const {
    int n = 0;
    for (const auto& [factor, count] : harmonics) n += count;
    return n;
  }

  void add(const PitchResult& result, const Verdict& verdict, double trueFreq) {
    ++frames;
    switch (verdict.outcome) {
      case Outcome::Gated: ++gated; break;
      case Outcome::Correct: {
        ++correct;
        if (trueFreq > 0.0) {
          const double cents = std::abs(1200.0 * std::log2(result.frequency / trueFreq));
          centsError += cents;
          worstCents = std::max(worstCents, cents);
        }
        break;
      }
      case Outcome::Harmonic: ++harmonics[verdict.factor]; break;
      case Outcome::Subharmonic: ++subharmonics[verdict.factor]; break;
      case Outcome::Neighbour: ++neighbour; break;
      case Outcome::Other: ++other; break;
    }
  }
};

std::string breakdown(const Tally& t) {
  std::string out;
  for (const auto& [factor, count] : t.harmonics) {
    out += "x" + std::to_string(factor) + ":" + std::to_string(count) + " ";
  }
  for (const auto& [factor, count] : t.subharmonics) {
    out += "/" + std::to_string(factor) + ":" + std::to_string(count) + " ";
  }
  if (t.neighbour) out += "+-1st:" + std::to_string(t.neighbour) + " ";
  if (t.other) out += "other:" + std::to_string(t.other) + " ";
  return out.empty() ? "-" : out;
}

// ---------------------------------------------------------------------------
// Synthetic signals with known pitch
// ---------------------------------------------------------------------------

// Each segment is a whole number of frames long, so segments written back to
// back into one WAV keep their frame alignment.
size_t segmentLength(int sampleRate, double seconds) {
  const size_t frames = static_cast<size_t>(std::lround(seconds * sampleRate / analysisFrameSize));
  return std::max<size_t>(1, frames) * analysisFrameSize;
}

void normalize(std::vector<float>& samples, float peak) {
  float largest = 0.0f;
  for (float s : samples) largest = std::max(largest, std::abs(s));
  if (largest > 0.0f) {
    for (float& s : samples) s *= peak / largest;
  }
}

// Partial k+1 at amplitude amps[k]. Fixed, spread phases keep the waveform
// from piling up into a single spike.
std::vector<float> harmonicTone(double f0, const std::vector<double>& amps, int sampleRate,
                                double seconds) {
  std::vector<float> out(segmentLength(sampleRate, seconds));
  for (size_t k = 0; k < amps.size(); ++k) {
    const double freq = f0 * static_cast<double>(k + 1);
    if (freq >= sampleRate / 2.0) break;
    const double phase = 0.7 * static_cast<double>(k);
    for (size_t n = 0; n < out.size(); ++n) {
      out[n] += static_cast<float>(
          amps[k] * std::sin(2 * std::numbers::pi * freq * n / sampleRate + phase));
    }
  }
  normalize(out, 0.5f);
  return out;
}

// Positions along the string as a fraction of its length from the bridge.
constexpr double PICK_POSITION = 0.18;  // A typical picking spot
constexpr double BRIDGE_PICKUP = 0.065;
constexpr double NECK_PICKUP = 0.25;

// A plucked steel string heard through a magnetic pickup, built additively.
//
// Plucked at fraction b of its length from the bridge, the string starts as a
// triangle at rest, whose harmonic k has amplitude ~ sin(pi k b) / k^2. A
// magnetic pickup at fraction p senses the string's velocity there, which
// multiplies harmonic k by k sin(pi k p) and turns it into a sine starting at
// zero. So harmonic k ~ sin(pi k b) sin(pi k p) / k. With a bridge pickup that
// leaves harmonics 2 to 4 some 4-5 dB above the fundamental, which is the
// textbook reason tuners misread through one.
//
// Higher harmonics die away faster (time constant sustain / k), and steel
// strings are slightly stiff, which sharpens harmonic k by sqrt(1 + B k^2).
// `trueFreq` returns the fundamental including that stiffness.
std::vector<float> pluckedString(double f0, int sampleRate, double seconds, double pickup,
                                 double& trueFreq) {
  constexpr double stiffness = 5e-5;  // B, typical of a guitar's wound strings
  constexpr double sustain = 3.0;     // Seconds, for the fundamental
  constexpr double pi = std::numbers::pi;
  trueFreq = f0 * std::sqrt(1.0 + stiffness);

  std::vector<float> out(segmentLength(sampleRate, seconds));
  for (int k = 1; k <= 60; ++k) {
    const double freq = f0 * k * std::sqrt(1.0 + stiffness * k * k);
    if (freq >= 0.45 * sampleRate) break;

    const double amp = std::sin(pi * k * PICK_POSITION) * std::sin(pi * k * pickup) / k;
    const double tau = sustain / k;
    for (size_t n = 0; n < out.size(); ++n) {
      const double t = static_cast<double>(n) / sampleRate;
      out[n] += static_cast<float>(amp * std::exp(-t / tau) * std::sin(2 * pi * freq * t));
    }
  }
  normalize(out, 0.5f);
  return out;
}

std::vector<float> noise(int sampleRate, double seconds, double rmsDbfs, unsigned seed) {
  std::mt19937 rng(seed);
  // Uniform on [-a, a] has an RMS of a / sqrt(3).
  const float a = static_cast<float>(std::pow(10.0, rmsDbfs / 20.0) * std::sqrt(3.0));
  std::uniform_real_distribution<float> uniform(-a, a);
  std::vector<float> out(segmentLength(sampleRate, seconds));
  for (float& s : out) s = uniform(rng);
  return out;
}

struct Segment {
  std::vector<float> samples;
  int expectedMidi;  // -1: every frame should be gated
  double trueFreq;   // Actual fundamental, for the cents error
};

struct SynthCase {
  std::string name;
  int sampleRate;
  std::vector<Segment> segments;
  double threshold;  // Required share of voiced frames that are correct
};

// Standard tuning, low to high.
constexpr int OPEN_STRINGS[6] = {40, 45, 50, 55, 59, 64};  // E2 A2 D3 G3 B3 E4

std::vector<SynthCase> buildSuite() {
  constexpr double seconds = 2.0;
  std::vector<SynthCase> suite;

  const auto plucks = [&](std::string name, int rate, const std::vector<int>& notes,
                          double pickup) {
    SynthCase c{std::move(name), rate, {}, 0.90};
    for (int midi : notes) {
      double trueFreq = 0.0;
      auto s = pluckedString(midiToFreq(midi), rate, seconds, pickup, trueFreq);
      c.segments.push_back({std::move(s), midi, trueFreq});
    }
    return c;
  };
  const std::vector<int> openStrings(std::begin(OPEN_STRINGS), std::end(OPEN_STRINGS));

  for (int rate : {44100, 48000}) {
    const std::string tag = rate == 44100 ? "44.1k " : "48k   ";

    // Pure tones: if these fail, the problem is not harmonics.
    SynthCase chromatic{tag + "sine  E2-E5 chromatic", rate, {}, 0.95};
    for (int midi = 40; midi <= 76; ++midi) {
      const double f = midiToFreq(midi);
      chromatic.segments.push_back({harmonicTone(f, {1.0}, rate, seconds), midi, f});
    }
    suite.push_back(std::move(chromatic));

    if (rate != 44100) {
      // One aggregate case is enough to show the rate makes no difference.
      suite.push_back(plucks(tag + "pluck bridge pickup, open strings", rate, openStrings,
                             BRIDGE_PICKUP));
      continue;
    }

    // The bare mechanism: a steady tone whose 2nd harmonic outweighs its
    // fundamental, on each open string.
    SynthCase weak{tag + "tone  2nd harmonic > fundamental", rate, {}, 0.90};
    for (int midi : openStrings) {
      const double f = midiToFreq(midi);
      weak.segments.push_back(
          {harmonicTone(f, {0.35, 1.0, 0.8, 0.5, 0.3, 0.2}, rate, seconds), midi, f});
    }
    suite.push_back(std::move(weak));

    // Plucked strings through a bridge pickup, one case per open string, so a
    // failure says which string.
    for (int midi : openStrings) {
      suite.push_back(plucks(tag + "pluck bridge pickup, " + noteName(midi), rate, {midi},
                             BRIDGE_PICKUP));
    }
    suite.push_back(plucks(tag + "pluck neck pickup, open strings", rate, openStrings,
                           NECK_PICKUP));
    suite.push_back(plucks(tag + "pluck bridge, F2 C3 A3 C4 G4 A4", rate,
                           {41, 48, 57, 60, 67, 69}, BRIDGE_PICKUP));

    // Nothing to name: every frame should be gated.
    suite.push_back(
        {tag + "silence", rate, {{std::vector<float>(segmentLength(rate, 1.0)), -1, 0}}, 1.0});
    suite.push_back({tag + "noise -70 dBFS", rate, {{noise(rate, 1.0, -70.0, 7), -1, 0}}, 1.0});
  }
  return suite;
}

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

// A float WAV written and read back must analyse identically to the samples it
// came from, or file replays would not be telling the truth about live input.
bool replayMatchesLive() {
  double trueFreq = 0.0;
  const std::vector<float> signal =
      pluckedString(midiToFreq(40), 44100, 1.0, BRIDGE_PICKUP, trueFreq);
  const auto path = std::filesystem::temp_directory_path() / "pitch_test_roundtrip.wav";

  WavData loaded;
  std::string error;
  if (!writeWavMono(path.string(), signal.data(), signal.size(), 44100) ||
      !readWav(path.string(), loaded, error) || loaded.samples != signal) {
    std::filesystem::remove(path);
    return false;
  }
  std::filesystem::remove(path);

  std::vector<float> live, replay;
  analyzeSignal(signal, 44100, {}, [&](const FrameReport& f) { live.push_back(f.result.frequency); });
  analyzeSignal(loaded.samples, 44100, {},
                [&](const FrameReport& f) { replay.push_back(f.result.frequency); });
  return live == replay;
}

// The search range of findPeakFrequency, checked on hand-built spectra so the
// result does not depend on any signal.
//
// For real input the upper half of the FFT mirrors the lower half, and float
// rounding can leave a mirrored bin a hair above its twin: the search must not
// look there, or a 4 kHz partial reads as 40 kHz.
bool peakSearchSkipsMirror() {
  auto spectrum = std::make_unique<FFTData>();
  const size_t k = 1000;
  const size_t mirror = spectrum->size() - k;
  (*spectrum)[k - 1] = 0.5f;
  (*spectrum)[k] = 1.0f;
  (*spectrum)[k + 1] = 0.5f;
  (*spectrum)[mirror - 1] = 0.5f;
  (*spectrum)[mirror] = 1.0001f;
  (*spectrum)[mirror + 1] = 0.5f;
  return findPeakFrequency(*spectrum, 44100) < 22050.0f;
}

// Rumble, handling noise or an interface's DC offset can outweigh a quiet
// tone, and nothing below ~20 Hz is a note: the search must start above it.
bool peakSearchSkipsDc() {
  auto spectrum = std::make_unique<FFTData>();
  (*spectrum)[0] = 1.0f;
  (*spectrum)[1] = 0.6f;
  const size_t a2 = 41;  // 110 Hz at 44.1k
  (*spectrum)[a2 - 1] = 0.4f;
  (*spectrum)[a2] = 0.8f;
  (*spectrum)[a2 + 1] = 0.4f;
  return findPeakFrequency(*spectrum, 44100) > 20.0f;
}

int runSuite(bool verbose) {
  const PitchSettings settings{};
  const std::vector<SynthCase> suite = buildSuite();

  std::printf("Synthetic suite: %zu-sample frames, gate %.0f dBFS\n\n",
              static_cast<size_t>(analysisFrameSize), static_cast<double>(settings.gateDb));
  std::printf("%-38s %7s %8s %7s  %-26s %9s  %s\n", "case", "voiced", "correct", "gated",
              "wrong (by relation)", "|cents|", "verdict");
  std::printf("%s\n", std::string(112, '-').c_str());

  int failures = 0;
  for (const SynthCase& c : suite) {
    Tally tally;
    std::vector<std::string> offending;

    for (const Segment& segment : c.segments) {
      analyzeSignal(segment.samples, c.sampleRate, settings, [&](const FrameReport& f) {
        Verdict verdict{Outcome::Other};
        if (segment.expectedMidi < 0) {
          // Nothing should be named here, so a gated frame is the right answer.
          ++tally.frames;
          if (f.result.gated) {
            ++tally.gated;
            ++tally.correct;
            verdict = {Outcome::Correct};
          } else {
            ++tally.other;
          }
        } else {
          verdict = classify(f.result, segment.expectedMidi);
          tally.add(f.result, verdict, segment.trueFreq);
        }

        if (verbose && verdict.outcome != Outcome::Correct && offending.size() < 6) {
          char line[256];
          std::snprintf(line, sizeof(line),
                        "    want %-4s frame %2zu  %6.1f dB  got %8.2f Hz %-4s  %-6s peaks:%s",
                        noteName(segment.expectedMidi).c_str(), f.index,
                        static_cast<double>(f.result.levelDb),
                        static_cast<double>(f.result.frequency),
                        noteName(f.result.note.midi).c_str(), verdictLabel(verdict).c_str(),
                        formatPeaks(strongestPeaks(*f.spectrum, c.sampleRate, 4)).c_str());
          offending.push_back(line);
        }
      });
    }

    const bool expectGated = c.segments.front().expectedMidi < 0;
    const int denominator = expectGated ? tally.frames : tally.voiced();
    const double share = denominator > 0 ? static_cast<double>(tally.correct) / denominator : 0.0;
    const bool pass = denominator > 0 && share >= c.threshold;
    if (!pass) ++failures;

    char cents[32] = "-";
    if (!expectGated && tally.correct > 0) {
      std::snprintf(cents, sizeof(cents), "%.1f/%.1f", tally.centsError / tally.correct,
                    tally.worstCents);
    }

    std::printf("%-38s %3d/%-3d %7.0f%% %7d  %-26s %9s  %s\n", c.name.c_str(), tally.voiced(),
                tally.frames, share * 100.0, tally.gated,
                expectGated ? (tally.other ? "detected a note" : "-") : breakdown(tally).c_str(),
                cents, pass ? "PASS" : "FAIL");
    for (const std::string& line : offending) std::printf("%s\n", line.c_str());
  }

  std::printf("\n|cents| is mean/worst deviation from the signal's true pitch on correct frames.\n");
  std::printf("x2 reads an octave high (2nd harmonic), x3 an octave and a fifth (3rd harmonic).\n");

  const struct {
    const char* name;
    bool (*check)();
  } checks[] = {
      {"WAV round trip analyses identically to live input", replayMatchesLive},
      {"peak search ignores the mirrored upper half of the FFT", peakSearchSkipsMirror},
      {"peak search ignores DC and sub-audio bins", peakSearchSkipsDc},
  };

  std::printf("\n");
  for (const auto& c : checks) {
    const bool pass = c.check();
    if (!pass) ++failures;
    std::printf("%-58s %s\n", c.name, pass ? "PASS" : "FAIL");
  }

  std::printf("\n%d of %zu checks failed\n", failures, suite.size() + std::size(checks));
  return failures == 0 ? 0 : 1;
}

int writeSuite(const std::string& dir) {
  std::filesystem::create_directories(dir);
  for (const SynthCase& c : buildSuite()) {
    std::vector<float> all;
    for (const Segment& s : c.segments) all.insert(all.end(), s.samples.begin(), s.samples.end());

    std::string slug;
    for (char ch : c.name) {
      if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '#' || ch == '.') {
        slug += ch;
      } else if (!slug.empty() && slug.back() != '-') {
        slug += '-';
      }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();

    const auto path = std::filesystem::path(dir) / (slug + ".wav");
    if (!writeWavMono(path.string(), all.data(), all.size(), c.sampleRate)) {
      std::fprintf(stderr, "could not write %s\n", path.string().c_str());
      return 1;
    }
    std::printf("wrote %s\n", path.string().c_str());
  }
  return 0;
}

int runFile(const std::string& path, int expectedMidi, int channel, const PitchSettings& settings,
            size_t peakCount) {
  WavData wav;
  std::string error;
  if (!readWav(path, wav, error)) {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
    return 2;
  }
  if (channel < 0 || channel >= wav.channels) {
    std::fprintf(stderr, "%s has %d channel(s); --channel %d is out of range\n", path.c_str(),
                 wav.channels, channel);
    return 2;
  }

  std::vector<float> mono(wav.samples.size() / wav.channels);
  for (size_t i = 0; i < mono.size(); ++i) mono[i] = wav.samples[i * wav.channels + channel];

  std::printf("%s\n%d Hz, %d channel(s), analysing channel %d, %.2f s, %zu frames of %zu\n",
              path.c_str(), wav.sampleRate, wav.channels, channel,
              static_cast<double>(mono.size()) / wav.sampleRate, mono.size() / analysisFrameSize,
              static_cast<size_t>(analysisFrameSize));
  std::printf("gate %.0f dBFS%s%s\n\n", static_cast<double>(settings.gateDb),
              expectedMidi >= 0 ? ", expecting " : "",
              expectedMidi >= 0 ? noteName(expectedMidi).c_str() : "");

  std::printf("%5s %7s %7s %10s %-4s %6s  %-6s  strongest peaks: Hz (dB rel. strongest)\n",
              "frame", "time", "level", "detected", "note", "cents", "result");

  Tally tally;
  std::map<int, int> histogram;
  analyzeSignal(mono, wav.sampleRate, settings, [&](const FrameReport& f) {
    std::string label = f.result.gated ? "gated" : "-";
    if (expectedMidi >= 0) {
      const Verdict verdict = classify(f.result, expectedMidi);
      tally.add(f.result, verdict, midiToFreq(expectedMidi));
      label = verdictLabel(verdict);
    }
    if (!f.result.gated) ++histogram[f.result.note.midi];

    std::printf("%5zu %7.3f %7.1f %10.2f %-4s %+6.1f  %-6s %s\n", f.index, f.time,
                static_cast<double>(f.result.levelDb), static_cast<double>(f.result.frequency),
                noteName(f.result.note.midi).c_str(), static_cast<double>(f.result.note.cents),
                label.c_str(),
                f.result.gated ? ""
                               : formatPeaks(strongestPeaks(*f.spectrum, wav.sampleRate, peakCount))
                                     .c_str());
  });

  std::vector<std::pair<int, int>> ranked(histogram.begin(), histogram.end());
  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  std::printf("\ndetected notes:");
  for (size_t i = 0; i < std::min<size_t>(ranked.size(), 6); ++i) {
    std::printf("  %s x%d", noteName(ranked[i].first).c_str(), ranked[i].second);
  }
  std::printf("\n");

  if (expectedMidi >= 0) {
    const int voiced = tally.voiced();
    std::printf("expected %s: %d of %d voiced frames correct (%.0f%%), %d gated; wrong: %s\n",
                noteName(expectedMidi).c_str(), tally.correct, voiced,
                voiced ? 100.0 * tally.correct / voiced : 0.0, tally.gated, breakdown(tally).c_str());
  }
  return 0;
}

void usage() {
  std::printf(
      "usage:\n"
      "  pitch_test --synth [--verbose]\n"
      "  pitch_test --write-synth <dir>\n"
      "  pitch_test <file.wav> [--expect E2] [--channel 0] [--gate -50] [--peaks 4]\n");
}

}  // namespace

int main(int argc, char* argv[]) {
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) {
    usage();
    return 2;
  }

  if (args[0] == "--synth") {
    const bool verbose = std::find(args.begin(), args.end(), "--verbose") != args.end();
    return runSuite(verbose);
  }
  if (args[0] == "--write-synth") {
    if (args.size() < 2) {
      usage();
      return 2;
    }
    return writeSuite(args[1]);
  }

  std::string path;
  int expected = -1;
  int channel = 0;
  size_t peaks = 4;
  PitchSettings settings{};

  for (size_t i = 0; i < args.size(); ++i) {
    const auto value = [&]() -> const std::string* {
      return i + 1 < args.size() ? &args[++i] : nullptr;
    };
    if (args[i] == "--expect") {
      const std::string* v = value();
      expected = v ? parseNote(*v) : -1;
      if (expected < 0) {
        std::fprintf(stderr, "--expect needs a note such as E2 or F#3\n");
        return 2;
      }
    } else if (args[i] == "--channel") {
      const std::string* v = value();
      if (!v) return usage(), 2;
      channel = std::atoi(v->c_str());
    } else if (args[i] == "--gate") {
      const std::string* v = value();
      if (!v) return usage(), 2;
      settings.gateDb = static_cast<float>(std::atof(v->c_str()));
    } else if (args[i] == "--peaks") {
      const std::string* v = value();
      if (!v) return usage(), 2;
      peaks = static_cast<size_t>(std::max(0, std::atoi(v->c_str())));
    } else if (path.empty() && args[i].rfind("--", 0) != 0) {
      path = args[i];
    } else {
      usage();
      return 2;
    }
  }

  if (path.empty()) {
    usage();
    return 2;
  }
  return runFile(path, expected, channel, settings, peaks);
}
