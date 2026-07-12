#include "TaskRaumschiff.h"

#include <Arduino.h>

#include "HardwareUtils.h"
#include "RaumschiffGame.h"
#include "RuntimeHealth.h"

extern volatile int aktiverTask;

void taskRaumschiffHandler(void *pvParameters)
{
    registerCurrentTaskWithWatchdog();
    static int lastTask = -1;
    TickType_t lastWake = xTaskGetTickCount();

    for (;;)
    {
        // Inactive tasks sleep and prepare a clean entry on the next launch.
        if (aktiverTask != 6)
        {
            feedRuntimeWatchdog();
            lastTask = -1;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (lastTask != 6)
        {
            raumschiffResetGame();
            lastTask = 6;
            lastWake = xTaskGetTickCount();
            Serial.println("Raumschiff gestartet und zurueckgesetzt.");
        }

        if (!lockDisplay(20))
        {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        raumschiffGameTick();
        unlockDisplay();
        feedRuntimeWatchdog();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(16));
    }
}
