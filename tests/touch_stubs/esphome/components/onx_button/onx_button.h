#pragma once
#include <vector>
namespace esphome::onx_button {
struct BufferedButton { std::vector<bool> edges; void set_virtual_pressed(bool value) { edges.push_back(value); } };
}
