#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
inline const char *esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "ESP_OK" : "connection error"; }

struct esp_http_client_config_t {
  const char *url = nullptr;
  int (*crt_bundle_attach)(void *) = nullptr;
  int timeout_ms = 0;
  bool disable_auto_redirect = false;
  int buffer_size = 0;
  bool keep_alive_enable = false;
  const char *user_agent = nullptr;
};

struct FakeHttp {
  std::string url;
  std::map<std::string, std::string> headers;
  size_t offset = 0;
  bool stalled = false;
  bool has_opened = false;
  int status = 200;
  int timeout_ms = 0;
  int last_errno = 0;
};
using esp_http_client_handle_t = FakeHttp *;

inline std::vector<std::string> requested_icon_urls;
inline int http_init_count = 0;
inline int http_cleanup_count = 0;
inline int http_open_count = 0;
inline bool fail_primary = false;
inline bool fail_all = false;
inline bool fail_404_all = false;
inline bool stall_once = false;
inline bool truncate_eof = false;
inline bool stale_keepalive_once = false;
inline bool fail_open_once = false;
inline std::function<void(const std::string &)> on_http_open;
inline const std::string icon_svg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"80\" height=\"80\" viewBox=\"0 0 24 24\"><path fill=\"#fff\" d=\"M2 2H22V22H2Z\"/></svg>";

inline int fake_status_for(const std::string &url) {
  if (fail_404_all) return 404;
  if (fail_all || (fail_primary && url.find("iconify") != std::string::npos)) return 403;
  return 200;
}

inline FakeHttp *esp_http_client_init(const esp_http_client_config_t *config) {
  if (!config->crt_bundle_attach || !config->disable_auto_redirect ||
      std::string(config->user_agent) != "RoomCompanion/MDI-icons")
    return nullptr;
  ++http_init_count;
  auto *http = new FakeHttp;
  http->url = config->url;
  http->timeout_ms = config->timeout_ms;
  http->status = fake_status_for(http->url);
  return http;
}
inline esp_err_t esp_http_client_set_url(FakeHttp *http, const char *url) {
  if (!http || !url) return ESP_FAIL;
  http->url = url;
  http->offset = 0;
  http->stalled = false;
  http->last_errno = 0;
  http->status = fake_status_for(http->url);
  return ESP_OK;
}
inline esp_err_t esp_http_client_set_timeout_ms(FakeHttp *http, int timeout_ms) {
  if (!http) return ESP_FAIL;
  http->timeout_ms = timeout_ms;
  return ESP_OK;
}
inline int esp_http_client_set_header(FakeHttp *http, const char *key, const char *value) {
  http->headers[key] = value;
  return 0;
}
inline esp_err_t esp_http_client_open(FakeHttp *http, int) {
  ++http_open_count;
  requested_icon_urls.emplace_back(http->url);
  http->offset = 0;
  http->stalled = false;
  http->last_errno = 0;
  if (on_http_open) on_http_open(http->url);
  if (fail_open_once) {
    fail_open_once = false;
    return ESP_FAIL;
  }
  http->has_opened = true;
  return ESP_OK;
}
inline int64_t esp_http_client_fetch_headers(FakeHttp *) {
  if (stale_keepalive_once) {
    stale_keepalive_once = false;
    return -1;
  }
  return 0;  // Chunked body with unknown length (ESP-IDF returns 0 for chunked, -1 on error).
}
inline int esp_http_client_get_status_code(FakeHttp *http) { return http->status; }
inline int esp_http_client_get_errno(FakeHttp *http) { return http ? http->last_errno : 0; }
inline int esp_http_client_read(FakeHttp *http, char *buffer, size_t size) {
  if (truncate_eof) {
    if (http->offset == 0) {
      const auto partial = std::min<size_t>(size, 10);
      std::memcpy(buffer, icon_svg.data(), partial);
      http->offset = partial;
      return static_cast<int>(partial);
    }
    http->last_errno = 0;
    return 0;
  }
  if (stall_once && !http->stalled) {
    http->stalled = true;
    return 0;
  }
  const auto count = std::min(size, icon_svg.size() - http->offset);
  std::memcpy(buffer, icon_svg.data() + http->offset, count);
  http->offset += count;
  return static_cast<int>(count);
}
inline bool esp_http_client_is_complete_data_received(FakeHttp *http) {
  return !truncate_eof && http->offset == icon_svg.size();
}
inline void esp_http_client_close(FakeHttp *) {}
inline void esp_http_client_cleanup(FakeHttp *http) {
  if (http) ++http_cleanup_count;
  delete http;
}
