#pragma once
// Panel-native SoundSense-compatible controls. No upstream SoundSense code is
// included. PDM capture is signed 16-bit PCM, regardless of the I2S slot setting.
// The PDM line carries two microphones: channel 0 is the board's right
// microphone and channel 1 its left one (checked on the panel, 2026-11-24).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace onx_sound_sense {
// Assist and the wake word listen to channel 1 (the left microphone), as the
// mono `channel: right` build did; the ambient level follows the same one.
inline constexpr uint8_t kAssistChannel = 1;
inline constexpr int kMaxLag = 2;  // 4 cm spacing at 16 kHz is at most 1.9 samples.

class Meter {
 public:
  std::atomic<bool> capture{false};
  // `data` is interleaved 16-bit PCM with `channels` channels (1 or 2).
  void feed(const uint8_t *data, size_t size, uint32_t now, uint8_t channels = 1) {
    if (!capture.load(std::memory_order_relaxed)) return;
    channels = std::clamp<uint8_t>(channels, 1, 2);
    const size_t frames = size / (2 * channels);
    if (frames == 0) return;
    auto sample = [&](size_t frame, uint8_t channel) {
      int16_t value;
      std::memcpy(&value, data + 2 * (frame * channels + std::min<uint8_t>(channel, channels - 1)), sizeof(value));
      return int32_t(value);
    };
    uint64_t energy[2]{};
    int64_t cross[2 * kMaxLag + 1]{};
    for (size_t f = 0; f < frames; ++f) {
      const int32_t ch0 = sample(f, 0), ch1 = sample(f, 1);
      energy[0] += int64_t(ch0) * ch0;
      energy[1] += int64_t(ch1) * ch1;
      if (channels < 2) continue;
      // cross[k] pairs ch0[f] with ch1[f + k]: a peak at k > 0 means the
      // sound reached channel 0 first.
      for (int k = -kMaxLag; k <= kMaxLag; ++k) {
        const int64_t g = int64_t(f) + k;
        if (g >= 0 && g < int64_t(frames)) cross[k + kMaxLag] += int64_t(ch0) * sample(size_t(g), 1);
      }
    }
    for (int c = 0; c < 2; ++c) energy_[c].store(uint32_t(energy[c] / frames), std::memory_order_relaxed);
    stereo_.store(channels > 1, std::memory_order_relaxed);
    if (channels > 1) {
      // Smoothed over ~10 chunks (~160 ms) so 2 s reports are not one chunk.
      constexpr float kAlpha = 0.1f;
      const bool first = !pair_valid_.load(std::memory_order_relaxed);
      for (int c = 0; c < 2; ++c) pair_energy_[c] += (first ? 1.0f : kAlpha) * (float(energy[c]) - pair_energy_[c]);
      for (int k = 0; k < 2 * kMaxLag + 1; ++k) pair_cross_[k] += (first ? 1.0f : kAlpha) * (float(cross[k]) - pair_cross_[k]);
      pair_valid_.store(true, std::memory_order_relaxed);
      const float norm = std::sqrt(pair_energy_[0] * pair_energy_[1]);
      int peak = 0;
      for (int k = 1; k < 2 * kMaxLag + 1; ++k) if (pair_cross_[k] > pair_cross_[peak]) peak = k;
      float lag = float(peak - kMaxLag);
      if (peak > 0 && peak < 2 * kMaxLag) {
        const float a = pair_cross_[peak - 1], b = pair_cross_[peak], c = pair_cross_[peak + 1];
        const float d = a - 2 * b + c;
        if (d < 0) lag += 0.5f * (a - c) / d;
      }
      correlation_.store(norm > 0 ? std::clamp(pair_cross_[kMaxLag] / norm, -1.0f, 1.0f) : NAN, std::memory_order_relaxed);
      lag_.store(norm > 0 ? lag : NAN, std::memory_order_relaxed);
    }
    at_.store(now, std::memory_order_relaxed);
    valid_.store(true, std::memory_order_release);
  }
  // Ambient level of the Assist microphone.
  float level(uint32_t now) const { return level(now, stereo_.load() ? kAssistChannel : 0); }
  // Level of one channel (0 or 1); NAN for channel 1 of a mono capture.
  float level(uint32_t now, uint8_t channel) const {
    if (!fresh_(now) || (channel > 0 && !stereo_.load())) return NAN;
    const float energy = energy_[std::min<uint8_t>(channel, 1)].load();
    // Relative dB scale: full scale = 90. This is not calibrated acoustic SPL.
    return energy > 0 ? std::clamp(10.0f * std::log10(energy / 1073741824.0f) + 90.0f, 0.0f, 90.0f) : 0.0f;
  }
  // Zero-lag correlation of the two microphones (-1..1).
  float correlation(uint32_t now) const { return fresh_(now) && stereo_.load() ? correlation_.load() : NAN; }
  // Arrival offset in samples; positive when channel 0 hears first.
  float lag_samples(uint32_t now) const { return fresh_(now) && stereo_.load() ? lag_.load() : NAN; }
  void reset() {
    valid_.store(false);
    pair_valid_.store(false);  // The next chunk reseeds the smoothing.
  }
 private:
  bool fresh_(uint32_t now) const {
    return capture.load() && valid_.load(std::memory_order_acquire) && now - at_.load() <= 1500;
  }
  std::atomic<uint32_t> energy_[2]{}, at_{0};
  std::atomic<float> correlation_{NAN}, lag_{NAN};
  std::atomic<bool> valid_{false}, stereo_{false};
  // Smoothing state, written by the feeder only.
  std::atomic<bool> pair_valid_{false};
  float pair_energy_[2]{}, pair_cross_[2 * kMaxLag + 1]{};
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
