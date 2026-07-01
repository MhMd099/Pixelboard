#include "RaumschiffGame.h"
#include "HardwareUtils.h"
#include "SoundUtils.h"
#include "Config.h"

struct Bullet {
    int x;
    int y;
    bool active;
};

struct Enemy {
    int x;
    int y;
    bool active;
};

static int shipX = 16;
static int shipY = 14;

static Bullet bullets[10];
static Enemy enemies[6];

static unsigned long lastEnemySpawn = 0;
static unsigned long lastShot = 0;

void RaumschiffInit() {

    shipX = 16;
    shipY = 14;

    for (int i = 0; i < 10; i++) bullets[i].active = false;
    for (int i = 0; i < 6; i++) enemies[i].active = false;
}

static void spawnEnemy() {

    for (int i = 0; i < 6; i++) {
        if (!enemies[i].active) {
            enemies[i].x = random(0, 32);
            enemies[i].y = 0;
            enemies[i].active = true;
            break;
        }
    }
}

static void shoot() {

    if (millis() - lastShot < 200) return;
    lastShot = millis();

    for (int i = 0; i < 10; i++) {
        if (!bullets[i].active) {
            bullets[i].x = shipX;
            bullets[i].y = shipY - 1;
            bullets[i].active = true;
            playSound(SND_SELECT);
            break;
        }
    }
}

void RaumschiffUpdate(int xInput, int yInput, bool shootBtn) {

    shipX += xInput / 40;

    if (shipX < 0) shipX = 0;
    if (shipX > 31) shipX = 31;

    if (shootBtn) shoot();

    if (millis() - lastEnemySpawn > 800) {
        spawnEnemy();
        lastEnemySpawn = millis();
    }

    for (int i = 0; i < 10; i++) {
        if (bullets[i].active) {
            bullets[i].y--;

            if (bullets[i].y < 0)
                bullets[i].active = false;
        }
    }

    for (int i = 0; i < 6; i++) {
        if (enemies[i].active) {
            enemies[i].y++;

            if (enemies[i].y > 15)
                enemies[i].active = false;
        }
    }

    // Kollision
    for (int b = 0; b < 10; b++) {
        for (int e = 0; e < 6; e++) {

            if (!bullets[b].active || !enemies[e].active) continue;

            if (bullets[b].x == enemies[e].x &&
                bullets[b].y == enemies[e].y) {

                bullets[b].active = false;
                enemies[e].active = false;

                playSound(SND_EAT);
            }
        }
    }
}

void RaumschiffRender() {

    FastLED.clear();

    // Player Ship
    setPixel(shipX, shipY, CRGB::Blue);

    // Bullets
    for (int i = 0; i < 10; i++) {
        if (bullets[i].active)
            setPixel(bullets[i].x, bullets[i].y, CRGB::White);
    }

    // Enemies
    for (int i = 0; i < 6; i++) {
        if (enemies[i].active)
            setPixel(enemies[i].x, enemies[i].y, CRGB::Red);
    }

    FastLED.show();
}