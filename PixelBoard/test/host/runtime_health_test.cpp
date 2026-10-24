#include <cassert>
#include <cstdio>
#include "Arduino.h"
#include "esp_task_wdt.h"
#include "RuntimeHealth.h"

static uint32_t nowMs = 0;
static uint32_t lastFeedMs = 0;
static uint32_t maxFeedGapMs = 0;
static uint32_t resetCount = 0;
static int watchdogTimeoutSeconds = 0;

esp_err_t esp_task_wdt_init(int seconds, bool panic) {
    assert(panic);
    watchdogTimeoutSeconds = seconds;
    return ESP_OK;
}

esp_err_t esp_task_wdt_add(void*) { return ESP_OK; }

esp_err_t esp_task_wdt_reset() {
    uint32_t gap = nowMs - lastFeedMs;
    if (gap > maxFeedGapMs) maxFeedGapMs = gap;
    lastFeedMs = nowMs;
    ++resetCount;
    return ESP_OK;
}

void vTaskDelay(TickType_t ticks) {
    assert(ticks > 0);
    nowMs += ticks;
    assert(nowMs - lastFeedMs < (uint32_t)watchdogTimeoutSeconds * 1000);
}

int main() {
    initRuntimeWatchdog();
    registerCurrentTaskWithWatchdog();
    assert(watchdogTimeoutSeconds == 8);
    // Three missing-controller retries must not trigger the watchdog.
    for (int i = 0; i < 3; ++i) delayWithRuntimeWatchdog(10000);
    assert(nowMs == 30000);
    assert(maxFeedGapMs <= 250);
    assert(resetCount >= 120);

    // Connected-controller polling and zero delay retain their durations.
    delayWithRuntimeWatchdog(15);
    assert(nowMs == 30015);
    delayWithRuntimeWatchdog(0);
    assert(nowMs == 30015);
    std::puts("OK: 30s offline polling, watchdog feed gap <= 250ms");
}
