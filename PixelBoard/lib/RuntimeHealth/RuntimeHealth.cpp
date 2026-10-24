#include "RuntimeHealth.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

static bool watchdogReady = false;

void initRuntimeWatchdog() {
    esp_err_t result = esp_task_wdt_init(8, true);
    watchdogReady = result == ESP_OK || result == ESP_ERR_INVALID_STATE;
}

void registerCurrentTaskWithWatchdog() {
    if (watchdogReady) esp_task_wdt_add(NULL);
}

void feedRuntimeWatchdog() {
    if (watchdogReady) esp_task_wdt_reset();
}

void delayWithRuntimeWatchdog(uint32_t milliseconds) {
    TickType_t remaining = pdMS_TO_TICKS(milliseconds);
    const TickType_t sliceTicks = pdMS_TO_TICKS(250) > 0 ? pdMS_TO_TICKS(250) : 1;
    while (remaining > 0) {
        feedRuntimeWatchdog();
        TickType_t slice = remaining < sliceTicks ? remaining : sliceTicks;
        vTaskDelay(slice);
        remaining -= slice;
    }
    feedRuntimeWatchdog();
}
