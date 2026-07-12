#include "TaskRaumschiff.h"

#include <Arduino.h>

#include "HardwareUtils.h"
#include "RaumschiffGame.h"

extern volatile int aktiverTask;

void taskRaumschiffHandler(void *pvParameters)
{
    static int lastTask = -1;

    for (;;)
    {
        // Inactive tasks sleep and prepare a clean entry on the next launch.
        if (aktiverTask != 6)
        {
            lastTask = -1;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (lastTask != 6)
        {
            raumschiffResetGame();
            lastTask = 6;
            Serial.println("Raumschiff gestartet und zurueckgesetzt.");
        }

        if (!lockDisplay(20))
        {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        raumschiffGameTick();
        unlockDisplay();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
