#ifndef TASK_MARIO_H
#define TASK_MARIO_H

#include <Arduino.h>

extern uint8_t marioLives;

void taskMarioHandler(void *pvParameters);
void renderMarioMenuPreview();
String marioStateJson();
void marioRequestRestart();

#endif
