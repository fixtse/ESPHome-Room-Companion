#pragma once
// Room cleaning for vacuums: the rooms a vacuum can clean and how the panel
// asks Home Assistant to clean the rooms picked on its screen, in the order
// they were picked.
//
// AREAS  Home Assistant's own vacuum.clean_area (vacuums that support it:
//        Roborock, Ecovacs, Matter, MQTT/Valetudo, Neato, template vacuums).
//        Room ids are Home Assistant area ids mapped to the vacuum's segments
//        in the vacuum's settings.
// SCRIPT A script of the user's, for any other integration. The panel calls
//        it with `<field>` set to the picked room names joined by `separator`
//        (e.g. cleaning_room: "Kitchen|Hall"), `room_ids` likewise with the
//        rooms' ids when they have one, and `vacuum_entity`.
#include "onx_memory.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace onx_vacuum_rooms {
constexpr size_t MAX_ROOMS = 16;
constexpr uint32_t MAGIC = 0x4F4E5852;  // ONXR
enum Method : uint8_t { OFF = 0, AREAS = 1, SCRIPT = 2 };
struct Room { char id[49]{}, name[49]{}; };
struct Config {
  uint32_t magic{MAGIC};
  char entity[129]{};
  uint8_t method{OFF};
  char script[129]{};
  char field[33]{"rooms"};
  char separator[4]{"|"};
  uint8_t count{0};
  Room rooms[MAX_ROOMS]{};
};
inline onx_memory::PagedArray<Config, onx_memory::ENTRY_COUNT> configs;

inline const char *method_name(uint8_t m) { return m == AREAS ? "areas" : m == SCRIPT ? "script" : "off"; }
inline int method_parse(const std::string &s) { return s == "areas" ? AREAS : s == "script" ? SCRIPT : s == "off" || s.empty() ? OFF : -1; }
// [a-z0-9_], 1-32 characters: a script variable name.
inline bool valid_field(const std::string &s) {
  if (s.empty() || s.size() > 32) return false;
  for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}
inline bool usable(const Config *c) { return c != nullptr && c->method != OFF && c->count > 0; }

// The panel's room list: picked room indices in the order they were tapped.
struct Picker {
  std::string entity;
  std::vector<int> order;
  bool open{false};
  void clear() { entity.clear(); order.clear(); open = false; }
  void show(const std::string &e) { clear(); entity = e; open = true; }
  // Tapping a picked room removes it (later rooms move up one place).
  void toggle(int room) {
    for (size_t i = 0; i < order.size(); ++i)
      if (order[i] == room) { order.erase(order.begin() + i); return; }
    order.push_back(room);
  }
  int position(int room) const {
    for (size_t i = 0; i < order.size(); ++i) if (order[i] == room) return int(i) + 1;
    return 0;
  }
  std::string signature() const {
    std::string s = open ? "rooms:" + entity : "";
    for (int r : order) s += ',' + std::to_string(r);
    return s;
  }
};
inline Picker picker;
}  // namespace onx_vacuum_rooms
