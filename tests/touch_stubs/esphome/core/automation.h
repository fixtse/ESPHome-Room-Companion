#pragma once
#include <functional>
namespace esphome {
template<typename... Args> struct Trigger {
  std::function<void(Args...)> callback;
  void trigger(Args... args) { if (callback) callback(args...); }
};
}
