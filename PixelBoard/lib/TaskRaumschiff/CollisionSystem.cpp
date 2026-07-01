#include "CollisionSystem.h"
#include "RaumschiffGame.h"

extern Spieler& getSpieler();
extern Projectile* getPlayerProjectiles();
extern Projectile* getEnemyProjectiles();
extern Asteroid* getAsteroiden();
extern Boss& getBoss();

extern uint8_t getProjectileCount();
extern uint8_t getAsteroidCount();

static bool hit(int ax, int ay, int bx, int by)
{
    return abs(ax - bx) <= 1 && abs(ay - by) <= 1;
}

void collisionUpdate()
{
    Spieler& s = getSpieler();
    Projectile* p = getPlayerProjectiles();
    Projectile* e = getEnemyProjectiles();
    Asteroid* a = getAsteroiden();
    Boss& b = getBoss();

    uint8_t count = getProjectileCount();
    uint8_t asteroidCount = getAsteroidCount();

    /* player bullets -> asteroids */
    for (int i = 0; i < count; i++)
    {
        if (!p[i].active)
            continue;

        for (int j = 0; j < asteroidCount; j++)
        {
            if (a[j].active && hit(p[i].x, p[i].y, a[j].x, a[j].y))
            {
                p[i].active = false;
                if (a[j].hp > 0)
                    a[j].hp--;

                if (a[j].hp == 0)
                {
                    a[j].active = false;
                    raumschiffAddScore(1);
                }
                break;
            }
        }
    }

    /* player bullets -> boss */
    if (b.active)
    {
        for (int i = 0; i < count; i++)
        {
            if (p[i].active && hit(p[i].x, p[i].y, b.x, b.y))
            {
                p[i].active = false;
                b.hp--;

                if (b.hp <= 0)
                {
                    b.active = false;
                    raumschiffAddScore(10);
                }
            }
        }
    }

    bool vulnerable = (long)(millis() - s.invulnerableUntil) >= 0;

    /* asteroids -> player */
    if (vulnerable)
    {
        for (int i = 0; i < asteroidCount; i++)
        {
            if (a[i].active && hit(a[i].x, a[i].y, s.x, s.y))
            {
                a[i].active = false;
                s.hp--;

                if (s.hp <= 0)
                {
                    Serial.println("GAME OVER");
                    raumschiffGameOver();
                    return;
                }
            }
        }
    }

    /* enemy bullets -> player */
    for (int i = 0; i < count; i++)
    {
        if (vulnerable && e[i].active && hit(e[i].x, e[i].y, s.x, s.y))
        {
            e[i].active = false;
            s.hp--;

            if (s.hp <= 0)
            {
                Serial.println("GAME OVER");
                raumschiffGameOver();
                return;
            }
        }
    }
}
