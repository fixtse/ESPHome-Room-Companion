#pragma once
#include "esphome/core/component.h"
#include "onx_icon_state.h"
#include "onx_emoji_queue.h"
#include "esphome/core/helpers.h"
#include "esphome/components/image/image.h"
#include <deque>
#include <map>
#include <memory>
#include <string>

namespace esphome::onx_icons {
class IconDownloader : public Component {
 public:
  // Diagnostics for the most recent icon/emoji fetch.
  struct FetchStats {
    uint32_t attempts{0}, open_ms{0}, read_ms{0}, raster_ms{0}, bytes{0};
    uint32_t heap_free_start{0}, heap_largest_start{0}, heap_free_after_open{0};
    bool stack_psram{false}, reused{false}, backup{false};
    std::string host, icon;
  };
  // Cumulative outcomes since boot.
  struct Stats {
    uint32_t attempts{0}, ok{0}, failed{0}, cancelled{0};
    uint32_t cache_hits{0}, keepalive_reuses{0}, backup_ok{0};
    uint32_t last_ms{0}, slowest_ms{0};
    std::string last_error;
    FetchStats last_fetch;
  };

  void loop() override;
  bool busy() const { return busy_ || worker_ != nullptr; }
  static bool memory_ready();
  void set_artwork_busy(bool value);
  // Preempt background prefetch and close the burst worker so media artwork can start quickly.
  void request_artwork_yield(uint32_t window_ms = 2000);
  void prefetch(const std::string &icon);
  void retry();
  void on_network_connected();
  std::string status(const std::string &icon) const;
  std::string error(const std::string &icon) const {
    auto found = errors_.find(icon); return found == errors_.end() ? "" : found->second;
  }
  const Stats &stats() const { return stats_; }
  size_t cache_size() const { return cache_.size(); }
  size_t cache_bytes() const;
  size_t urgent_queued() const { return urgent_queue_.size(); }
  size_t prefetch_queued() const { return prefetch_queue_.size(); }
  const std::string &active_icon() const { return active_icon_; }
  bool primary_in_cooldown() const;
  uint32_t cache_revision() const { return cache_revision_; }

  // Stable per-widget ownership keeps LVGL descriptors alive across cache eviction.
  void bind(lv_obj_t *widget, const std::string &icon, image::Image *fallback, bool active = false);
  void unbind(lv_obj_t *widget) { this->bindings_.erase(widget); }

 protected:
  struct Bitmap {
    uint8_t *pixels;
    image::Image image;
    uint32_t used{0};
    explicit Bitmap(uint8_t *p);
    ~Bitmap();
  };
  static void download_task_(void *arg);
  void reap_worker_();
  bool network_and_memory_ready_() const;
  bool artwork_hold_active_(uint32_t now) const;
  bool can_start_() const;
  bool is_queued_(const std::string &icon) const;
  void enqueue_urgent_(const std::string &icon);
  void enqueue_prefetch_(const std::string &icon);
  bool pick_next_(std::string &key, bool &is_prefetch);
  void drain_();
  bool start_(const std::string &key, bool is_prefetch);
  bool cancelled_();
  void record_failure_(const std::string &icon, const std::string &error, bool permanent, uint32_t now);
  void rebind_ready_(const std::string &icon, const std::shared_ptr<Bitmap> &bitmap);

  void *worker_{nullptr};  // Burst-scoped WithCaps TaskHandle_t; reaped once suspended.
  bool worker_stack_psram_{false};
  // Worker-task-only HTTP/1.1 keep-alive state (always closed before task suspension).
  void *ka_handle_{nullptr};
  std::string ka_host_;
  std::string active_icon_;
  bool active_is_prefetch_{false};
  uint32_t started_ms_{0};
  std::deque<std::string> urgent_queue_;
  std::deque<std::string> prefetch_queue_;
  uint32_t emoji_index_{0};
  uint32_t cache_clock_{0};
  uint32_t cache_revision_{0};
  std::map<std::string, std::shared_ptr<Bitmap>> cache_;
  struct Binding {
    std::string icon;
    std::shared_ptr<Bitmap> bitmap;
    const void *source{nullptr};
    bool active{false}, styled{false};
  };
  std::map<lv_obj_t *, Binding> bindings_;
  std::map<std::string, ::onx_icon_state::Retry> failures_;
  std::map<std::string, std::string> errors_;
  uint32_t primary_fail_streak_{0};
  uint32_t primary_cooldown_until_ms_{0};
  uint32_t artwork_yield_until_ms_{0};
  Stats stats_;

  Mutex mutex_;
  bool busy_{false}, completed_{false}, artwork_busy_{false};
  bool stop_worker_{false}, cancel_requested_{false};
  std::string requested_icon_;
  bool requested_prefer_backup_{false};
  uint32_t requested_heap_free_{0}, requested_heap_largest_{0};
  std::string completed_icon_, completed_error_;
  uint8_t *completed_pixels_{nullptr};
  bool completed_cancelled_{false}, completed_all_404_{false};
  bool completed_primary_host_err_{false}, completed_primary_ok_{false};
  FetchStats completed_stats_;
};
}  // namespace esphome::onx_icons
