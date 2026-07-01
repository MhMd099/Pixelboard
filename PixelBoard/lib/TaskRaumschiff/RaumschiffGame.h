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

static const uint8_t RAUMSCHIFF_MAX_PLAYERS = 2;

/* =========================
   PLAYER
========================= */

struct Spieler
{
    int x;
    int y;
    uint8_t size;

    int hp;
    uint32_t score;
    uint16_t kills;
    uint8_t damageLevel;
    uint8_t shield;

    uint32_t shootCooldown;
    uint32_t dashCooldown;
    uint32_t invulnerableUntil;
    uint32_t multiShotUntil;
    uint32_t damageBoostUntil;
    uint32_t invertUntil;

    bool active;
    bool ready;
};

/* =========================
   PROJECTILE
========================= */

struct Projectile
{
    int x;
    int y;
    int prevX;
    int prevY;
    int dx;
    int dy;

    uint8_t owner;
    uint16_t damage;
    bool charged;
    bool active;
};

/* =========================
   ASTEROID
========================= */

enum EnemyKind
{
    ENEMY_SMALL,
    ENEMY_MEDIUM,
    ENEMY_HEAVY,
    ENEMY_MINIBOSS
};

struct Asteroid
{
    int x;
    int y;
    int prevX;
    int prevY;
    uint16_t hp;
    uint16_t maxHp;
    uint8_t size;
    uint8_t speed;
    uint8_t kind;
    bool active;
};

/* =========================
   POWERUP
========================= */

enum PowerUpEffect
{
    POWERUP_NONE,
    POWERUP_SHIELD,
    POWERUP_MULTI_SHOT,
    POWERUP_HEAL,
    POWERUP_EMP,
    POWERUP_SLOW_FIELD,
    POWERUP_DAMAGE_BOOST,
    POWERUP_LASER
};

struct PowerUp
{
    int x;
    int y;
    int prevX;
    int prevY;
    uint8_t effect;
    bool active;
};

/* =========================
   LASER / HAZARD
========================= */

struct LaserBeam
{
    uint8_t owner;
    int y;
    uint16_t damage;
    uint32_t activeUntil;
    bool active;
};

enum HazardType
{
    HAZARD_NONE,
    HAZARD_BLACK_HOLE
};

struct Hazard
{
    int x;
    int y;
    int prevX;
    int prevY;
    uint8_t type;
    uint8_t radius;
    uint8_t speed;
    uint16_t hp;
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
    int maxHp;
    uint8_t phase;
    uint8_t size;
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
uint32_t getRaumschiffTeamScore();
void raumschiffAddScore(uint8_t points);
void raumschiffAddScoreTo(uint8_t playerIndex, uint8_t points);
void raumschiffRegisterKill(uint8_t playerIndex, uint16_t points, int dropX, int dropY);
void raumschiffGameOver();
void raumschiffSetWebInput(int8_t dx, int8_t dy, bool shoot, bool dash, bool charge);
void raumschiffRequestWebSpawn(uint8_t size, uint16_t hp = 0, uint8_t speed = 0, uint8_t kind = ENEMY_SMALL);
void raumschiffRequestBoss(uint16_t hp, uint8_t size);
void raumschiffRequestHazard(uint8_t type, uint8_t radius, uint8_t speed, uint16_t hp);
void raumschiffRequestPowerUp();
void raumschiffSetDirectorSettings(uint16_t spawnIntervalMs,
                                   uint8_t maxActiveAsteroids,
                                   uint8_t smallAsteroidPercent,
                                   uint16_t asteroidMoveMs,
                                   uint8_t powerUpChancePercent,
                                   bool autoWhenWebConnected);
void raumschiffSetPlayerName(uint8_t playerIndex, const String& name);
void raumschiffApplyPowerUpToPlayer(uint8_t playerIndex, uint8_t effect);
void raumschiffMaybeDropPowerUp(int x, int y);
void raumschiffDamageBoss(uint8_t owner, uint16_t damage);
bool raumschiffWebPlayerConnected();
String raumschiffStateJson();

Spieler& getSpieler();
Spieler* getSpielerListe();
Projectile* getPlayerProjectiles();
Projectile* getEnemyProjectiles();
Asteroid* getAsteroiden();
PowerUp* getPowerUps();
LaserBeam* getLaserBeams();
Hazard* getHazards();
Boss& getBoss();

uint8_t getPlayerCount();
uint8_t getProjectileCount();
uint8_t getAsteroidCount();
uint8_t getPowerUpCount();
uint8_t getLaserBeamCount();
uint8_t getHazardCount();

#endif
