#pragma once
#include <cstdint>

using TickType_t = uint32_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
void vTaskDelay(TickType_t ticks);
