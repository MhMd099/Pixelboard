#include "RaumschiffGame.h"
#include "CollisionSystem.h"

#include "Joystick.h"
#include "HardwareUtils.h"
#include <FastLED.h>

extern Joystick joystick1;

/* =========================
   STATE
========================= */

static GameState gameState = MENU;

/* =========================
   PLAYER
========================= */

static Spieler spieler;

/* =========================
   PROJECTILES
========================= */

static const uint8_t projectileCount = 10;
static Projectile playerProjectiles[projectileCount];
static Projectile enemyProjectiles[projectileCount];

/* =========================
   ENEMIES
========================= */

static const uint8_t asteroidCount = 6;
static Asteroid asteroiden[asteroidCount];

static Boss boss;

/* =========================
   GAME CONTROL
========================= */

static uint32_t score = 0;
static uint32_t lastMoveMs = 0;
static uint32_t lastProjectileMoveMs = 0;
static uint32_t lastAsteroidMoveMs = 0;
static uint32_t lastAsteroidSpawnMs = 0;
static bool lastFireButtonState = false;
static bool bossSpawned = false;

static const uint8_t fieldWidth = 32;
static const uint8_t fieldHeight = 16;
static const uint8_t inputThreshold = 60;
static const uint16_t playerMoveIntervalMs = 95;
static const uint16_t projectileMoveIntervalMs = 55;
static const uint16_t asteroidMoveIntervalMs = 180;
static const uint16_t shootIntervalMs = 333;
static const uint16_t dashCooldownMs = 2000;
static const uint16_t dashInvulnerableMs = 220;

/* =========================
   ACCESSORS
========================= */

GameState getGameState() { return gameState; }
uint32_t getRaumschiffScore() { return score; }

void raumschiffAddScore(uint8_t points)
{
    score += points;
}

void raumschiffGameOver()
{
    gameState = GAME_OVER;
}

Spieler& getSpieler() { return spieler; }

Projectile* getPlayerProjectiles()
{
    return playerProjectiles;
}

Projectile* getEnemyProjectiles()
{
    return enemyProjectiles;
}

Asteroid* getAsteroiden()
{
    return asteroiden;
}

Boss& getBoss()
{
    return boss;
}

uint8_t getProjectileCount()
{
    return projectileCount;
}

uint8_t getAsteroidCount()
{
    return asteroidCount;
}

/* =========================
   INPUT
========================= */

static void handleInput()
{
    uint32_t now = millis();
    if (now - lastMoveMs < playerMoveIntervalMs)
        return;

    int x = joystick1.readXPercent();
    int y = joystick1.readYPercent();
    int dx = 0;
    int dy = 0;

    if (x > inputThreshold) dy = -1;
    else if (x < -inputThreshold) dy = 1;

    if (y > inputThreshold) dx = 1;
    else if (y < -inputThreshold) dx = -1;

    if (dx == 0 && dy == 0)
        return;

    spieler.x = constrain(spieler.x + dx, 1, fieldWidth - 2);
    spieler.y = constrain(spieler.y + dy, 1, fieldHeight - 2);
    lastMoveMs = now;
}


/* =========================
   SHOOT SYSTEM
========================= */

static void shoot()
{
    uint32_t now = millis();
    if (now - spieler.shootCooldown < shootIntervalMs)
        return;

    for (int i = 0; i < projectileCount; i++)
    {
        if (!playerProjectiles[i].active)
        {
            playerProjectiles[i].x = spieler.x;
            playerProjectiles[i].y = spieler.y;
            playerProjectiles[i].dx = 1;
            playerProjectiles[i].dy = 0;
            playerProjectiles[i].active = true;

            spieler.shootCooldown = now;
            break;
        }
    }
}

static void handleActions()
{
    bool pressed = joystick1.isPressed();
    if (pressed && !lastFireButtonState)
        shoot();
    lastFireButtonState = pressed;

    if (joystick1.doppelklickZaehler > 0)
    {
        joystick1.doppelklickZaehler--;
        uint32_t now = millis();
        if (now - spieler.dashCooldown >= dashCooldownMs)
        {
            int x = joystick1.readXPercent();
            int y = joystick1.readYPercent();
            int dx = 0;
            int dy = 0;

            if (x > inputThreshold) dy = -1;
            else if (x < -inputThreshold) dy = 1;

            if (y > inputThreshold) dx = 1;
            else if (y < -inputThreshold) dx = -1;

            if (dx == 0 && dy == 0)
                dx = 1;

            spieler.x = constrain(spieler.x + dx * 4, 1, fieldWidth - 2);
            spieler.y = constrain(spieler.y + dy * 4, 1, fieldHeight - 2);
            spieler.dashCooldown = now;
            spieler.invulnerableUntil = now + dashInvulnerableMs;
        }
    }
}

/* =========================
   UPDATE PROJECTILES
========================= */

static void updateProjectiles()
{
    uint32_t now = millis();
    if (now - lastProjectileMoveMs < projectileMoveIntervalMs)
        return;
    lastProjectileMoveMs = now;

    for (int i = 0; i < projectileCount; i++)
    {
        if (playerProjectiles[i].active)
        {
            playerProjectiles[i].x += playerProjectiles[i].dx;

            if (playerProjectiles[i].x >= fieldWidth)
                playerProjectiles[i].active = false;
        }

        if (enemyProjectiles[i].active)
        {
            enemyProjectiles[i].x += enemyProjectiles[i].dx;

            if (enemyProjectiles[i].x < 0)
                enemyProjectiles[i].active = false;
        }
    }
}

/* =========================
   ASTEROIDS
========================= */

static void spawnAsteroid()
{
    uint16_t interval = 1200;
    if (score > 25) interval = 650;
    else if (score > 12) interval = 850;

    uint32_t now = millis();
    if (now - lastAsteroidSpawnMs < interval)
        return;

    for (int i = 0; i < asteroidCount; i++)
    {
        if (!asteroiden[i].active)
        {
            asteroiden[i].x = fieldWidth - 1;
            asteroiden[i].y = random(1, fieldHeight - 1);
            asteroiden[i].hp = (score > 18 && random(0, 4) == 0) ? 2 : 1;
            asteroiden[i].active = true;
            lastAsteroidSpawnMs = now;
            break;
        }
    }
}

static void updateAsteroids()
{
    uint32_t now = millis();
    if (now - lastAsteroidMoveMs < asteroidMoveIntervalMs)
        return;
    lastAsteroidMoveMs = now;

    for (int i = 0; i < asteroidCount; i++)
    {
        if (!asteroiden[i].active)
            continue;

        asteroiden[i].x--;
        if (asteroiden[i].x < 0)
            asteroiden[i].active = false;
    }
}

/* =========================
   BOSS LOGIC
========================= */

static void bossLogic()
{
    if (!boss.active) return;

    uint32_t now = millis();
    if (now - boss.attackTimer > 900)
    {
        for (int i = 0; i < projectileCount; i++)
        {
            if (!enemyProjectiles[i].active)
            {
                enemyProjectiles[i].x = boss.x;
                enemyProjectiles[i].y = boss.y + random(-1, 2);
                enemyProjectiles[i].dx = -1;
                enemyProjectiles[i].dy = 0;
                enemyProjectiles[i].active = true;
                break;
            }
        }

        boss.y = constrain(boss.y + random(-1, 2), 2, fieldHeight - 3);
        boss.attackTimer = now;
    }
}

/* =========================
   RENDER
========================= */

static void render()
{
    FastLED.clear();

    /* player */
    setPixel(spieler.x, spieler.y, CRGB::Cyan);

    /* projectiles */
    for (int i = 0; i < projectileCount; i++)
    {
        if (playerProjectiles[i].active)
            setPixel(playerProjectiles[i].x,
                     playerProjectiles[i].y,
                     CRGB::Yellow);

        if (enemyProjectiles[i].active)
            setPixel(enemyProjectiles[i].x,
                     enemyProjectiles[i].y,
                     CRGB::Red);
    }

    /* asteroids */
    for (int i = 0; i < asteroidCount; i++)
    {
        if (!asteroiden[i].active)
            continue;

        CRGB color = (asteroiden[i].hp > 1) ? CRGB::OrangeRed : CRGB::Orange;
        setPixel(asteroiden[i].x, asteroiden[i].y, color);
    }

    /* boss */
    if (boss.active)
    {
        setPixel(boss.x, boss.y, CRGB::Magenta);
        setPixel(boss.x, boss.y - 1, CRGB::Purple);
        setPixel(boss.x, boss.y + 1, CRGB::Purple);
        setPixel(boss.x - 1, boss.y, CRGB::Purple);
    }

    FastLED.show();
}

static void renderMenu()
{
    FastLED.clear();
    uint32_t now = millis();

    for (int x = 0; x < fieldWidth; x++)
    {
        uint8_t pulse = (x + now / 80) % fieldWidth;
        setPixel(x, 0, CHSV(pulse * 8, 220, 90));
        setPixel(x, fieldHeight - 1, CHSV((pulse * 8) + 80, 220, 90));
    }

    setPixel(4, 8, CRGB::Cyan);
    setPixel(5, 7, CRGB::Cyan);
    setPixel(5, 8, CRGB::White);
    setPixel(5, 9, CRGB::Cyan);

    for (int x = 17; x < 26; x += 2)
        setPixel(x, 8 + ((x + now / 250) % 3) - 1, CRGB::Orange);

    FastLED.show();
}

static void updatePlaying()
{
    handleInput();
    handleActions();
    spawnAsteroid();
    updateProjectiles();
    updateAsteroids();
    collisionUpdate();
    render();
}

/* =========================
   GAME LOOP
========================= */

void raumschiffGameTick()
{
    if (gameState == MENU)
    {
        bool pressed = joystick1.isPressed();
        if (pressed && !lastFireButtonState)
            gameState = PLAYING;
        lastFireButtonState = pressed;

        renderMenu();
        return;
    }

    if (gameState == PLAYING)
    {
        updatePlaying();

        if (!bossSpawned && score > 15)
        {
            gameState = BOSS;
            bossSpawned = true;
            boss.active = true;
            boss.x = 28;
            boss.y = 8;
            boss.hp = 10;
            boss.phase = 1;
            boss.attackTimer = millis();
        }
        return;
    }

    if (gameState == BOSS)
    {
        updatePlaying();
        bossLogic();

        if (!boss.active)
            gameState = PLAYING;
        return;
    }

    if (gameState == GAME_OVER)
    {
        FastLED.clear();
        FastLED.show();
        return;
    }
}

/* =========================
   RESET
========================= */

void raumschiffResetGame()
{
    gameState = MENU;

    spieler.x = 5;
    spieler.y = 8;
    spieler.size = 1;
    spieler.hp = 3;
    spieler.shootCooldown = 0;
    spieler.dashCooldown = 0;
    spieler.invulnerableUntil = 0;

    score = 0;
    lastMoveMs = 0;
    lastProjectileMoveMs = 0;
    lastAsteroidMoveMs = 0;
    lastAsteroidSpawnMs = 0;
    lastFireButtonState = joystick1.isPressed();
    bossSpawned = false;
    joystick1.reset();

    boss.active = false;
    boss.x = 28;
    boss.y = 8;
    boss.hp = 0;
    boss.phase = 0;
    boss.attackTimer = 0;

    for (int i = 0; i < projectileCount; i++)
    {
        playerProjectiles[i].active = false;
        enemyProjectiles[i].active = false;
    }

    for (int i = 0; i < asteroidCount; i++)
        asteroiden[i].active = false;
}
