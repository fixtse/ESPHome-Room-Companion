#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <cctype>
namespace onx_cover_presets {
constexpr size_t MAX = 4;
struct Config { uint8_t count{0}; std::array<uint8_t,MAX> positions{}; bool enabled{true}; };
inline Config defaults() { return {4, {15,25,50,75}, true}; }
inline bool parse(const std::string &raw, Config &out) {
  out = {};
  size_t at = 0;
  auto spaces = [&]() { while (at < raw.size() && std::isspace(static_cast<unsigned char>(raw[at]))) ++at; };
  spaces(); if (at == raw.size()) return true;
  while (at < raw.size()) {
    spaces();
    if (out.count == MAX || at == raw.size() || raw[at] < '0' || raw[at] > '9') return false;
    unsigned value = 0, digits = 0;
    while (at < raw.size() && raw[at] >= '0' && raw[at] <= '9') {
      value = value * 10 + unsigned(raw[at++] - '0'); if (++digits > 3 || value > 100) return false;
    }
    for (size_t i=0;i<out.count;++i) if (out.positions[i] == value) return false;
    out.positions[out.count++] = value;
    spaces(); if (at == raw.size()) return true;
    if (raw[at++] != ',' || at == raw.size()) return false;
  }
  return true;
}
inline std::string encode(const Config &config) {
  std::string result;
  for (size_t i=0;i<config.count;++i) { if (i) result += ','; result += std::to_string(config.positions[i]); }
  return result;
}
}  // namespace onx_cover_presets
