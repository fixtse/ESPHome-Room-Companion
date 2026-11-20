#pragma once
#include <cstdint>
// Host stand-in: request diagnostics record the web task's stack headroom.
inline uint32_t uxTaskGetStackHighWaterMark(void *) { return 4096; }
