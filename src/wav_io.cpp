#include "wav_io.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {

constexpr uint16_t FORMAT_PCM = 1;
constexpr uint16_t FORMAT_FLOAT = 3;
constexpr uint16_t FORMAT_EXTENSIBLE = 0xFFFE;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool readWav(const std::string& path, WavData& out, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open file";
    return false;
  }
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());

  if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
      std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
    error = "not a RIFF/WAVE file";
    return false;
  }

  uint16_t format = 0;
  uint16_t channels = 0;
  uint16_t bits = 0;
  uint32_t sampleRate = 0;
  const uint8_t* pcm = nullptr;
  size_t pcmBytes = 0;

  // Walk the chunks rather than assuming a fixed header: recorders add LIST,
  // fact, bext and others, and fmt is not always first.
  size_t pos = 12;
  while (pos + 8 <= data.size()) {
    const uint8_t* chunk = data.data() + pos;
    const uint32_t size = readU32(chunk + 4);
    const size_t body = pos + 8;
    // A truncated data chunk still yields whatever audio is there.
    const size_t available = std::min<size_t>(size, data.size() - body);

    if (std::memcmp(chunk, "fmt ", 4) == 0 && available >= 16) {
      format = readU16(chunk + 8);
      channels = readU16(chunk + 10);
      sampleRate = readU32(chunk + 12);
      bits = readU16(chunk + 22);
      // The real format tag is the first two bytes of the SubFormat GUID.
      if (format == FORMAT_EXTENSIBLE && available >= 26) format = readU16(chunk + 8 + 24);
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      pcm = data.data() + body;
      pcmBytes = available;
    }

    pos = body + size + (size & 1);  // Chunks are padded to even sizes
  }

  if (channels == 0 || sampleRate == 0) {
    error = "missing or invalid fmt chunk";
    return false;
  }
  if (pcm == nullptr) {
    error = "no data chunk";
    return false;
  }

  const bool pcmInt = format == FORMAT_PCM && (bits == 16 || bits == 24 || bits == 32);
  const bool pcmFloat = format == FORMAT_FLOAT && (bits == 32 || bits == 64);
  if (!pcmInt && !pcmFloat) {
    error = "unsupported sample format (tag " + std::to_string(format) + ", " +
            std::to_string(bits) + "-bit)";
    return false;
  }

  const size_t bytesPerSample = bits / 8;
  size_t count = pcmBytes / bytesPerSample;
  count -= count % channels;  // Whole frames only

  out.sampleRate = static_cast<int>(sampleRate);
  out.channels = channels;
  out.samples.resize(count);

  for (size_t i = 0; i < count; ++i) {
    const uint8_t* p = pcm + i * bytesPerSample;
    float sample = 0.0f;

    if (pcmFloat && bits == 32) {
      const uint32_t raw = readU32(p);
      std::memcpy(&sample, &raw, sizeof(sample));
    } else if (pcmFloat) {
      const uint64_t raw = readU32(p) | (static_cast<uint64_t>(readU32(p + 4)) << 32);
      double value = 0.0;
      std::memcpy(&value, &raw, sizeof(value));
      sample = static_cast<float>(value);
    } else if (bits == 16) {
      sample = static_cast<int16_t>(readU16(p)) / 32768.0f;
    } else if (bits == 24) {
      int32_t value = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
      if (value & 0x800000) value -= 0x1000000;  // Sign-extend
      sample = static_cast<float>(value / 8388608.0);
    } else {
      sample = static_cast<float>(static_cast<int32_t>(readU32(p)) / 2147483648.0);
    }

    out.samples[i] = sample;
  }

  return true;
}

bool writeWavMono(const std::string& path, const float* samples, std::size_t count,
                  int sampleRate) {
  std::ofstream file(path, std::ios::binary);
  if (!file) return false;

  const auto put16 = [&](uint16_t value) {
    const char bytes[2] = {static_cast<char>(value), static_cast<char>(value >> 8)};
    file.write(bytes, 2);
  };
  const auto put32 = [&](uint32_t value) {
    const char bytes[4] = {static_cast<char>(value), static_cast<char>(value >> 8),
                           static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    file.write(bytes, 4);
  };

  const uint32_t rate = static_cast<uint32_t>(sampleRate);
  const uint32_t dataBytes = static_cast<uint32_t>(count * sizeof(float));

  // Non-PCM formats want the 18-byte fmt (with cbSize) and a fact chunk.
  file.write("RIFF", 4);
  put32(4 + (8 + 18) + (8 + 4) + (8 + dataBytes));
  file.write("WAVE", 4);

  file.write("fmt ", 4);
  put32(18);
  put16(FORMAT_FLOAT);
  put16(1);         // Channels
  put32(rate);
  put32(rate * 4);  // Bytes per second
  put16(4);         // Block align
  put16(32);        // Bits per sample
  put16(0);         // cbSize

  file.write("fact", 4);
  put32(4);
  put32(static_cast<uint32_t>(count));

  file.write("data", 4);
  put32(dataBytes);
  // Samples go out in host order, which is little-endian on every platform
  // this builds for (x86-64 and ARM64).
  file.write(reinterpret_cast<const char*>(samples), static_cast<std::streamsize>(dataBytes));

  return static_cast<bool>(file);
}
