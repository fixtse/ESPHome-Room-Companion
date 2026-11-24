#pragma once
// Mono microphone that forwards the better of a stereo pair (see selector.h).
// Start/stop are passed to the stereo source, which counts its listeners.
#include "esphome/components/microphone/microphone.h"
#include "esphome/core/component.h"
#include "selector.h"

#include <span>
#include <vector>

namespace esphome::onx_mic_select {

class MicSelect : public microphone::Microphone, public Component {
 public:
  void set_source(microphone::Microphone *source) { this->source_ = source; }
  float get_setup_priority() const override { return setup_priority::DATA; }

  void setup() override {
    this->audio_stream_info_ = audio::AudioStreamInfo(16, 1, 16000);
    this->source_->add_data_callback([this](const microphone::AudioCapture &capture) { this->on_capture_(capture); });
  }

  void loop() override {
    if (this->source_->is_running()) this->state_ = microphone::STATE_RUNNING;
    else if (this->source_->is_stopped()) this->state_ = microphone::STATE_STOPPED;
    else this->state_ = microphone::STATE_STARTING;
  }

  void start() override { this->source_->start(); }
  void stop() override { this->source_->stop(); }

  void set_mode(uint8_t mode) { this->selector_.mode.store(mode); }
  uint8_t channel() const { return this->selector_.channel(); }
  bool is_left() const { return this->selector_.channel() == ::onx_mic_selector::kLeftMic; }
  uint32_t switches() const { return this->selector_.switches(); }

 protected:
  // Runs on the source microphone's task.
  void on_capture_(const microphone::AudioCapture &capture) {
    if (!this->has_data_callbacks_() || capture.stream_info.get_bits_per_sample() != 16) return;
    const auto *in = reinterpret_cast<const int16_t *>(capture.data.data());
    if (capture.stream_info.get_channels() == 1) {
      this->buffer_.assign(in, in + capture.data.size() / 2);
    } else {
      const size_t frames = capture.data.size() / 4;
      this->buffer_.resize(frames);
      this->selector_.process(in, frames, this->buffer_.data());
    }
    this->publish_audio_(std::span<uint8_t>(reinterpret_cast<uint8_t *>(this->buffer_.data()), this->buffer_.size() * 2),
                         this->audio_stream_info_, capture.capture_time_us);
  }

  microphone::Microphone *source_{nullptr};
  ::onx_mic_selector::Selector selector_;
  std::vector<int16_t> buffer_;
};

}  // namespace esphome::onx_mic_select
