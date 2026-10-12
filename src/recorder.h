#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

// Captures the raw frames the tuner analyses, so a take can be replayed
// through tests/pitch_test frame for frame: the frames start at the same
// sample and the file is 32-bit float, so the replay sees identical input.
class Recorder {
 public:
  explicit Recorder(int sampleRate) : sampleRate(sampleRate) {}

  void start();

  // Stops and writes the take to recordings/ as 32-bit float WAV. Returns the
  // file's absolute path, or an empty string if nothing was captured or the
  // write failed.
  std::string stop();

  bool recording() const { return active.load(std::memory_order_acquire); }
  float seconds() const;

  // Called from the analysis thread with each frame, before windowing.
  void append(const float* samples, std::size_t count);

 private:
  int sampleRate;
  std::atomic<bool> active{false};
  std::atomic<std::size_t> captured{0};
  std::mutex mutex;
  std::vector<float> take;
};
