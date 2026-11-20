#pragma once

#include <cstdint>
#include "esp_attr.h"

namespace esphome::onx_config_web {

enum class RequestRoute : uint8_t {
  UNKNOWN, ROOT, PANEL, PANEL_SLASH, API, RESULT, APP_JS, PANEL_JS, HA_JS, LOGO, FAVICON, OTHER,
};
enum class RequestMethod : uint8_t { OTHER, GET, POST };

struct RequestDiagnostics {
  uint32_t magic;
  uint32_t at_ms;
  uint32_t entry_stack_bytes;
  uint32_t exit_stack_bytes;
  RequestRoute route;
  RequestMethod method;
  bool completed;
};

constexpr uint32_t REQUEST_DIAGNOSTICS_MAGIC = 0x4F4E4854;
RTC_NOINIT_ATTR inline RequestDiagnostics retained_request;
inline RequestDiagnostics previous_request{};

inline const char *method_name(RequestMethod method) {
  if (method == RequestMethod::GET) return "GET";
  if (method == RequestMethod::POST) return "POST";
  return "(other)";
}

inline const char *route_name(RequestRoute route) {
  switch (route) {
    case RequestRoute::ROOT: return "/";
    case RequestRoute::PANEL: return "/panel";
    case RequestRoute::PANEL_SLASH: return "/panel/";
    case RequestRoute::API: return "/panel/api";
    case RequestRoute::RESULT: return "/panel/result";
    case RequestRoute::APP_JS: return "/panel/app.js";
    case RequestRoute::PANEL_JS: return "/panel/panel.js";
    case RequestRoute::HA_JS: return "/panel/ha.js";
    case RequestRoute::LOGO: return "/panel/fixtse-logo.png";
    case RequestRoute::FAVICON: return "/panel/favicon.svg";
    case RequestRoute::OTHER: return "(other)";
    default: return "(unresolved)";
  }
}

}  // namespace esphome::onx_config_web
