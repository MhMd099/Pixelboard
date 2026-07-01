#ifndef RAUMSCHIFFGAME_H
#define RAUMSCHIFFGAME_H

#include <Arduino.h>

struct Spieler
{
    int x;
    int y;
    uint8_t size; // NEU
};

struct Asteroid
{
    int x;
    int y;
    uint8_t size; // NEU

    uint8_t speed;
    uint8_t type;
};

extern Spieler spieler;
extern Asteroid asteroiden[];
extern const uint8_t asteroidAnzahl;

void raumschiffResetGame();
void raumschiffGameTick();

#endif