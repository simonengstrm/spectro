#include "recorder.h"

#include <ctime>
#include <filesystem>
#include <iostream>

#include "wav_io.h"

namespace {

std::string timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
  return text;
}

}  // namespace

void Recorder::start() {
  std::lock_guard lock(mutex);
  take.clear();
  captured.store(0, std::memory_order_relaxed);
  active.store(true, std::memory_order_release);
}

void Recorder::append(const float* samples, std::size_t count) {
  if (!active.load(std::memory_order_acquire)) return;

  std::lock_guard lock(mutex);
  // stop() may have run between the check above and taking the lock.
  if (!active.load(std::memory_order_relaxed)) return;
  take.insert(take.end(), samples, samples + count);
  captured.store(take.size(), std::memory_order_relaxed);
}

float Recorder::seconds() const {
  return static_cast<float>(captured.load(std::memory_order_relaxed)) / sampleRate;
}

std::string Recorder::stop() {
  std::vector<float> finished;
  {
    std::lock_guard lock(mutex);
    active.store(false, std::memory_order_release);
    finished.swap(take);
    captured.store(0, std::memory_order_relaxed);
  }
  if (finished.empty()) return {};

  std::error_code ec;
  std::filesystem::create_directories("recordings", ec);
  const std::filesystem::path path =
      std::filesystem::path("recordings") / ("take-" + timestamp() + ".wav");

  if (!writeWavMono(path.string(), finished.data(), finished.size(), sampleRate)) {
    std::cout << "Could not write recording to " << path.string() << std::endl;
    return {};
  }

  const std::string saved = std::filesystem::absolute(path, ec).string();
  std::cout << "Saved " << finished.size() / static_cast<double>(sampleRate) << " s to " << saved
            << std::endl;
  return saved;
}
