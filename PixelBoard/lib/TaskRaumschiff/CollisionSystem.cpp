#include "CollisionSystem.h"

extern Spieler spieler;
extern Asteroid asteroiden[];
extern const uint8_t asteroidAnzahl;

void collisionUpdate()
{
    for (int i = 0; i < asteroidAnzahl; i++)
    {
        if (spieler.x < asteroiden[i].x + asteroiden[i].size &&
            spieler.x + spieler.size > asteroiden[i].x &&
            spieler.y < asteroiden[i].y + asteroiden[i].size &&
            spieler.y + spieler.size > asteroiden[i].y)
        {
            Serial.println("HIT PLAYER");
        }
    }
}