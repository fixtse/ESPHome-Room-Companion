#pragma once
namespace esphome {
namespace setup_priority { constexpr float DATA = 600; }
struct Component { virtual void setup() {} virtual void loop() {} virtual float get_setup_priority() const { return 0; } };
}
