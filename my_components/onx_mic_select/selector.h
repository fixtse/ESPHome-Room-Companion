#pragma once
// Chooses one microphone of a stereo pair, by signal-to-noise ratio, and
// writes it as mono. No ESPHome dependencies, so the host tests use it too.
//
// Each channel keeps a noise floor (falls fast, rises ~3 dB per 5 s) and,
// while there is sound above that floor, a smoothed SNR score. The other
// microphone takes over when its score is better by more than kHysteresisDb,
// or at once when the current one goes silent (dead or disconnected) while
// the other still hears sound. A switch crossfades over one chunk.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace onx_mic_select {

// On the Dual MIC board, stereo channel 0 is the right microphone and
// channel 1 the left one (measured on the panel, 2026-11-24).
inline constexpr uint8_t kRightMic = 0;
inline constexpr uint8_t kLeftMic = 1;

enum Mode : uint8_t { MODE_AUTO = 0, MODE_LEFT, MODE_RIGHT };

class Selector {
 public:
  static constexpr float kHysteresisDb = 1.5f;
  static constexpr float kActivityDb = 6.0f;      // Sound this far above the floor scores.
  static constexpr float kScoreAlpha = 0.05f;     // ~0.3 s at 16 ms chunks.
  static constexpr float kFloorFall = 0.3f;
  static constexpr float kFloorRise = 1.0025f;    // Per chunk: ~+3 dB per 5 s.
  static constexpr float kSilentEnergy = 4.0f;    // Below 2 LSB RMS.
  static constexpr float kHearingEnergy = 400.0f; // Above 20 LSB RMS.
  static constexpr int kDeadChunks = 60;          // ~1 s.

  std::atomic<uint8_t> mode{MODE_AUTO};

  // `in` is interleaved stereo (frames * 2 samples); `out` gets `frames`.
  void process(const int16_t *in, size_t frames, int16_t *out) {
    if (frames == 0) return;
    float energy[2]{};
    for (size_t f = 0; f < frames; ++f)
      for (int c = 0; c < 2; ++c) energy[c] += float(in[2 * f + c]) * float(in[2 * f + c]);
    float snr[2];
    for (int c = 0; c < 2; ++c) {
      energy[c] /= float(frames);
      if (!initialized_) floor_[c] = std::max(energy[c], 1.0f);
      else if (energy[c] < floor_[c]) floor_[c] += kFloorFall * (energy[c] - floor_[c]);
      else floor_[c] *= kFloorRise;
      floor_[c] = std::max(floor_[c], 1.0f);
      snr[c] = 10.0f * std::log10(std::max(energy[c], 1.0f) / floor_[c]);
    }
    for (int c = 0; c < 2; ++c) {
      const bool silent = energy[c] < kSilentEnergy && energy[1 - c] > kHearingEnergy;
      silent_chunks_[c] = silent ? std::min(silent_chunks_[c] + 1, kDeadChunks) : 0;
    }
    if (!initialized_ || std::max(snr[0], snr[1]) > kActivityDb) {
      for (int c = 0; c < 2; ++c) score_[c] += (initialized_ ? kScoreAlpha : 1.0f) * (snr[c] - score_[c]);
    }
    initialized_ = true;

    const uint8_t from = current_.load(std::memory_order_relaxed);
    const uint8_t to = choose_(from);
    if (to == from) {
      for (size_t f = 0; f < frames; ++f) out[f] = in[2 * f + from];
      return;
    }
    for (size_t f = 0; f < frames; ++f) {
      const float w = float(f + 1) / float(frames);
      out[f] = int16_t(std::lround((1.0f - w) * in[2 * f + from] + w * in[2 * f + to]));
    }
    current_.store(to, std::memory_order_relaxed);
    switches_.fetch_add(1, std::memory_order_relaxed);
  }

  uint8_t channel() const { return current_.load(std::memory_order_relaxed); }
  uint32_t switches() const { return switches_.load(std::memory_order_relaxed); }
  bool dead(uint8_t channel) const { return silent_chunks_[channel & 1] >= kDeadChunks; }
  void reset() {
    initialized_ = false;
    silent_chunks_[0] = silent_chunks_[1] = 0;
  }

 private:
  uint8_t choose_(uint8_t from) const {
    const uint8_t forced = mode.load(std::memory_order_relaxed);
    if (forced == MODE_LEFT) return kLeftMic;
    if (forced == MODE_RIGHT) return kRightMic;
    const uint8_t other = 1 - from;
    if (dead(from) && !dead(other)) return other;
    if (!dead(other) && score_[other] > score_[from] + kHysteresisDb) return other;
    return from;
  }

  bool initialized_{false};
  float floor_[2]{1.0f, 1.0f};
  float score_[2]{};
  int silent_chunks_[2]{};
  // Assist used this one (the mono `channel: right` slot) before selection.
  std::atomic<uint8_t> current_{kLeftMic};
  std::atomic<uint32_t> switches_{0};
};

}  // namespace onx_mic_select
