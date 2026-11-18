#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
TaskHandle_t xTaskCreateStatic(void (*fn)(void *),const char *name,unsigned size,void *arg,unsigned priority,StackType_t *stack,StaticTask_t *tcb);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
void xTaskNotifyGive(TaskHandle_t task);
unsigned ulTaskNotifyTake(BaseType_t clear,unsigned ticks);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
