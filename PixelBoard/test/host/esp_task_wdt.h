#pragma once
#include <cstddef>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_INVALID_STATE = 0x103;
esp_err_t esp_task_wdt_init(int timeoutSeconds, bool panic);
esp_err_t esp_task_wdt_add(void* task);
esp_err_t esp_task_wdt_reset();
