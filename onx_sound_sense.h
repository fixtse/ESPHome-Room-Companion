#pragma once
// Panel-native SoundSense-compatible controls. No upstream SoundSense code is
// included. PDM capture is signed 16-bit PCM, regardless of the I2S slot setting.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace onx_sound_sense {
class Meter {
 public:
  std::atomic<bool> capture{false};
  void feed(const uint8_t *data, size_t size, uint32_t now) {
    if (!capture.load(std::memory_order_relaxed) || size < 2) return;
    uint64_t energy = 0;
    uint32_t count = 0;
    // Decimate computation only, without allocating or retaining audio buffers.
    for (size_t i = 0; i + 1 < size; i += 8) {
      int16_t sample;
      std::memcpy(&sample, data + i, sizeof(sample));
      energy += int64_t(sample) * sample;
      ++count;
    }
    energy_.store(static_cast<uint32_t>(energy / count), std::memory_order_relaxed);
    at_.store(now, std::memory_order_relaxed);
    valid_.store(true, std::memory_order_release);
  }
  float level(uint32_t now) const {
    if (!capture.load() || !valid_.load(std::memory_order_acquire) || now - at_.load() > 1500) return NAN;
    const float energy = energy_.load();
    // Relative dB scale: full scale = 90. This is not calibrated acoustic SPL.
    return energy > 0 ? std::clamp(10.0f * std::log10(energy / 1073741824.0f) + 90.0f, 0.0f, 90.0f) : 0.0f;
  }
  void reset() { valid_.store(false); }
 private:
  std::atomic<uint32_t> energy_{0}, at_{0};
  std::atomic<bool> valid_{false};
};
struct Detector {
  bool quiet{false}, elevated{false}, spike{false}, initialized{false};
  uint32_t quiet_since{0}, elevated_since{0}, spike_at{0};
  float previous{0};
  const char *update(float level, float silence, float presence, uint32_t now) {
    if (!std::isfinite(level)) { *this = {}; return "unknown"; }
    if (initialized && level - previous >= 12.0f) { spike = true; spike_at = now; }
    previous = level; initialized = true;
    if (level < silence) {
      if (!quiet) quiet_since = now;
      quiet = true; elevated = false; spike = false;
      return now - quiet_since >= 15000 ? "silence" : "quieting";
    }
    quiet = false;
    if (level >= presence) {
      if (!elevated) elevated_since = now;
      elevated = true;
      if (now - elevated_since >= 12000) return "presence";
    } else elevated = false;
    if (spike && now - spike_at < 8000) return "noise";
    spike = false;
    return "active";
  }
};
inline float volume_target(float db, float minimum_percent, float response) {
  if (!std::isfinite(db) || !std::isfinite(minimum_percent) || !std::isfinite(response)) return NAN;
  const float base = std::clamp(minimum_percent / 100.0f, 0.0f, 1.0f);
  const float noise = std::clamp((db - 30.0f) / 45.0f, 0.0f, 1.0f);
  return std::clamp(base + (1.0f - base) * noise * std::clamp(response / 10.0f, 0.0f, 1.0f), base, 1.0f);
}
inline Meter meter;
inline Detector detector;
}  // namespace onx_sound_sense
