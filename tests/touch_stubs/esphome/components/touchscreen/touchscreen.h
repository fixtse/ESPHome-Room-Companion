#pragma once
#include <vector>
namespace esphome::touchscreen {
constexpr int STATE_PRESSED = 1, STATE_UPDATED = 2, STATE_RELEASING = 4;
struct TouchPoint { int x, y, state; };
using TouchPoints_t = std::vector<TouchPoint>;
struct TouchListener { virtual void update(const TouchPoints_t &) {} virtual void release() {} };
struct Touchscreen { void register_listener(TouchListener *) {} };
}
