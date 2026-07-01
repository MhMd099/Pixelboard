#include "RaumschiffGame.h"

#include <FastLED.h>
#include <Arduino.h>

#include "HardwareUtils.h"
#include "Joystick.h"
#include "CollisionSystem.h"

extern Joystick joystick1;
extern Joystick joystick3;

static bool gameInitialized = false;
static bool gameActive = false;
static int lastTaskState = -1;

Spieler spieler;

struct Stern
{
    float x;
    uint8_t y;
    uint8_t speed;
    uint8_t brightness;
};

static const uint8_t sternAnzahl = 45;
static Stern sterne[sternAnzahl];

const uint8_t asteroidAnzahl = 6;
Asteroid asteroiden[asteroidAnzahl];


// ---------------- INPUT ----------------
void raumschiffHandleInput()
{
    int x = joystick1.readXPercent();
    int y = joystick1.readYPercent();

    if (abs(x) < 25)
        x = joystick3.readXPercent();

    if (abs(y) < 25)
        y = joystick3.readYPercent();

    if (x > 60) spieler.y--;
    if (x < -60) spieler.y++;
    if (y > 60) spieler.x++;
    if (y < -60) spieler.x--;

    spieler.x = constrain(spieler.x, 1, 30);
    spieler.y = constrain(spieler.y, 1, 14);
}


// ---------------- UPDATE WORLD ----------------
static void raumschiffUpdateState()
{
    for (int i = 0; i < sternAnzahl; i++)
    {
        sterne[i].x -= sterne[i].speed * 0.08f;

        if (sterne[i].x < 0)
        {
            sterne[i].x = random(32, 40);
            sterne[i].y = random(0, 16);
        }
    }

    for (int i = 0; i < asteroidAnzahl; i++)
    {
        asteroiden[i].x -= asteroiden[i].speed;

        if (asteroiden[i].x < 0)
        {
            asteroiden[i].x = random(32, 50);
            asteroiden[i].y = random(2, 14);
        }
    }
}


// ---------------- RENDER ----------------
static void raumschiffRender()
{
    FastLED.clear();

    for (int i = 0; i < sternAnzahl; i++)
    {
        setPixel((int)sterne[i].x,
                 sterne[i].y,
                 CRGB(sterne[i].brightness,
                      sterne[i].brightness,
                      sterne[i].brightness));
    }

    for (int i = 0; i < asteroidAnzahl; i++)
    {
        for (int dx = 0; dx < asteroiden[i].size; dx++)
        {
            for (int dy = 0; dy < asteroiden[i].size; dy++)
            {
                setPixel(
                    asteroiden[i].x + dx,
                    asteroiden[i].y + dy,
                    CRGB(120, 120, 120));
            }
        }
    }

    for (int dx = 0; dx < spieler.size; dx++)
    {
        for (int dy = 0; dy < spieler.size; dy++)
        {
            setPixel(
                spieler.x + dx,
                spieler.y + dy,
                CRGB(0, 255, 0));
        }
    }

    FastLED.show();
}


// ---------------- RESET GAME ----------------
void raumschiffResetGame()
{
    spieler.x = 10;
    spieler.y = 10;
    spieler.size = 3;

    gameActive = true;

    for (int i = 0; i < sternAnzahl; i++)
    {
        sterne[i].x = random(10, 32);
        sterne[i].y = random(0, 16);
        sterne[i].speed = random(1, 4);
        sterne[i].brightness = random(40, 180);
    }

    for (int i = 0; i < asteroidAnzahl; i++)
    {
        asteroiden[i].x = random(32, 50);
        asteroiden[i].y = random(2, 14);
        asteroiden[i].speed = random(1, 3);
        asteroiden[i].type = random(0, 3);
        asteroiden[i].size = 3;
    }
}


// ---------------- GAME LOOP ----------------
void raumschiffGameTick()
{
    if (!gameInitialized)
    {
        raumschiffResetGame();
        gameInitialized = true;
    }

    raumschiffHandleInput();
    raumschiffUpdateState();
    collisionUpdate();
    raumschiffRender();
}