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
