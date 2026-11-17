#pragma once
// UI geometry scale. Layouts are authored for the knob's 240x240 screen; the
// rectangular panels set `scale` at boot (same value as the ui_scale
// substitution used for YAML layout). S() maps a layout length to pixels.
#include <cmath>

namespace onx_geom {
inline float scale = 1.0f;
inline int S(int value) { return scale == 1.0f ? value : static_cast<int>(std::lround(value * scale)); }
inline int S(float value) { return static_cast<int>(std::lround(value * scale)); }
}  // namespace onx_geom
