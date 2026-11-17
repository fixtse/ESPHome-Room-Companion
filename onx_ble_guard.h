#pragma once
// Bluetooth (Improv provisioning) needs several tens of KB of internal RAM to
// start. Bluedroid does not fail gracefully: when it cannot allocate, its start-up
// error path dereferences lists that were never created and aborts the boot
// (assert list_begin list.c:266), which repeats on every boot while the memory
// stays short. This guard keeps such a failure to a single reboot.
//
// begin() is called right before a start attempt and done() once Bluetooth is
// active. The flag lives in RTC memory, which survives a software reset but not
// a power-on, so after an aborted attempt Bluetooth stays off until power is
// cycled and the panel keeps booting.
#include <cstdint>
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

namespace onx_ble_guard {
constexpr uint32_t PENDING = 0x424C4547;
static RTC_NOINIT_ATTR uint32_t attempt;  // One translation unit includes this header.
inline bool blocked() {
  static bool checked = false, previous_attempt_failed = false;
  if (!checked) {
    checked = true;
    if (esp_reset_reason() == ESP_RST_POWERON) attempt = 0;
    previous_attempt_failed = attempt == PENDING;
  }
  return previous_attempt_failed;
}
inline bool pending() { return attempt == PENDING; }
inline void begin() { attempt = PENDING; }
inline void done() { attempt = 0; }
// Free internal RAM and its largest contiguous block, both required.
inline bool enough_memory(size_t free_bytes, size_t block_bytes) {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= free_bytes &&
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= block_bytes;
}
}  // namespace onx_ble_guard
