#pragma once
#include "esphome/core/hal.h"
// The icon worker is a persistent task: each finish_icon_task() runs it until it
// waits for the next notification.
typedef void *TaskHandle_t;
#define tskNO_AFFINITY 0x7FFFFFFF
#define portMAX_DELAY 0xFFFFFFFFu
#define pdTRUE 1
struct IconTaskIdle {};
inline void (*icon_task)(void *)=nullptr;
inline void *icon_task_arg=nullptr;
inline int icon_notifications=0;
inline int xTaskCreatePinnedToCoreWithCaps(void (*task)(void *),const char *,int,void *arg,int,TaskHandle_t *handle,int,unsigned){
 icon_task=task;icon_task_arg=arg;if(handle)*handle=reinterpret_cast<TaskHandle_t>(&icon_task);return pdPASS;}
inline void xTaskNotifyGive(TaskHandle_t){++icon_notifications;}
inline unsigned ulTaskNotifyTake(int,unsigned){if(!icon_notifications)throw IconTaskIdle{};const int n=icon_notifications;icon_notifications=0;return n;}
inline void vTaskDelete(void *){}
inline void vTaskDelay(unsigned){++icon_clock;}
inline void finish_icon_task(){try{icon_task(icon_task_arg);}catch(const IconTaskIdle &){}}
