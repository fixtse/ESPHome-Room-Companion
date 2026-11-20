#pragma once
#include <cstdint>
#include <array>
#include <cstdio>
#include <string>
#if defined(USE_ESP32) && defined(USE_MDNS)
#include <mdns.h>
#endif

namespace esphome::onx_config_web {
// ESP-IDF performs the multicast search asynchronously. Never wait for network
// replies on the ESPHome/LVGL task; retain at most four advertised instances.
// The same search serves Home Assistant (_home-assistant._tcp) and ImmichFrame
// (_immichframe._tcp); both advertise their address in TXT records.
class HADiscovery {
 public:
  HADiscovery(const char *service = "_home-assistant", const char *fallback_name = "Home Assistant")
      : service_(service), fallback_name_(fallback_name) {}
  void poll() {
#if defined(USE_ESP32) && defined(USE_MDNS)
    if (!search_) return;
    mdns_result_t *results = nullptr;
    if (!mdns_query_async_get_results(search_, 0, &results, nullptr)) return;
    count_ = 0;
    for (auto *r = results; r && count_ < urls_.size(); r = r->next) {
      std::string url, base, name, version, uuid;
      for (size_t i = 0; i < r->txt_count; ++i) {
        const auto &txt = r->txt[i];
        if (!txt.key || !txt.value) continue;
        const std::string key(txt.key), value(txt.value);
        const bool usable = value.size() <= 192 && (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0) &&
                            value.find('@') == std::string::npos;
        if ((key == "internal_url" || key == "url") && usable) url = value;
        else if (key == "base_url" && usable) base = value;
        else if (key == "location_name" || key == "name") name = value.substr(0, 80);
        else if (key == "version") version = value.substr(0, 32);
        else if (key == "uuid") uuid = value.substr(0, 64);
      }
      if (url.empty()) url = base;
      if (url.empty() && r->port) {
        char host[64]{};
        for (auto *a = r->addr; a; a = a->next)
          if (a->addr.type == ESP_IPADDR_TYPE_V4) { snprintf(host, sizeof(host), IPSTR, IP2STR(&a->addr.u_addr.ip4)); break; }
        if (host[0]) url = "http://" + std::string(host) + ':' + std::to_string(r->port);
        else if (r->hostname) url = "http://" + std::string(r->hostname).substr(0, 63) + ".local:" + std::to_string(r->port);
      }
      if (url.empty()) continue;
      bool duplicate = false;
      for (size_t i = 0; i < count_; ++i) duplicate |= urls_[i] == url;
      if (duplicate) continue;
      if (name.empty()) name = r->instance_name ? std::string(r->instance_name).substr(0, 80) : std::string(fallback_name_);
      names_[count_] = std::move(name);
      versions_[count_] = std::move(version);
      uuids_[count_] = std::move(uuid);
      urls_[count_++] = std::move(url);
    }
    mdns_query_results_free(results);
    mdns_query_async_delete(search_);
    search_ = nullptr;
    status_ = "ready";
#endif
  }
  std::string response(bool refresh, uint32_t now) {
    poll();
#if defined(USE_ESP32) && defined(USE_MDNS)
    if (!search_ && (!started_ || (refresh && now - started_ms_ >= 5000))) {
      started_ = true; started_ms_ = now; count_ = 0;
      search_ = mdns_query_async_new(nullptr, service_, "_tcp", MDNS_TYPE_PTR, 2500, 4, nullptr);
      status_ = search_ ? "searching" : "unavailable";
    }
#endif
    std::string out = "{\"status\":" + quote(status_) + ",\"instances\":[";
    for (size_t i = 0; i < count_; ++i) {
      if (i) out += ',';
      out += "{\"name\":" + quote(names_[i]) + ",\"url\":" + quote(urls_[i]) + ",\"version\":" +
             quote(versions_[i]) + ",\"uuid\":" + quote(uuids_[i]) + '}';
    }
    return out + "]}";
  }
 // The one discovered instance's URL once a search has finished; empty while
  // searching, when nothing was found or when several instances answer.
  std::string single_url() const { return status_ == "ready" && count_ == 1 ? urls_[0] : std::string(); }
 private:
  static std::string quote(const std::string &value) {
    std::string out = "\"";
    for (unsigned char c : value) {
      if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
      else if (c >= 32) out += char(c);
    }
    return out + '"';
  }
  const char *service_, *fallback_name_;
  std::array<std::string, 4> names_, urls_, versions_, uuids_;
  size_t count_{0};
  bool started_{false};
  uint32_t started_ms_{0};
  std::string status_{"unavailable"};
#if defined(USE_ESP32) && defined(USE_MDNS)
  mdns_search_once_t *search_{nullptr};
#endif
};
}  // namespace esphome::onx_config_web
