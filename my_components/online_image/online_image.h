// SPDX-License-Identifier: GPL-3.0-only
// ESPHome 2026.10.2 OnlineImage API with a bounded background network fetch.
#pragma once
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/runtime_image/runtime_image.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome::online_image {
class OnlineImage final : public PollingComponent,
                          public runtime_image::RuntimeImage,
                          public Parented<http_request::HttpRequestComponent> {
 public:
  OnlineImage(const std::string &url, int width, int height, runtime_image::ImageFormat format,
              image::ImageType type, image::Transparency transparency, image::Image *placeholder,
              uint32_t buffer_size, bool is_big_endian = false);
  ~OnlineImage();
  void update() override;
  void loop() override;
  void update_if(bool value) { if (value) update(); }
  void set_url(const std::string &url) { if (validate_url_(url)) url_=url; }
  void release();
  bool is_busy() const { return busy_ || worker_ != nullptr; }
  void set_source_pixel_limit(uint32_t pixels) { source_pixel_limit_=pixels; }
  void set_allow_redirects(bool allow) { allow_redirects_ = allow; }
  void set_cache_entries(size_t max_entries, size_t max_total_bytes = 512 * 1024) {
    cache_max_entries_ = max_entries;
    cache_max_bytes_ = max_total_bytes;
  }
  static bool memory_ready();
  const std::string &last_error() const { return last_error_; }
  // Diagnostics for the most recent fetch (start heap is sampled in update()).
  struct FetchStats {
    uint32_t attempts{0}, open_ms{0}, read_ms{0}, bytes{0};
    uint32_t heap_free_start{0}, heap_largest_start{0}, heap_free_after_open{0};
    bool https{false}, stack_psram{false}, cached{false};
    std::string host;
  };
  const FetchStats &last_stats() const { return stats_; }
  template<typename V> void add_request_header(const std::string &header,V value) {
    request_headers_.emplace_back(header,value);
  }
  template<typename F> void add_on_finished_callback(F &&callback) {
    finished_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_error_callback(F &&callback) {
    failed_.add(std::forward<F>(callback));
  }
 protected:
  struct Work {
    OnlineImage *owner;
    std::string url;
    std::vector<http_request::Header> headers;
    uint32_t generation, open_timeout_ms, read_timeout_ms, budget_ms;
    bool allow_redirects;
    FetchStats stats;
  };
  struct CacheEntry {
    std::string url;
    uint8_t *data{nullptr};
    size_t size{0};
    uint32_t used{0};
  };
  static void fetch_task_(void *arg);
  bool validate_url_(const std::string &url);
  bool cancelled_(uint32_t generation);
  void cache_store_(const std::string &url, const uint8_t *data, size_t size);
  const CacheEntry *cache_find_(const std::string &url);
  std::string url_;
  std::vector<std::pair<std::string,TemplatableValue<std::string>>> request_headers_;
  CallbackManager<void(bool)> finished_;
  CallbackManager<void()> failed_;
  Mutex mutex_;
  bool allow_redirects_{true};
  bool busy_{false}, completed_{false}, completed_cached_{false};
  void *worker_{nullptr};  // WithCaps task; loop() deletes it once suspended
  void reap_worker_();
  uint32_t generation_{0}, completed_generation_{0};
  uint8_t *completed_data_{nullptr};
  size_t completed_size_{0};
  std::string completed_url_;
  uint32_t source_pixel_limit_{0};
  std::string completed_error_, last_error_;
  FetchStats stats_, completed_stats_;
  std::vector<CacheEntry> cache_;
  size_t cache_max_entries_{0}, cache_max_bytes_{0}, cache_bytes_{0};
  uint32_t cache_clock_{0};
};
}
