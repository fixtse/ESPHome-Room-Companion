#pragma once
#include "freertos/FreeRTOS.h"
#include "esphome/core/hal.h"
#include <cassert>

// The icon worker is burst-scoped: each finish_icon_task() advances it until it
// waits for the next notification or suspends itself on burst completion.
typedef void *TaskHandle_t;
#define tskNO_AFFINITY 0x7FFFFFFF
#define portMAX_DELAY 0xFFFFFFFFu
#define pdTRUE 1
enum eTaskState { eRunning = 0, eSuspended = 3 };
struct IconTaskIdle {};
struct IconTaskSuspended {};
inline void (*icon_task)(void *) = nullptr;
inline void *icon_task_arg = nullptr;
inline int icon_notifications = 0;
inline bool icon_task_suspended = false;
inline int icon_task_created_count = 0;
inline int icon_task_deleted_count = 0;

inline BaseType_t xTaskCreatePinnedToCoreWithCaps(void (*task)(void *), const char *, int, void *arg, int,
                                                  TaskHandle_t *handle, int, unsigned) {
  icon_task = task;
  icon_task_arg = arg;
  icon_task_suspended = false;
  icon_notifications = 0;
  ++icon_task_created_count;
  if (handle) *handle = reinterpret_cast<TaskHandle_t>(&icon_task);
  return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t) { ++icon_notifications; }
inline unsigned ulTaskNotifyTake(int, unsigned) {
  if (!icon_notifications) throw IconTaskIdle{};
  const int n = icon_notifications;
  icon_notifications = 0;
  return n;
}
inline eTaskState eTaskGetState(TaskHandle_t) { return icon_task_suspended ? eSuspended : eRunning; }
inline void vTaskSuspend(TaskHandle_t) {
  icon_task_suspended = true;
  throw IconTaskSuspended{};
}
inline void vTaskDeleteWithCaps(TaskHandle_t) {
  assert(icon_task_suspended);
  icon_task_suspended = false;
  icon_task = nullptr;
  icon_task_arg = nullptr;
  ++icon_task_deleted_count;
}
inline void vTaskDelete(void *) {}
inline void vTaskDelay(unsigned ticks) { icon_clock += ticks ? ticks : 1; }
inline void finish_icon_task() {
  if (!icon_task || icon_task_suspended) return;
  try {
    icon_task(icon_task_arg);
  } catch (const IconTaskIdle &) {
  } catch (const IconTaskSuspended &) {
  }
}
