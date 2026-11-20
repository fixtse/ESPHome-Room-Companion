#pragma once
// Records the crash PC and backtrace in the RTC breadcrumb (onx_runtime::retained)
// so "Download diagnostics" shows where the firmware crashed, even after OTA
// rollback boots the previous image (it reads the same breadcrumb layout).
//
// ESPHome's own crash handler already wraps esp_panic_handler (it logs the
// backtrace on the next boot of the same build only), so this wraps the two
// functions that turn the exception frame into panic info instead:
// -Wl,--wrap=panic_arch_fill_info,--wrap=panic_soc_fill_info. Runs in the panic
// handler: no heap,
// no locks, no flash-resident code or constants, so it is safe even when the
// crash happened with the flash cache disabled.
#include "onx_runtime_diagnostics.h"
#if defined(USE_ESP32) && defined(__XTENSA__)
#include "esp_attr.h"
#include "esp_debug_helpers.h"
#include "esp_memory_utils.h"
#include "esp_private/panic_internal.h"
#include "esp_cpu_utils.h"
#include "xtensa_context.h"

extern "C" char *g_panic_abort_details;

namespace onx_crash {
// "panic" and the hex digits live in DRAM, not flash rodata.
static DRAM_ATTR const char PANIC_NAME[] = "panic";
static DRAM_ATTR const char HEX[] = "0123456789abcdef";

static IRAM_ATTR size_t put_hex(char *out, size_t at, size_t size, uint32_t value) {
  if (at + 11 >= size) return at;
  out[at++] = '0';
  out[at++] = 'x';
  for (int shift = 28; shift >= 0; shift -= 4) out[at++] = HEX[(value >> shift) & 0xF];
  out[at++] = ' ';
  return at;
}

static IRAM_ATTR void capture(const void *raw_frame) {
  auto &details = onx_runtime::retained_panic_details;
  details.magic = 0;
  const char *message = g_panic_abort_details;
  size_t length = 0;
  // FreeRTOS builds the overflow message, including the task name, on the
  // internal ISR stack. Only read internal DRAM while the cache may be off.
  if (message != nullptr) {
    while (length < sizeof(details.message) - 1 && esp_ptr_in_dram(message + length)) {
      const char byte = static_cast<const volatile char *>(message)[length];
      if (byte == '\0') break;
      details.message[length++] = byte;
    }
  }
  details.message[length] = '\0';
  if (length != 0) details.magic = onx_runtime::PANIC_DETAILS_MAGIC;

  auto &event = onx_runtime::retained;
  event.magic = 0;
  for (size_t i = 0; i < sizeof(event.name); ++i) {
    event.name[i] = i < sizeof(PANIC_NAME) ? PANIC_NAME[i] : 0;
  }
  // entity: "<cause> <excvaddr> <pc> <backtrace pcs...>" in hex.
  size_t at = 0;
  const auto *frame = static_cast<const XtExcFrame *>(raw_frame);
  if (frame != nullptr) {
    at = put_hex(event.entity, at, sizeof(event.entity), frame->exccause);
    at = put_hex(event.entity, at, sizeof(event.entity), frame->excvaddr);
    esp_backtrace_frame_t bt{};
    bt.pc = frame->pc;
    bt.sp = frame->a1;
    bt.next_pc = frame->a0;
    bt.exc_frame = frame;
    at = put_hex(event.entity, at, sizeof(event.entity), esp_cpu_process_stack_pc(bt.pc));
    for (int depth = 0; depth < 12 && bt.next_pc != 0; ++depth) {
      if (!esp_backtrace_get_next_frame(&bt)) break;
      at = put_hex(event.entity, at, sizeof(event.entity), esp_cpu_process_stack_pc(bt.pc));
    }
  }
  event.entity[at < sizeof(event.entity) ? at : sizeof(event.entity) - 1] = 0;
  event.magic = 0x4F4E5852;
}
}  // namespace onx_crash

extern "C" void __real_panic_arch_fill_info(void *frame, panic_info_t *info);
extern "C" void __real_panic_soc_fill_info(void *frame, panic_info_t *info);
extern "C" IRAM_ATTR void __wrap_panic_arch_fill_info(void *frame, panic_info_t *info) {
  onx_crash::capture(frame);
  __real_panic_arch_fill_info(frame, info);
}
extern "C" IRAM_ATTR void __wrap_panic_soc_fill_info(void *frame, panic_info_t *info) {
  onx_crash::capture(frame);
  __real_panic_soc_fill_info(frame, info);
}
#endif
