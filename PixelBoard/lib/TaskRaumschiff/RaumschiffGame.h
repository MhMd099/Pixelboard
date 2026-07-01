#ifndef RAUMSCHIFF_GAME_H
#define RAUMSCHIFF_GAME_H

#include <Arduino.h>

enum GameState
{
    MENU,
    PLAYING,
    BOSS,
    GAME_OVER
};

/* =========================
   PLAYER
========================= */

struct Spieler
{
    int x;
    int y;
    uint8_t size;

    int hp;

    uint32_t shootCooldown;
    uint32_t dashCooldown;
    uint32_t invulnerableUntil;
};

/* =========================
   PROJECTILE
========================= */

struct Projectile
{
    int x;
    int y;
    int dx;
    int dy;

    bool active;
};

/* =========================
   ASTEROID
========================= */

struct Asteroid
{
    int x;
    int y;
    uint8_t hp;
    bool active;
};

/* =========================
   BOSS
========================= */

struct Boss
{
    int x;
    int y;
    int hp;
    uint8_t phase;
    uint32_t attackTimer;
    bool active;
};

/* =========================
   API
========================= */

void raumschiffGameTick();
void raumschiffResetGame();

GameState getGameState();
uint32_t getRaumschiffScore();
void raumschiffAddScore(uint8_t points);
void raumschiffGameOver();

Spieler& getSpieler();
Projectile* getPlayerProjectiles();
Projectile* getEnemyProjectiles();
Asteroid* getAsteroiden();
Boss& getBoss();

uint8_t getProjectileCount();
uint8_t getAsteroidCount();

#endif
