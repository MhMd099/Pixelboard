#include "TaskRaumschiff.h"

#include <Arduino.h>
#include <FastLED.h>

#include "RaumschiffGame.h"
#include "HardwareUtils.h"
extern volatile int aktiverTask;

static int lastTaskState = -1;
#include "TaskRaumschiff.h"

#include <Arduino.h>
#include "RaumschiffGame.h"

extern volatile int aktiverTask;

void taskRaumschiffHandler(void *pvParameters)
{
    static int lastTask = -1;

    for (;;)
    {
        // ❌ nicht aktiv → schlafen + reset trigger vorbereiten
        if (aktiverTask != 6)
        {
            lastTask = -1;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        // 🔥 TASK ENTRY DETECTION
        if (lastTask != 6)
        {
            raumschiffResetGame();
            lastTask = 6;

            Serial.println("Raumschiff gestartet + Reset ausgeführt");
        }

        // 🔒 Display lock
        if (!lockDisplay(20))
        {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        // 🚀 EINZIGER GAME CALL
        raumschiffGameTick();

        unlockDisplay();

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
