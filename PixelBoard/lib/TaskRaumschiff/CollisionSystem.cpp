#include "CollisionSystem.h"
#include "RaumschiffGame.h"
#include "SoundUtils.h"

static bool samePixel(int ax, int ay, int bx, int by)
{
    return ax == bx && ay == by;
}

static bool pointInAsteroid(int x, int y, const Asteroid& asteroid)
{
    return asteroid.active &&
           x >= asteroid.x &&
           x < asteroid.x + asteroid.size &&
           y >= asteroid.y &&
           y < asteroid.y + asteroid.size;
}

static bool rangesOverlap(int aMin, int aMax, int bMin, int bMax)
{
    return aMin <= bMax && bMin <= aMax;
}

static bool sweptShotHitsAsteroid(const Projectile& shot, const Asteroid& asteroid)
{
    if (!asteroid.active)
        return false;

    const int shotPadding = shot.charged ? 1 : 0;
    const int asteroidPadding = 1;
    int shotMinX = min(shot.prevX, shot.x);
    int shotMaxX = max(shot.prevX, shot.x);
    int shotMinY = min(shot.prevY, shot.y);
    int shotMaxY = max(shot.prevY, shot.y);

    int asteroidMinX = min(asteroid.prevX, asteroid.x) - asteroidPadding;
    int asteroidMaxX = max(asteroid.prevX + asteroid.size - 1,
                           asteroid.x + asteroid.size - 1) + asteroidPadding;
    int asteroidMinY = min(asteroid.prevY, asteroid.y) - asteroidPadding;
    int asteroidMaxY = max(asteroid.prevY + asteroid.size - 1,
                           asteroid.y + asteroid.size - 1) + asteroidPadding;

    return rangesOverlap(shotMinX - shotPadding, shotMaxX + shotPadding, asteroidMinX, asteroidMaxX) &&
           rangesOverlap(shotMinY - shotPadding, shotMaxY + shotPadding, asteroidMinY, asteroidMaxY);
}

static bool pointInPowerUp(int x, int y, const PowerUp& powerUp)
{
    return powerUp.active &&
           x >= powerUp.x - 1 &&
           x <= powerUp.x &&
           y == powerUp.y;
}

static bool pointInHazard(int x, int y, const Hazard& hazard)
{
    if (!hazard.active)
        return false;

    return abs(x - hazard.x) <= hazard.radius &&
           abs(y - hazard.y) <= hazard.radius;
}

static bool sweptShotHitsHazard(const Projectile& shot, const Hazard& hazard)
{
    if (!hazard.active)
        return false;

    int shotMinX = min(shot.prevX, shot.x);
    int shotMaxX = max(shot.prevX, shot.x);
    int shotMinY = min(shot.prevY, shot.y);
    int shotMaxY = max(shot.prevY, shot.y);
    int hazardMinX = min(hazard.prevX - hazard.radius, hazard.x - hazard.radius);
    int hazardMaxX = max(hazard.prevX + hazard.radius, hazard.x + hazard.radius);
    int hazardMinY = min(hazard.prevY - hazard.radius, hazard.y - hazard.radius);
    int hazardMaxY = max(hazard.prevY + hazard.radius, hazard.y + hazard.radius);

    return rangesOverlap(shotMinX, shotMaxX, hazardMinX, hazardMaxX) &&
           rangesOverlap(shotMinY, shotMaxY, hazardMinY, hazardMaxY);
}

static bool sweptShotHitsPowerUp(const Projectile& shot, const PowerUp& powerUp)
{
    if (!powerUp.active)
        return false;

    int shotMinX = min(shot.prevX, shot.x);
    int shotMaxX = max(shot.prevX, shot.x);
    int shotMinY = min(shot.prevY, shot.y);
    int shotMaxY = max(shot.prevY, shot.y);
    int powerMinX = min(powerUp.prevX - 1, powerUp.x - 1);
    int powerMaxX = max(powerUp.prevX, powerUp.x);

    return rangesOverlap(shotMinX, shotMaxX, powerMinX, powerMaxX) &&
           rangesOverlap(shotMinY, shotMaxY, powerUp.y, powerUp.y);
}

static bool pointInBossForgiving(int x, int y, const Boss& boss)
{
    if (!boss.active)
        return false;

    int half = boss.size / 2;
    return x >= boss.x - half - 1 &&
           x <= boss.x + half + 1 &&
           y >= boss.y - half - 1 &&
           y <= boss.y + half + 1;
}

static bool sweptShotHitsBoss(const Projectile& shot, const Boss& boss)
{
    if (!boss.active)
        return false;

    int half = boss.size / 2 + 1;
    int shotMinX = min(shot.prevX, shot.x);
    int shotMaxX = max(shot.prevX, shot.x);
    int shotMinY = min(shot.prevY, shot.y);
    int shotMaxY = max(shot.prevY, shot.y);

    return rangesOverlap(shotMinX, shotMaxX, boss.x - half, boss.x + half) &&
           rangesOverlap(shotMinY, shotMaxY, boss.y - half, boss.y + half);
}

static void damagePlayer(Spieler& player)
{
    if (player.shield > 0)
    {
        player.shield--;
        Audio::playBuff();
        return;
    }

    player.hp--;
    Audio::playHit();
}

static void applyBlackHoleNerf(Spieler& player)
{
    damagePlayer(player);
    player.invertUntil = millis() + 4500;
}

static bool anyLivingPlayer(Spieler* players, uint8_t playerCount)
{
    for (uint8_t i = 0; i < playerCount; i++)
        if (players[i].active && players[i].hp > 0)
            return true;
    return false;
}

void collisionUpdate()
{
    Spieler* players = getSpielerListe();
    Projectile* playerShots = getPlayerProjectiles();
    Projectile* enemyShots = getEnemyProjectiles();
    Asteroid* asteroids = getAsteroiden();
    PowerUp* powerUps = getPowerUps();
    LaserBeam* laserBeams = getLaserBeams();
    Hazard* hazards = getHazards();
    Boss& boss = getBoss();

    uint8_t playerCount = getPlayerCount();
    uint8_t projectileCount = getProjectileCount();
    uint8_t asteroidCount = getAsteroidCount();
    uint8_t powerUpCount = getPowerUpCount();
    uint8_t laserBeamCount = getLaserBeamCount();
    uint8_t hazardCount = getHazardCount();

    /* player bullets -> boss bullets */
    for (uint8_t i = 0; i < projectileCount; i++)
    {
        if (!playerShots[i].active)
            continue;

        for (uint8_t j = 0; j < projectileCount; j++)
        {
            if (enemyShots[j].active &&
                samePixel(playerShots[i].x, playerShots[i].y, enemyShots[j].x, enemyShots[j].y))
            {
                uint8_t owner = playerShots[i].owner;
                playerShots[i].active = false;
                enemyShots[j].active = false;
                raumschiffAddScoreTo(owner, 1);
                Audio::playHit();
                break;
            }
        }
    }

    /* player bullets -> powerups */
    for (uint8_t i = 0; i < projectileCount; i++)
    {
        if (!playerShots[i].active)
            continue;

        for (uint8_t j = 0; j < powerUpCount; j++)
        {
            if (sweptShotHitsPowerUp(playerShots[i], powerUps[j]))
            {
                uint8_t owner = playerShots[i].owner;
                playerShots[i].active = false;
                raumschiffApplyPowerUpToPlayer(owner, powerUps[j].effect);
                powerUps[j].active = false;
                break;
            }
        }
    }

    /* player bullets -> hazards */
    for (uint8_t i = 0; i < projectileCount; i++)
    {
        if (!playerShots[i].active)
            continue;

        for (uint8_t j = 0; j < hazardCount; j++)
        {
            if (sweptShotHitsHazard(playerShots[i], hazards[j]))
            {
                uint16_t damage = playerShots[i].damage;
                if (damage < 1) damage = 1;
                playerShots[i].active = false;

                if (hazards[j].hp > damage)
                {
                    hazards[j].hp -= damage;
                    Audio::playHit();
                }
                else
                {
                    uint8_t owner = playerShots[i].owner;
                    hazards[j].active = false;
                    raumschiffRegisterKill(owner, 12, hazards[j].x, hazards[j].y);
                    Audio::playExplosion();
                }
                break;
            }
        }
    }

    /* laser beams -> hazards and asteroids */
    for (uint8_t l = 0; l < laserBeamCount; l++)
    {
        if (!laserBeams[l].active)
            continue;

        for (uint8_t h = 0; h < hazardCount; h++)
        {
            if (hazards[h].active &&
                abs(hazards[h].y - laserBeams[l].y) <= hazards[h].radius)
            {
                hazards[h].active = false;
                raumschiffRegisterKill(laserBeams[l].owner, 12, hazards[h].x, hazards[h].y);
            }
        }

        for (uint8_t a = 0; a < asteroidCount; a++)
        {
            if (!asteroids[a].active)
                continue;

            if (laserBeams[l].y >= asteroids[a].y &&
                laserBeams[l].y < asteroids[a].y + asteroids[a].size)
            {
                if (asteroids[a].hp > laserBeams[l].damage)
                {
                    asteroids[a].hp -= laserBeams[l].damage;
                    Audio::playHit();
                }
                else
                {
                    uint16_t points = asteroids[a].size * 4;
                    if (asteroids[a].kind == ENEMY_MINIBOSS) points += 35;
                    int dropX = asteroids[a].x;
                    int dropY = asteroids[a].y;
                    asteroids[a].active = false;
                    raumschiffRegisterKill(laserBeams[l].owner, points, dropX, dropY);
                    Audio::playExplosion();
                }
            }
        }

        if (boss.active &&
            abs(boss.y - laserBeams[l].y) <= (int)(boss.size / 2 + 1))
            raumschiffDamageBoss(laserBeams[l].owner, laserBeams[l].damage);
    }

    /* player bullets -> asteroids */
    for (uint8_t i = 0; i < projectileCount; i++)
    {
        if (!playerShots[i].active)
            continue;

        for (uint8_t j = 0; j < asteroidCount; j++)
        {
            if (sweptShotHitsAsteroid(playerShots[i], asteroids[j]))
            {
                uint8_t owner = playerShots[i].owner;
                uint16_t damage = playerShots[i].damage;
                if (damage < 1) damage = 1;
                playerShots[i].active = false;

                if (asteroids[j].hp > damage)
                {
                    asteroids[j].hp -= damage;
                    Audio::playHit();
                }
                else
                {
                    uint16_t points = asteroids[j].size == 1 ? 2 : asteroids[j].size * 4;
                    if (asteroids[j].kind == ENEMY_HEAVY) points += 10;
                    if (asteroids[j].kind == ENEMY_MINIBOSS) points += 35;
                    int dropX = asteroids[j].x;
                    int dropY = asteroids[j].y;
                    asteroids[j].active = false;
                    raumschiffRegisterKill(owner, points, dropX, dropY);
                    Audio::playExplosion();
                }
                break;
            }
        }
    }

    /* player bullets -> boss */
    if (boss.active)
    {
        for (uint8_t i = 0; i < projectileCount; i++)
        {
            if (playerShots[i].active &&
                (pointInBossForgiving(playerShots[i].x, playerShots[i].y, boss) ||
                 sweptShotHitsBoss(playerShots[i], boss)))
            {
                uint8_t owner = playerShots[i].owner;
                uint16_t damage = playerShots[i].damage;
                if (damage < 1) damage = 1;
                playerShots[i].active = false;
                raumschiffDamageBoss(owner, damage);
            }
        }
    }

    /* powerups -> players */
    for (uint8_t u = 0; u < powerUpCount; u++)
    {
        if (!powerUps[u].active)
            continue;

        for (uint8_t p = 0; p < playerCount; p++)
        {
            Spieler& player = players[p];
            if (!player.active || player.hp <= 0)
                continue;

            if (pointInPowerUp(player.x, player.y, powerUps[u]) ||
                pointInPowerUp(player.x - 1, player.y, powerUps[u]))
            {
                raumschiffApplyPowerUpToPlayer(p, powerUps[u].effect);
                powerUps[u].active = false;
                break;
            }
        }
    }

    /* hazards -> players */
    for (uint8_t h = 0; h < hazardCount; h++)
    {
        if (!hazards[h].active)
            continue;

        for (uint8_t p = 0; p < playerCount; p++)
        {
            Spieler& player = players[p];
            if (!player.active || player.hp <= 0)
                continue;

            bool dashProtected = (long)(millis() - player.invulnerableUntil) < 0;
            if (!dashProtected &&
                (pointInHazard(player.x, player.y, hazards[h]) ||
                 pointInHazard(player.x - 1, player.y, hazards[h])))
            {
                hazards[h].active = false;
                applyBlackHoleNerf(player);
                break;
            }
        }
    }

    /* asteroids -> players */
    for (uint8_t a = 0; a < asteroidCount; a++)
    {
        if (!asteroids[a].active)
            continue;

        for (uint8_t p = 0; p < playerCount; p++)
        {
            Spieler& player = players[p];
            if (!player.active || player.hp <= 0)
                continue;

            bool dashProtected = (long)(millis() - player.invulnerableUntil) < 0;
            if (!dashProtected && pointInAsteroid(player.x, player.y, asteroids[a]))
            {
                asteroids[a].active = false;
                damagePlayer(player);
                break;
            }
        }
    }

    /* enemy bullets -> players */
    for (uint8_t e = 0; e < projectileCount; e++)
    {
        if (!enemyShots[e].active)
            continue;

        for (uint8_t p = 0; p < playerCount; p++)
        {
            Spieler& player = players[p];
            if (!player.active || player.hp <= 0)
                continue;

            bool dashProtected = (long)(millis() - player.invulnerableUntil) < 0;
            bool bossRound = getGameState() == BOSS;
            if ((!dashProtected || bossRound) &&
                samePixel(enemyShots[e].x, enemyShots[e].y, player.x, player.y))
            {
                enemyShots[e].active = false;
                damagePlayer(player);
                break;
            }
        }
    }

    if (!anyLivingPlayer(players, playerCount))
    {
        Serial.println("GAME OVER");
        raumschiffGameOver();
    }
}
