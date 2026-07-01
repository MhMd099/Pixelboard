#include "TaskRaumschiff.h"
#include <Arduino.h>
#include "RaumschiffGame.h"
#include "Joystick.h"
#include "Config.h"
#include "HardwareUtils.h"
#include "SoundUtils.h"

extern Joystick joystick1;
extern Joystick joystick3;

extern volatile int aktiverTask;
extern void drawPixel(int x, int y, CRGB c);
extern bool lockDisplay(uint32_t timeout);
extern void unlockDisplay();

static GameStateRaumschiff state = STATE_RS_PLAYING;

void taskRaumschiffHandler(void *pv) {

    RaumschiffInit();

    for (;;) {

        if (aktiverTask != 7) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        RaumschiffUpdate(
            joystick1.readXPercent(),
            joystick1.readYPercent(),
            joystick1.isPressed()
        );

        RaumschiffRender();

        unlockDisplay();
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
}