#pragma once
#include <Arduino.h>
#include <FastLED.h>

enum GameStateRaumschiff {
    STATE_RS_PLAYING,
    STATE_RS_GAMEOVER
};

void RaumschiffInit();

void RaumschiffUpdate(int xInput, int yInput, bool shoot);

void RaumschiffRender();