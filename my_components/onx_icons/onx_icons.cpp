#include "onx_geom.h"
#include "onx_icons.h"
#include "onx_icon_state.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <new>
#include <vector>
// NanoSVG's parser alone requires a large contiguous allocation. Keep all SVG
// working memory in external RAM rather than the constrained internal/DMA heap.
static void *onx_svg_malloc(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static void *onx_svg_realloc(void *p, size_t size) { return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#define NSVG_MALLOC onx_svg_malloc
#define NSVG_REALLOC onx_svg_realloc
#define NSVG_FREE heap_caps_free
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#undef NSVG_MALLOC
#undef NSVG_REALLOC
#undef NSVG_FREE

namespace esphome::onx_icons {
// Icons are rasterised at the layout's 80 px icon size, scaled for the panel
// (onx_geom::scale is 1 on the 2.8" panel).
static size_t icon_px() { return static_cast<size_t>(onx_geom::S(80)); }
static constexpr size_t MAX_SVG_BYTES = 32768;
static constexpr size_t MAX_EMOJI_SVG_BYTES = 64 * 1024;  // Noto SVGs are typically 2-15 KB.
static constexpr size_t READ_BUF_BYTES = 2048;
static constexpr size_t CACHE_SIZE = 120;  // ~25 KB on 3.5", ~14 KB on 2.8"; bounded by PSRAM headroom.
static constexpr size_t CACHE_MIN_FREE_PSRAM = 1536 * 1024;
static constexpr size_t FAILURE_LIMIT = 128;
static constexpr size_t URGENT_QUEUE_LIMIT = 32;
static constexpr size_t PREFETCH_QUEUE_LIMIT = 64;
static constexpr uint32_t WORKER_STACK_BYTES = 16 * 1024;
static constexpr uint32_t OPEN_TIMEOUT_MS = 4500;
static constexpr uint32_t READ_TIMEOUT_MS = 2000;
static constexpr uint32_t SOURCE_BUDGET_MS = 7500;
static constexpr uint32_t TOTAL_BUDGET_MS = 12000;
static constexpr uint32_t PRIMARY_COOLDOWN_MS = 60000;
static const char *const TAG = "onx_icons";

// Keep SVG source text in PSRAM rather than internal heap for both icons and emoji.
using PsramText = std::basic_string<char, std::char_traits<char>, RAMAllocator<char>>;

struct PsramDeleter {
  void operator()(char *p) const noexcept { heap_caps_free(p); }
};

struct KeepAliveClient {
  void *&raw_handle;
  std::string &host;
  esp_http_client_handle_t handle() const { return static_cast<esp_http_client_handle_t>(raw_handle); }
  void set(esp_http_client_handle_t h, const std::string &h_host) {
    raw_handle = h;
    host = h_host;
  }
  void reset() {
    if (raw_handle != nullptr) {
      auto h = handle();
      esp_http_client_close(h);
      esp_http_client_cleanup(h);
      raw_handle = nullptr;
    }
    host.clear();
  }
};

struct FetchOutcome {
  bool ok{false};
  bool cancelled{false};
  bool not_found{false};
  bool host_error{false};
  int status_code{0};
  uint32_t attempts{0}, open_ms{0}, read_ms{0}, bytes{0};
  uint32_t heap_free_after_open{0};
  bool reused{false};
  std::string host, error;
};

static std::string extract_host(const std::string &url) {
  const size_t sep = url.find("://");
  if (sep == std::string::npos) return {};
  const size_t begin = sep + 3;
  const size_t end = url.find_first_of(":/?", begin);
  return url.substr(begin, std::min<size_t>(end == std::string::npos ? url.size() - begin : end - begin, 64));
}

// Fixed HTTPS hosts only; redirects and device credentials are never used.
// Reuses an open HTTP/1.1 keep-alive TLS socket across back-to-back requests to
// the same host, with automatic 1-shot reconnect on stale sockets or fast connect hiccups.
static bool fetch_svg(KeepAliveClient &ka, const std::string &url, PsramText &svg, size_t limit,
                      uint32_t budget_ms, const std::function<bool()> &cancelled, FetchOutcome &out) {
  svg.clear();
  out = FetchOutcome{};
  out.host = extract_host(url);
  if (url.empty() || out.host.empty()) {
    out.error = "invalid icon URL";
    return false;
  }
  const uint32_t started = millis();
  auto remaining_ms = [&]() -> uint32_t {
    const uint32_t elapsed = millis() - started;
    return elapsed < budget_ms ? budget_ms - elapsed : 0;
  };

  bool opened = false;
  int64_t length = -1;
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    if (cancelled()) {
      out.cancelled = true;
      out.error = "cancelled";
      ka.reset();
      return false;
    }
    const uint32_t rem = remaining_ms();
    if (rem < 500) {
      out.host_error = true;
      out.error = "icon fetch timed out";
      ka.reset();
      return false;
    }

    bool using_keepalive = (ka.handle() != nullptr && ka.host == out.host);
    if (using_keepalive) {
      if (esp_http_client_set_url(ka.handle(), url.c_str()) != ESP_OK) {
        ka.reset();
        using_keepalive = false;
      }
    } else {
      ka.reset();
    }

    if (ka.handle() == nullptr) {
      esp_http_client_config_t config{};
      config.url = url.c_str();
      config.crt_bundle_attach = esp_crt_bundle_attach;
      config.timeout_ms = static_cast<int>(std::min<uint32_t>(OPEN_TIMEOUT_MS, rem));
      config.disable_auto_redirect = true;
      config.buffer_size = READ_BUF_BYTES;
      config.keep_alive_enable = true;
      config.user_agent = "RoomCompanion/MDI-icons";
      auto created = esp_http_client_init(&config);
      if (created == nullptr) {
        out.error = "HTTP client allocation failed";
        return false;
      }
      ka.set(created, out.host);
      esp_http_client_set_header(ka.handle(), "Accept", "image/svg+xml");
      esp_http_client_set_header(ka.handle(), "Accept-Encoding", "identity");
      esp_http_client_set_header(ka.handle(), "Connection", "keep-alive");
    }

    esp_http_client_set_timeout_ms(ka.handle(), static_cast<int>(std::min<uint32_t>(OPEN_TIMEOUT_MS, remaining_ms())));
    const uint32_t try_start = millis();
    out.attempts++;
    const auto open_err = esp_http_client_open(ka.handle(), 0);
    out.open_ms = millis() - started;
#ifdef USE_ESP32
    out.heap_free_after_open = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (open_err != ESP_OK) {
      ka.reset();
      // Recover immediately from a stale keep-alive socket, or retry once after a fast
      // transient connect hiccup (never stack slow timeouts).
      if (attempt == 0 && !cancelled()) {
        if (using_keepalive && remaining_ms() >= 2000) continue;
        if (millis() - try_start < 1500 && remaining_ms() >= 3000) {
          vTaskDelay(pdMS_TO_TICKS(150));
          if (!cancelled() && remaining_ms() >= 2000) continue;
        }
      }
      out.host_error = true;
      out.error = std::string("HTTPS connect: ") + esp_err_to_name(open_err);
      return false;
    }

    const uint32_t hdr_rem = remaining_ms();
    if (cancelled()) {
      out.cancelled = true;
      out.error = "cancelled";
      ka.reset();
      return false;
    }
    if (hdr_rem < 250) {
      out.host_error = true;
      out.error = "header timeout";
      ka.reset();
      return false;
    }

    esp_http_client_set_timeout_ms(ka.handle(), static_cast<int>(std::min<uint32_t>(READ_TIMEOUT_MS, hdr_rem)));
    length = esp_http_client_fetch_headers(ka.handle());
    if (length < 0) {
      ka.reset();
      // Peers commonly signal closed keep-alive sockets only when reading response headers.
      if (attempt == 0 && using_keepalive && !cancelled() && remaining_ms() >= 2500) continue;
      out.host_error = true;
      out.error = "header fetch failed";
      return false;
    }

    out.reused = using_keepalive;
    const int status = esp_http_client_get_status_code(ka.handle());
    out.status_code = status;
    if (status != 200) {
      ka.reset();  // Never reuse a socket with an unread error body.
      out.not_found = (status == 404);
      out.host_error = (status == 403 || status == 429 || status >= 500);
      out.error = "HTTP " + std::to_string(status);
      return false;
    }
    if (length > static_cast<int64_t>(limit)) {
      ka.reset();
      out.error = "oversized icon";
      return false;
    }
    opened = true;
    break;
  }

  if (!opened || ka.handle() == nullptr) return false;

  std::unique_ptr<char, PsramDeleter> read_buf(
      static_cast<char *>(heap_caps_malloc(READ_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
  if (!read_buf) {
    ka.reset();
    out.error = "SVG read buffer PSRAM allocation failed";
    return false;
  }
  if (length > 0) svg.reserve(static_cast<size_t>(length) + 1);

  uint32_t last_progress_ms = millis();
  unsigned zero_reads = 0;
  bool complete = false;
  while (remaining_ms() > 0 && !cancelled() && svg.size() <= limit) {
    const uint32_t rem = remaining_ms();
    esp_http_client_set_timeout_ms(ka.handle(), static_cast<int>(std::min<uint32_t>(READ_TIMEOUT_MS, rem)));
    const size_t max_read = std::min<size_t>(READ_BUF_BYTES, (limit + 1) - svg.size());
    const int count = esp_http_client_read(ka.handle(), read_buf.get(), max_read);
    if (count < 0) {
      out.error = "SVG read failed";
      out.host_error = true;
      break;
    }
    if (count > 0) {
      svg.append(read_buf.get(), static_cast<size_t>(count));
      last_progress_ms = millis();
      zero_reads = 0;
      if (svg.size() > limit) {
        out.error = "oversized icon";
        break;
      }
      if (esp_http_client_is_complete_data_received(ka.handle())) {
        complete = true;
        break;
      }
    } else {
      if (esp_http_client_is_complete_data_received(ka.handle())) {
        complete = true;
        break;
      }
      const int err = esp_http_client_get_errno(ka.handle());
      ++zero_reads;
      // Abort fast on peer EOF (err==0 repeatedly), socket error, or stalled stream.
      if ((err != 0 && err != EAGAIN && err != EWOULDBLOCK) ||
          (err == 0 && zero_reads >= 3) ||
          (millis() - last_progress_ms >= READ_TIMEOUT_MS)) {
        out.error = err == 0 ? "SVG response truncated" : "SVG read stalled";
        out.host_error = true;
        break;
      }
      vTaskDelay(1);
    }
  }
  out.read_ms = millis() - started - out.open_ms;
  out.bytes = svg.size();

  if (cancelled()) {
    out.cancelled = true;
    out.error = "cancelled";
    ka.reset();
    return false;
  }
  if (!complete) {
    ka.reset();
    if (out.error.empty()) {
      out.error = "SVG read timed out";
      out.host_error = true;
    }
    return false;
  }
  const bool valid = !svg.empty() && svg.size() <= limit && svg.find("<svg") != PsramText::npos;
  if (!valid) {
    out.error = "invalid SVG response";
    return false;
  }
  out.ok = true;
  return true;
}

static uint8_t *raster_bitmap(PsramText &svg, std::string &error) {
  svg.push_back('\0');
  auto *parsed = nsvgParse(svg.data(), "px", 96);
  if (!parsed) { error = "SVG parser PSRAM allocation failed"; return nullptr; }
  uint8_t *pixels = nullptr;
  if (parsed->shapes && std::isfinite(parsed->width) && std::isfinite(parsed->height) &&
      parsed->width > 0 && parsed->height > 0 && parsed->width <= 4096 && parsed->height <= 4096) {
    pixels = static_cast<uint8_t *>(heap_caps_calloc(icon_px() * icon_px(), 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *rasterizer = nsvgCreateRasterizer();
    if (pixels && rasterizer) {
      const float scale = std::min(icon_px() / parsed->width, icon_px() / parsed->height);
      nsvgRasterize(rasterizer, parsed, (icon_px() - parsed->width * scale) / 2,
                    (icon_px() - parsed->height * scale) / 2, scale, pixels, icon_px(), icon_px(), icon_px() * 4);
      // Monochrome icons: BGRA and RGBA have identical white channels for LVGL.
      bool visible = false;
      for (size_t i = 0; i < icon_px() * icon_px(); ++i) {
        visible |= pixels[4 * i + 3] != 0;
        pixels[4 * i] = pixels[4 * i + 1] = pixels[4 * i + 2] = 255;
      }
      if (!visible) { heap_caps_free(pixels); pixels = nullptr; }
    } else {
      heap_caps_free(pixels); pixels = nullptr;
    }
    nsvgDeleteRasterizer(rasterizer);
  }
  nsvgDelete(parsed);
  if (pixels == nullptr) error = "SVG dimensions invalid or raster allocation failed";
  return pixels;
}

// Full-color raster for emoji, BGRA for LVGL ARGB8888.
static uint8_t *raster_emoji(PsramText &svg, std::string &error) {
  svg.push_back('\0');
  auto *parsed = nsvgParse(svg.data(), "px", 96);
  if (!parsed) { error = "SVG parser PSRAM allocation failed"; return nullptr; }
  constexpr size_t N = ::onx_emoji_queue::SIZE;
  uint8_t *pixels = nullptr;
  if (parsed->shapes && std::isfinite(parsed->width) && std::isfinite(parsed->height) &&
      parsed->width > 0 && parsed->height > 0 && parsed->width <= 4096 && parsed->height <= 4096) {
    pixels = static_cast<uint8_t *>(heap_caps_calloc(N * N, 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *rasterizer = nsvgCreateRasterizer();
    if (pixels && rasterizer) {
      const float scale = std::min(N / parsed->width, N / parsed->height);
      nsvgRasterize(rasterizer, parsed, (N - parsed->width * scale) / 2, (N - parsed->height * scale) / 2,
                    scale, pixels, N, N, N * 4);
      bool visible = false;
      for (size_t i = 0; i < N * N; ++i) {
        visible |= pixels[4 * i + 3] != 0;
        std::swap(pixels[4 * i], pixels[4 * i + 2]);  // RGBA -> BGRA.
      }
      if (!visible) { heap_caps_free(pixels); pixels = nullptr; }
    } else {
      heap_caps_free(pixels); pixels = nullptr;
    }
    nsvgDeleteRasterizer(rasterizer);
  }
  nsvgDelete(parsed);
  if (pixels == nullptr) error = "SVG dimensions invalid or raster allocation failed";
  return pixels;
}

// Fixed HTTPS hosts; the stem is built on-device from the emoji table, never from user text.
static uint8_t *fetch_emoji(KeepAliveClient &ka, const std::string &stem, const std::function<bool()> &cancelled,
                            IconDownloader::FetchStats &stats, std::string &error, bool &was_cancelled) {
  for (char c : stem)
    if (!(std::isxdigit(static_cast<unsigned char>(c)) || c == '_')) { error = "invalid emoji name"; return nullptr; }
  // Google's Noto emoji CDN covers every sequence, including flags and keycaps.
  // jsDelivr only mirrors the flag folder (the repository exceeds its size limit).
  const bool flag = stem.size() == 11 && stem.rfind("1f1", 0) == 0 && stem.compare(5, 4, "_1f1") == 0;
  std::vector<std::string> urls{"https://fonts.gstatic.com/s/e/notoemoji/latest/" + stem + "/emoji.svg"};
  if (flag) urls.push_back("https://cdn.jsdelivr.net/gh/googlefonts/noto-emoji@main/third_party/region-flags/waved-svg/emoji_u" + stem + ".svg");
  const uint32_t total_started = millis();
  for (size_t i = 0; i < urls.size(); ++i) {
    if (cancelled()) { was_cancelled = true; error = "cancelled"; return nullptr; }
    const uint32_t elapsed = millis() - total_started;
    if (elapsed >= TOTAL_BUDGET_MS) { error = "emoji download budget exhausted"; break; }
    const uint32_t budget = std::min<uint32_t>(SOURCE_BUDGET_MS, TOTAL_BUDGET_MS - elapsed);
    PsramText svg;
    FetchOutcome out;
    if (fetch_svg(ka, urls[i], svg, MAX_EMOJI_SVG_BYTES, budget, cancelled, out)) {
      const uint32_t r0 = millis();
      auto *pixels = raster_emoji(svg, error);
      stats.attempts += out.attempts;
      stats.open_ms = out.open_ms;
      stats.read_ms = out.read_ms;
      stats.raster_ms = millis() - r0;
      stats.bytes = out.bytes;
      stats.heap_free_after_open = out.heap_free_after_open;
      stats.reused = out.reused;
      stats.backup = (i > 0);
      stats.host = out.host;
      if (pixels) return pixels;
    } else {
      stats.attempts += out.attempts;
      stats.open_ms = out.open_ms;
      stats.read_ms = out.read_ms;
      stats.bytes = out.bytes;
      stats.heap_free_after_open = out.heap_free_after_open;
      stats.reused = out.reused;
      stats.backup = (i > 0);
      stats.host = out.host;
      error = out.error;
      if (out.cancelled) { was_cancelled = true; return nullptr; }
    }
    ESP_LOGW(TAG, "emoji %s source failed (%s)", stem.c_str(), error.c_str());
  }
  return nullptr;
}

static uint8_t *fetch_bitmap(KeepAliveClient &ka, const std::string &icon, bool prefer_backup,
                             const std::function<bool()> &cancelled, IconDownloader::FetchStats &stats,
                             std::string &error, bool &was_cancelled, bool &all_404,
                             bool &primary_host_err, bool &primary_ok) {
  const std::string primary = ::onx_icon_state::download_url(icon);
  const std::string backup = ::onx_icon_state::backup_url(icon);
  struct Source { const std::string &url; bool is_backup; };
  const Source first = prefer_backup ? Source{backup, true} : Source{primary, false};
  const Source second = prefer_backup ? Source{primary, false} : Source{backup, true};
  const Source order[2] = {first, second};

  const uint32_t total_started = millis();
  unsigned not_found_count = 0;
  for (const auto &src : order) {
    if (cancelled()) { was_cancelled = true; error = "cancelled"; return nullptr; }
    const uint32_t elapsed = millis() - total_started;
    if (elapsed >= TOTAL_BUDGET_MS) { error = "icon download budget exhausted"; break; }
    const uint32_t budget = std::min<uint32_t>(SOURCE_BUDGET_MS, TOTAL_BUDGET_MS - elapsed);
    if (budget < 1500) { error = "icon download budget exhausted"; break; }

    PsramText svg;
    FetchOutcome out;
    if (fetch_svg(ka, src.url, svg, MAX_SVG_BYTES, budget, cancelled, out)) {
      const uint32_t r0 = millis();
      auto *pixels = raster_bitmap(svg, error);
      stats.attempts += out.attempts;
      stats.open_ms = out.open_ms;
      stats.read_ms = out.read_ms;
      stats.raster_ms = millis() - r0;
      stats.bytes = out.bytes;
      stats.heap_free_after_open = out.heap_free_after_open;
      stats.reused = out.reused;
      stats.backup = src.is_backup;
      stats.host = out.host;
      if (pixels) {
        if (!src.is_backup) primary_ok = true;
        return pixels;
      }
    } else {
      stats.attempts += out.attempts;
      stats.open_ms = out.open_ms;
      stats.read_ms = out.read_ms;
      stats.bytes = out.bytes;
      stats.heap_free_after_open = out.heap_free_after_open;
      stats.reused = out.reused;
      stats.backup = src.is_backup;
      stats.host = out.host;
      error = out.error;
      if (out.cancelled) { was_cancelled = true; return nullptr; }
      if (out.not_found) ++not_found_count;
      if (!src.is_backup && out.host_error) primary_host_err = true;
    }
    ESP_LOGW(TAG, "%s icon source failed (%s)", icon.c_str(), error.c_str());
  }
  all_404 = (not_found_count == 2);
  return nullptr;
}

IconDownloader::Bitmap::Bitmap(uint8_t *p)
    : pixels(p), image(p, icon_px(), icon_px(), esphome::image::IMAGE_TYPE_RGB, esphome::image::TRANSPARENCY_ALPHA_CHANNEL) {}
IconDownloader::Bitmap::~Bitmap() {
  // Bitmap destruction only occurs in loop()/bind(), on the UI task.
  lv_image_cache_drop(image.get_lv_image_dsc());
  lv_image_header_cache_drop(image.get_lv_image_dsc());
  heap_caps_free(pixels);
}

bool IconDownloader::cancelled_() {
  LockGuard lock(mutex_);
  return cancel_requested_ || stop_worker_;
}

// Burst-scoped worker: reuses one keep-alive TLS client across back-to-back icons,
// then closes the socket and suspends itself as soon as the burst ends or artwork needs the network.
void IconDownloader::download_task_(void *arg) {
  auto *owner = static_cast<IconDownloader *>(arg);
  KeepAliveClient ka{owner->ka_handle_, owner->ka_host_};
  auto is_cancelled = [owner]() { return owner->cancelled_(); };
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    std::string icon;
    bool stop = false;
    bool prefer_backup = false;
    uint32_t heap_free_start = 0, heap_largest_start = 0;
    bool stack_psram = false;
    {
      LockGuard lock(owner->mutex_);
      stop = owner->stop_worker_;
      icon = std::move(owner->requested_icon_);
      owner->requested_icon_.clear();
      prefer_backup = owner->requested_prefer_backup_;
      heap_free_start = owner->requested_heap_free_;
      heap_largest_start = owner->requested_heap_largest_;
      stack_psram = owner->worker_stack_psram_;
    }
    if (stop) break;
    if (icon.empty()) continue;

    FetchStats st{};
    st.icon = icon;
    st.heap_free_start = heap_free_start;
    st.heap_largest_start = heap_largest_start;
    st.stack_psram = stack_psram;
    std::string error;
    bool was_cancelled = false, all_404 = false, primary_host_err = false, primary_ok = false;
    uint8_t *pixels = icon.rfind("emoji:", 0) == 0
                          ? fetch_emoji(ka, icon.substr(6), is_cancelled, st, error, was_cancelled)
                          : fetch_bitmap(ka, icon, prefer_backup, is_cancelled, st, error,
                                         was_cancelled, all_404, primary_host_err, primary_ok);
    if (was_cancelled) {
      heap_caps_free(pixels);
      pixels = nullptr;
      ka.reset();
    }
    {
      LockGuard lock(owner->mutex_);
      owner->completed_icon_ = std::move(icon);
      owner->completed_error_ = std::move(error);
      owner->completed_pixels_ = pixels;
      owner->completed_cancelled_ = was_cancelled;
      owner->completed_all_404_ = all_404;
      owner->completed_primary_host_err_ = primary_host_err;
      owner->completed_primary_ok_ = primary_ok;
      owner->completed_stats_ = std::move(st);
      owner->completed_ = true;
    }
  }
  // Destroy the keep-alive TLS client before suspending so no sockets/TLS buffers linger.
  ka.reset();
  // A WithCaps task cannot free its own stack; loop() deletes it once suspended.
  vTaskSuspend(nullptr);
}

void IconDownloader::reap_worker_() {
  if (!worker_) return;
  auto handle = static_cast<TaskHandle_t>(worker_);
  if (eTaskGetState(handle) != eSuspended) return;
  vTaskDeleteWithCaps(handle);
  worker_ = nullptr;
  worker_stack_psram_ = false;
}

bool IconDownloader::memory_ready() {
#ifdef USE_ESP32
  // The worker stack falls back to PSRAM when internal RAM is fragmented;
  // only require headroom for lwIP/Wi-Fi. SVG text, NanoSVG and TLS live in PSRAM.
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 32 * 1024 &&
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 8 * 1024;
#else
  return true;
#endif
}

size_t IconDownloader::cache_bytes() const {
  return cache_.size() * icon_px() * icon_px() * 4;
}

bool IconDownloader::primary_in_cooldown() const {
  return primary_cooldown_until_ms_ != 0 &&
         static_cast<int32_t>(millis() - primary_cooldown_until_ms_) < 0;
}

bool IconDownloader::artwork_hold_active_(uint32_t now) const {
  return artwork_busy_ ||
         (artwork_yield_until_ms_ != 0 && static_cast<int32_t>(now - artwork_yield_until_ms_) < 0);
}

bool IconDownloader::network_and_memory_ready_() const {
  if (wifi::global_wifi_component == nullptr || !wifi::global_wifi_component->is_connected()) return false;
  // Starting before wall-clock synchronization can make certificate validation fail.
  // Wait instead of poisoning the retry caches during normal boot.
  return std::time(nullptr) >= 1577836800 && memory_ready();
}

bool IconDownloader::can_start_() const {
  return !busy_ && worker_ == nullptr && !artwork_hold_active_(millis()) && network_and_memory_ready_();
}

bool IconDownloader::is_queued_(const std::string &icon) const {
  return std::find(urgent_queue_.begin(), urgent_queue_.end(), icon) != urgent_queue_.end() ||
         std::find(prefetch_queue_.begin(), prefetch_queue_.end(), icon) != prefetch_queue_.end();
}

std::string IconDownloader::status(const std::string &icon) const {
  if (!::onx_icon_state::valid_icon(icon)) return "missing_icon_metadata";
  if (cache_.find(icon) != cache_.end()) return "ready";
  if (busy_ && active_icon_ == icon) return "downloading";
  if (failures_.find(icon) != failures_.end()) return "retry_wait";
  if (wifi::global_wifi_component == nullptr || !wifi::global_wifi_component->is_connected()) return "waiting_for_wifi";
  if (std::time(nullptr) < 1577836800) return "waiting_for_clock";
  if (artwork_hold_active_(millis())) return "waiting_for_artwork";
  if (!memory_ready()) return "waiting_for_memory";
  if (is_queued_(icon)) return "queued";
  if (busy()) return "download_busy";
  return "awaiting_display";
}

void IconDownloader::set_artwork_busy(bool value) {
  artwork_busy_ = value;
  artwork_yield_until_ms_ = 0;
  if (value) {
    if (busy_ && active_is_prefetch_) {
      LockGuard lock(mutex_);
      cancel_requested_ = true;
    }
  } else {
    drain_();
  }
}

void IconDownloader::request_artwork_yield(uint32_t window_ms) {
  uint32_t until = millis() + window_ms;
  if (until == 0) until = 1;
  artwork_yield_until_ms_ = until;
  if (busy_ && active_is_prefetch_) {
    LockGuard lock(mutex_);
    cancel_requested_ = true;
  }
}

void IconDownloader::retry() {
  failures_.clear();
  errors_.clear();
  primary_fail_streak_ = 0;
  primary_cooldown_until_ms_ = 0;
  drain_();
}

void IconDownloader::on_network_connected() {
  primary_fail_streak_ = 0;
  primary_cooldown_until_ms_ = 0;
  for (auto it = failures_.begin(); it != failures_.end();) {
    if (!it->second.permanent) {
      errors_.erase(it->first);
      it = failures_.erase(it);
    } else {
      ++it;
    }
  }
  drain_();
}

void IconDownloader::prefetch(const std::string &icon) {
  if (cache_.find(icon) == cache_.end()) enqueue_prefetch_(icon);
}

void IconDownloader::enqueue_urgent_(const std::string &icon) {
  if (!::onx_icon_state::valid_icon(icon)) return;
  if (cache_.find(icon) != cache_.end() || (busy_ && active_icon_ == icon)) return;
  const auto failed = failures_.find(icon);
  if (failed != failures_.end() && !failed->second.due(millis())) return;
  // Promote out of background prefetch queue if present.
  auto pref_it = std::find(prefetch_queue_.begin(), prefetch_queue_.end(), icon);
  if (pref_it != prefetch_queue_.end()) prefetch_queue_.erase(pref_it);
  if (std::find(urgent_queue_.begin(), urgent_queue_.end(), icon) == urgent_queue_.end()) {
    if (urgent_queue_.size() >= URGENT_QUEUE_LIMIT) urgent_queue_.pop_front();
    urgent_queue_.push_back(icon);
  }
  drain_();
}

void IconDownloader::enqueue_prefetch_(const std::string &icon) {
  if (!::onx_icon_state::valid_icon(icon)) return;
  if (cache_.find(icon) != cache_.end() || (busy_ && active_icon_ == icon)) return;
  const auto failed = failures_.find(icon);
  if (failed != failures_.end() && !failed->second.due(millis())) return;
  if (is_queued_(icon) || prefetch_queue_.size() >= PREFETCH_QUEUE_LIMIT) return;
  prefetch_queue_.push_back(icon);
  drain_();
}

bool IconDownloader::pick_next_(std::string &key, bool &is_prefetch) {
  const uint32_t now = millis();
  while (!urgent_queue_.empty()) {
    std::string next = std::move(urgent_queue_.front());
    urgent_queue_.pop_front();
    if (cache_.find(next) != cache_.end()) continue;
    const auto failed = failures_.find(next);
    if (failed != failures_.end() && !failed->second.due(now)) continue;
    key = std::move(next);
    is_prefetch = false;
    return true;
  }
  // Visible emoji sit behind visible icons but ahead of off-screen background prefetches.
  auto &pending = ::onx_emoji_queue::pending;
  if (!pending.empty()) {
    auto next = pending.front();
    pending.pop_front();
    emoji_index_ = next.index;
    key = "emoji:" + next.stem;
    is_prefetch = false;
    return true;
  }
  while (!prefetch_queue_.empty()) {
    std::string next = std::move(prefetch_queue_.front());
    prefetch_queue_.pop_front();
    if (cache_.find(next) != cache_.end()) continue;
    const auto failed = failures_.find(next);
    if (failed != failures_.end() && !failed->second.due(now)) continue;
    key = std::move(next);
    is_prefetch = true;
    return true;
  }
  return false;
}

bool IconDownloader::start_(const std::string &key, bool is_prefetch) {
  uint32_t heap_free = 0, heap_largest = 0;
  bool want_internal = false;
#ifdef USE_ESP32
  heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  // Prefer an internal stack (TLS/NanoSVG are faster on internal RAM) only when
  // plenty of internal headroom stays available for Wi-Fi, API and SPI DMA.
  want_internal = heap_free >= 56 * 1024 && heap_largest >= WORKER_STACK_BYTES + 8192;
#endif
  {
    LockGuard lock(mutex_);
    requested_icon_ = key;
    requested_prefer_backup_ = primary_in_cooldown();
    requested_heap_free_ = heap_free;
    requested_heap_largest_ = heap_largest;
    stop_worker_ = false;
    cancel_requested_ = false;
    completed_ = false;
  }
  if (worker_ == nullptr) {
    TaskHandle_t handle = nullptr;
    BaseType_t created = pdFAIL;
    worker_stack_psram_ = false;
#ifdef USE_ESP32
    if (want_internal) {
      created = xTaskCreatePinnedToCoreWithCaps(download_task_, "onx_icon", WORKER_STACK_BYTES, this, 1, &handle,
                                                tskNO_AFFINITY, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
#endif
    if (created != pdPASS) {
      worker_stack_psram_ = true;
      created = xTaskCreatePinnedToCoreWithCaps(download_task_, "onx_icon", WORKER_STACK_BYTES, this, 1, &handle,
                                                tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (created != pdPASS) {
      worker_stack_psram_ = false;
      LockGuard lock(mutex_);
      requested_icon_.clear();
      return false;
    }
    worker_ = handle;
  }
  active_icon_ = key;
  active_is_prefetch_ = is_prefetch;
  started_ms_ = millis();
  busy_ = true;
  ++stats_.attempts;
  ESP_LOGI(TAG, "Downloading %s", key.c_str());
  xTaskNotifyGive(static_cast<TaskHandle_t>(worker_));
  return true;
}

void IconDownloader::drain_() {
  reap_worker_();
  if (!can_start_()) return;
  std::string key;
  bool is_prefetch = false;
  if (!pick_next_(key, is_prefetch)) return;
  if (!start_(key, is_prefetch)) {
    if (key.rfind("emoji:", 0) != 0) record_failure_(key, "worker allocation failed", false, millis());
  }
}

void IconDownloader::record_failure_(const std::string &icon, const std::string &error, bool permanent, uint32_t now) {
  if (failures_.find(icon) == failures_.end() && failures_.size() >= FAILURE_LIMIT) {
    auto oldest = std::min_element(failures_.begin(), failures_.end(), [](const auto &a, const auto &b) {
      return static_cast<int32_t>(a.second.failed_at - b.second.failed_at) < 0;
    });
    if (oldest != failures_.end()) {
      errors_.erase(oldest->first);
      failures_.erase(oldest);
    }
  }
  errors_[icon] = error;
  if (permanent) failures_[icon].fail_permanent(now);
  else failures_[icon].fail(now);
}

void IconDownloader::rebind_ready_(const std::string &icon, const std::shared_ptr<Bitmap> &bitmap) {
  if (!bitmap) return;
  const auto *source = bitmap->image.get_lv_image_dsc();
  for (auto &kv : bindings_) {
    auto &binding = kv.second;
    if (binding.icon != icon) continue;
    if (binding.source != source || lv_img_get_src(kv.first) != source) {
      lv_img_set_src(kv.first, source);
      binding.source = source;
      binding.bitmap = bitmap;
    }
  }
}

void IconDownloader::loop() {
  reap_worker_();
  bool done = false;
  {
    LockGuard lock(mutex_);
    done = completed_;
  }
  if (!done) {
    if (!busy_ && worker_ == nullptr) {
      if (::onx_emoji_queue::tick) ::onx_emoji_queue::tick();
      drain_();
    }
    return;
  }

  std::string icon, error;
  uint8_t *pixels = nullptr;
  bool was_cancelled = false, all_404 = false, primary_host_err = false, primary_ok = false;
  FetchStats fetch_stats;
  {
    LockGuard lock(mutex_);
    icon = std::move(completed_icon_);
    pixels = completed_pixels_;
    error = std::move(completed_error_);
    was_cancelled = completed_cancelled_;
    all_404 = completed_all_404_;
    primary_host_err = completed_primary_host_err_;
    primary_ok = completed_primary_ok_;
    fetch_stats = std::move(completed_stats_);
    completed_pixels_ = nullptr;
    completed_ = false;
  }
  const bool was_prefetch = active_is_prefetch_;
  busy_ = false;
  active_icon_.clear();
  active_is_prefetch_ = false;

  const uint32_t now = millis();
  const uint32_t took = now - started_ms_;
  stats_.last_ms = took;
  if (took > stats_.slowest_ms) stats_.slowest_ms = took;
  stats_.last_fetch = fetch_stats;
  if (fetch_stats.reused) ++stats_.keepalive_reuses;

  if (was_cancelled) {
    ++stats_.cancelled;
    heap_caps_free(pixels);
    // Preempted background prefetch is not an error: re-queue it cleanly.
    if (was_prefetch && icon.rfind("emoji:", 0) != 0 && cache_.find(icon) == cache_.end() &&
        !is_queued_(icon) && prefetch_queue_.size() < PREFETCH_QUEUE_LIMIT) {
      prefetch_queue_.push_front(icon);
    }
  } else if (icon.rfind("emoji:", 0) == 0) {
    if (pixels) ++stats_.ok;
    else {
      ++stats_.failed;
      stats_.last_error = error;
      ESP_LOGW(TAG, "%s unavailable (%s)", icon.c_str(), error.c_str());
    }
    if (::onx_emoji_queue::completed) ::onx_emoji_queue::completed(emoji_index_, pixels);
    else heap_caps_free(pixels);
  } else if (pixels) {
    ++stats_.ok;
    if (fetch_stats.backup) ++stats_.backup_ok;
    if (primary_ok) {
      primary_fail_streak_ = 0;
      primary_cooldown_until_ms_ = 0;
    } else if (primary_host_err) {
      // Primary failed with a transport/refusal error while backup succeeded:
      // prefer backup directly for 60 s instead of timing out primary on every icon.
      ++primary_fail_streak_;
      uint32_t until = now + PRIMARY_COOLDOWN_MS;
      primary_cooldown_until_ms_ = until == 0 ? 1 : until;
    }

    auto need_evict = [&]() -> bool {
      if (cache_.empty()) return false;
      if (cache_.size() >= CACHE_SIZE) return true;
#ifdef USE_ESP32
      if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < CACHE_MIN_FREE_PSRAM) return true;
#endif
      return false;
    };
    while (need_evict()) {
      auto oldest = std::min_element(cache_.begin(), cache_.end(), [](const auto &a, const auto &b) {
        return a.second->used < b.second->used;
      });
      cache_.erase(oldest);  // Existing widgets retain their shared ownership.
    }
    auto bitmap = std::make_shared<Bitmap>(pixels);
    bitmap->used = ++cache_clock_;
    cache_[icon] = bitmap;
    failures_.erase(icon);
    errors_.erase(icon);
    ++cache_revision_;
    rebind_ready_(icon, bitmap);
    ESP_LOGI(TAG, "Downloaded and rendered %s (%ums%s%s)", icon.c_str(), static_cast<unsigned>(took),
             fetch_stats.reused ? ", keep-alive" : "", fetch_stats.backup ? ", backup" : "");
  } else {
    ++stats_.failed;
    stats_.last_error = error;
    if (primary_host_err) {
      if (++primary_fail_streak_ >= 2) {
        uint32_t until = now + PRIMARY_COOLDOWN_MS;
        primary_cooldown_until_ms_ = until == 0 ? 1 : until;
      }
    }
    record_failure_(icon, error, all_404, now);
    ESP_LOGW(TAG, "%s unavailable (%s); retaining fallback, retry in %ums",
             icon.c_str(), error.c_str(), static_cast<unsigned>(failures_[icon].delay));
  }

  // Continue draining on the live worker (reusing its keep-alive TLS socket) if more work
  // is queued and artwork isn't waiting; otherwise stop the worker so TLS/stack RAM is freed.
  if (worker_ != nullptr) {
    if (::onx_emoji_queue::tick) ::onx_emoji_queue::tick();
    std::string next_key;
    bool next_is_prefetch = false;
    if (!artwork_hold_active_(now) && network_and_memory_ready_() && pick_next_(next_key, next_is_prefetch)) {
      if (!start_(next_key, next_is_prefetch)) {
        if (next_key.rfind("emoji:", 0) != 0) record_failure_(next_key, "worker dispatch failed", false, now);
      }
    } else {
      {
        LockGuard lock(mutex_);
        stop_worker_ = true;
        requested_icon_.clear();
      }
      xTaskNotifyGive(static_cast<TaskHandle_t>(worker_));
    }
  }
}

void IconDownloader::bind(lv_obj_t *widget, const std::string &icon, image::Image *fallback, bool active) {
  if (!widget || !fallback) return;
  auto found = cache_.find(icon);
  auto bitmap = found == cache_.end() ? std::shared_ptr<Bitmap>{} : found->second;
  if (bitmap) bitmap->used = ++cache_clock_;
  // Assign the descriptor before releasing the previous widget's backing bitmap.
  auto &binding = bindings_[widget];
  binding.icon = icon;
  const auto *source = bitmap ? bitmap->image.get_lv_image_dsc() : fallback->get_lv_image_dsc();
  if (binding.source != source || lv_img_get_src(widget) != source) {
    if (bitmap) ++stats_.cache_hits;
    lv_img_set_src(widget, source);
    binding.source = source;
    binding.bitmap = bitmap;
  }
  if (!binding.styled || binding.active != active) {
    lv_obj_set_style_image_recolor(widget, lv_color_hex(active ? 0x62B0FF : 0xFFFFFF), 0);
    lv_obj_set_style_image_recolor_opa(widget, LV_OPA_COVER, 0);
    binding.active = active; binding.styled = true;
  }
  if (!bitmap) enqueue_urgent_(icon);
}
}  // namespace esphome::onx_icons
