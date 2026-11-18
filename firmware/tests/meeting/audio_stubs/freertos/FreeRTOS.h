#pragma once
#include <stdint.h>
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(v) (v)
typedef int BaseType_t;
typedef uint32_t StackType_t;
typedef uint32_t TickType_t;
typedef struct {int unused;} StaticTask_t;
