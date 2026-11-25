#include "../my_components/onx_icons/onx_icons.cpp"
#include <cassert>

static uint32_t emoji_done_index = 0;
static uint8_t *emoji_done_pixels = nullptr;
static int emoji_done_calls = 0;
static void emoji_done(uint32_t index, uint8_t *pixels) {
  emoji_done_index = index;
  emoji_done_pixels = pixels;
  emoji_done_calls++;
}

struct Downloader : esphome::onx_icons::IconDownloader {
  size_t cached() const { return cache_.size(); }
  void step_to_idle() {
    for (int i = 0; i < 64 && busy(); ++i) {
      finish_icon_task();
      loop();
    }
  }
};

int main() {
  uint8_t fallback_pixels[4] = {255, 255, 255, 255};
  esphome::image::Image fallback(fallback_pixels, 1, 1, esphome::image::IMAGE_TYPE_RGB,
                                 esphome::image::TRANSPARENCY_ALPHA_CHANNEL);
  lv_obj_t widget, widget2, widget3;
  Downloader download;

  // 1. Initial statuses + basic bind + single-read stall recovery + immediate widget rebind + worker reaping.
  assert(download.status("") == "missing_icon_metadata");
  assert(download.status("mdi:weather-fog") == "awaiting_display");
  stall_once = true;
  download.bind(&widget, "mdi:ceiling-fan", &fallback);
  assert(download.busy() && download.status("mdi:ceiling-fan") == "downloading");
  assert(widget.source == fallback.get_lv_image_dsc());
  finish_icon_task();
  download.loop();  // Harvests ceiling-fan, immediately rebinds widget, signals worker stop.
  assert(download.cached() == 1);
  assert(widget.source != fallback.get_lv_image_dsc());  // Rebound immediately without second bind()!
  assert(download.cache_revision() == 1);
  download.step_to_idle();  // Worker closes keep-alive socket, suspends, and is reaped.
  assert(!download.busy() && icon_task_created_count == 1 && icon_task_deleted_count == 1);
  assert(http_init_count == http_cleanup_count);
  // The parser scratch allocation exceeds the typical available internal block.
  assert(largest_external_allocation > 32768);
  assert(external_allocations.size() == 1);  // Only the cached RGBA bitmap survives.
  assert(download.status("mdi:ceiling-fan") == "ready");

  // Cached bind does not hit network again.
  const auto count_after_first = requested_icon_urls.size();
  download.bind(&widget, "mdi:ceiling-fan", &fallback);
  assert(requested_icon_urls.size() == count_after_first);

  // 2. Priority queue + HTTP/1.1 Keep-Alive reuse across a burst!
  // Enqueue a background prefetch first, then two urgent visible binds while busy:
  // urgent icons must NOT be dropped and must beat subsequent prefetches, all over ONE keep-alive client!
  const int inits_before_burst = http_init_count;
  download.bind(&widget, "mdi:lamp", &fallback);
  download.prefetch("mdi:sofa");
  download.bind(&widget2, "mdi:desk-lamp", &fallback);
  download.bind(&widget3, "mdi:lightbulb", &fallback);
  assert(download.status("mdi:desk-lamp") == "queued");
  assert(download.urgent_queued() == 2 && download.prefetch_queued() == 1);

  std::vector<std::string> burst_urls_before = requested_icon_urls;
  download.step_to_idle();
  assert(!download.busy() && download.cached() == 5);
  assert(widget.source != fallback.get_lv_image_dsc());
  assert(widget2.source != fallback.get_lv_image_dsc());
  assert(widget3.source != fallback.get_lv_image_dsc());
  // All 4 icons in the burst reused ONE esp_http_client handle (1 init, 3 keep-alive reuses)!
  assert(http_init_count - inits_before_burst == 1);
  assert(download.stats().keepalive_reuses >= 3);
  assert(http_init_count == http_cleanup_count);
  // Urgent desk-lamp & lightbulb downloaded BEFORE background prefetch sofa.
  const size_t base = burst_urls_before.size();
  assert(requested_icon_urls[base].find("/lamp.svg") != std::string::npos);
  assert(requested_icon_urls[base + 1].find("/desk-lamp.svg") != std::string::npos);
  assert(requested_icon_urls[base + 2].find("/lightbulb.svg") != std::string::npos);
  assert(requested_icon_urls[base + 3].find("/sofa.svg") != std::string::npos);

  // 3. Stale keep-alive socket recovery: 2nd request in burst gets fetch_headers == -1,
  // downloader transparently reconnects once on a fresh client and succeeds!
  download.bind(&widget, "mdi:fan", &fallback);
  download.bind(&widget2, "mdi:air-conditioner", &fallback);
  finish_icon_task();
  download.loop();  // fan finishes, air-conditioner dispatched on same keep-alive worker.
  stale_keepalive_once = true;
  download.step_to_idle();
  assert(!download.busy() && download.status("mdi:air-conditioner") == "ready");

  // 4. Fast transient connect retry recovers transparently on a single quick open failure.
  fail_open_once = true;
  download.bind(&widget, "mdi:thermometer", &fallback);
  download.step_to_idle();
  assert(download.status("mdi:thermometer") == "ready");

  // 5. Truncated peer-EOF aborts fast (not spinning for 10,000 ms).
  const uint32_t clock_before_trunc = icon_clock;
  truncate_eof = true;
  download.bind(&widget, "mdi:broken-stream", &fallback);
  download.step_to_idle();
  truncate_eof = false;
  assert(download.status("mdi:broken-stream") == "retry_wait");
  assert(icon_clock - clock_before_trunc < 100);  // Aborted in a handful of ticks, not 10 s!
  assert(download.error("mdi:broken-stream").find("truncated") != std::string::npos);

  // 6. Primary failure -> backup succeeds -> primary enters 60 s cooldown so next icon skips primary!
  fail_primary = true;
  download.bind(&widget, "mdi:radiator", &fallback);
  download.step_to_idle();
  assert(download.status("mdi:radiator") == "ready");
  assert(requested_icon_urls.back() == onx_icon_state::backup_url("mdi:radiator"));
  assert(download.primary_in_cooldown());
  assert(download.stats().backup_ok >= 1);

  const size_t urls_before_cd = requested_icon_urls.size();
  download.bind(&widget, "mdi:heater", &fallback);
  download.step_to_idle();
  assert(download.status("mdi:heater") == "ready");
  // Went straight to backup (1 URL request, NOT 2!).
  assert(requested_icon_urls.size() == urls_before_cd + 1);
  assert(requested_icon_urls.back() == onx_icon_state::backup_url("mdi:heater"));

  // After cooldown expires (or on_network_connected), primary is tried again.
  fail_primary = false;
  icon_clock += 65000;
  assert(!download.primary_in_cooldown());
  download.bind(&widget, "mdi:fire", &fallback);
  download.step_to_idle();
  assert(requested_icon_urls.back() == onx_icon_state::download_url("mdi:fire"));

  // 7. Transient failure vs permanent 404 + on_network_connected() vs explicit retry().
  fail_all = true;
  download.bind(&widget, "mdi:music", &fallback);
  download.step_to_idle();
  assert(download.status("mdi:music") == "retry_wait" && !download.error("mdi:music").empty());
  const auto failures_before = requested_icon_urls.size();
  download.bind(&widget, "mdi:music", &fallback);
  assert(!download.busy() && requested_icon_urls.size() == failures_before);

  fail_all = false;
  fail_404_all = true;
  download.bind(&widget, "mdi:does-not-exist", &fallback);
  download.step_to_idle();
  fail_404_all = false;
  assert(download.status("mdi:does-not-exist") == "retry_wait");

  // Network reconnect clears transient failures (mdi:music) immediately, keeps permanent 404.
  download.on_network_connected();
  assert(download.status("mdi:music") == "awaiting_display");
  assert(download.status("mdi:does-not-exist") == "retry_wait");

  // Explicit retry() clears permanent 404 too.
  download.retry();
  assert(download.status("mdi:does-not-exist") == "awaiting_display");
  download.bind(&widget, "mdi:music", &fallback);
  download.step_to_idle();
  assert(download.status("mdi:music") == "ready");

  // Invalid icon name is rejected.
  download.bind(&widget, "mdi:../../secret", &fallback);
  assert(!download.busy() && widget.source == fallback.get_lv_image_dsc());

  // 8. Artwork preemption cancels in-flight background prefetch without recording failure and resumes after!
  download.prefetch("mdi:bed");
  download.prefetch("mdi:chair-rolling");
  assert(download.busy());
  // Simulate artwork requesting yield mid-fetch on the background prefetch.
  on_http_open = [&](const std::string &) { download.request_artwork_yield(500); };
  finish_icon_task();
  on_http_open = nullptr;
  download.loop();
  download.step_to_idle();
  assert(!download.busy());
  assert(download.stats().cancelled >= 1);
  // Preempted prefetch was NOT marked as failed; it stays queued for when artwork clears!
  assert(download.error("mdi:bed").empty());
  assert(download.status("mdi:bed") == "waiting_for_artwork");
  // Once yield window expires (or artwork finishes), prefetches automatically drain!
  icon_clock += 600;
  download.loop();
  download.step_to_idle();
  assert(download.status("mdi:bed") == "ready");
  assert(download.status("mdi:chair-rolling") == "ready");

  // 9. Emoji fetched through the same worker (ahead of background prefetch, behind urgent icons) in color.
  onx_emoji_queue::completed = emoji_done;
  onx_emoji_queue::pending.push_back({7, "1f600"});
  download.loop();
  assert(download.busy() && onx_emoji_queue::pending.empty());
  download.step_to_idle();
  assert(requested_icon_urls.back() == "https://fonts.gstatic.com/s/e/notoemoji/latest/1f600/emoji.svg");
  assert(!download.busy() && emoji_done_calls == 1 && emoji_done_index == 7 && emoji_done_pixels);
  // Opaque white source survives the RGBA->BGRA swap.
  {
    const auto *c = emoji_done_pixels + (24 * 48 + 24) * 4;
    assert(c[0] == 255 && c[1] == 255 && c[2] == 255 && c[3] == 255);
  }
  heap_caps_free(emoji_done_pixels);

  onx_emoji_queue::pending.push_back({8, "1f1f5_1f1ea"});
  fail_all = true;
  download.loop();
  download.step_to_idle();
  fail_all = false;
  assert(requested_icon_urls[requested_icon_urls.size() - 2] ==
             "https://fonts.gstatic.com/s/e/notoemoji/latest/1f1f5_1f1ea/emoji.svg" &&
         requested_icon_urls.back().find("waved-svg/emoji_u1f1f5_1f1ea.svg") != std::string::npos);
  assert(emoji_done_calls == 2 && emoji_done_pixels == nullptr);

  onx_emoji_queue::pending.push_back({9, "../x"});
  download.loop();
  download.step_to_idle();
  assert(emoji_done_calls == 3 && emoji_done_pixels == nullptr);

  // Every HTTP client opened was cleanly closed and freed.
  assert(http_init_count == http_cleanup_count);
  assert(icon_task_created_count == icon_task_deleted_count);
}
