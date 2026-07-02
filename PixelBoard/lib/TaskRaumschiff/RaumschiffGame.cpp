#include "RaumschiffGame.h"
#include "CollisionSystem.h"

#include "Config.h"
#include "HardwareUtils.h"
#include "Joystick.h"
#include "SoundUtils.h"
#include <FastLED.h>

extern Joystick joystick1;
extern Joystick joystick2;
extern Joystick joystick3;

static GameState gameState = MENU;

static Spieler spieler[RAUMSCHIFF_MAX_PLAYERS];

static const uint8_t projectileCount = 32;
static Projectile playerProjectiles[projectileCount];
static Projectile enemyProjectiles[projectileCount];

static const uint8_t asteroidCount = 16;
static Asteroid asteroiden[asteroidCount];

static const uint8_t powerUpCount = 6;
static PowerUp powerUps[powerUpCount];

static const uint8_t laserBeamCount = 3;
static LaserBeam laserBeams[laserBeamCount];

static const uint8_t hazardCount = 4;
static Hazard hazards[hazardCount];

static Boss boss;

static uint32_t score = 0;
static uint32_t lastMoveMs[RAUMSCHIFF_MAX_PLAYERS];
static uint32_t lastProjectileMoveMs = 0;
static uint32_t lastAsteroidMoveMs = 0;
static uint32_t lastHazardMoveMs = 0;
static uint32_t lastPowerUpMoveMs = 0;
static uint32_t lastAsteroidSpawnMs = 0;
static uint32_t lastAutoHazardSpawnMs = 0;
static uint32_t lastGameOverRenderMs = 0;
static uint32_t nextBossScore = 300;
static uint32_t lastMatchScore = 0;
static uint32_t lastMatchP1Score = 0;
static uint32_t lastMatchP2Score = 0;
static uint16_t lastMatchP1Kills = 0;
static uint16_t lastMatchP2Kills = 0;
static uint16_t lastMatchBosses = 0;
static uint32_t matchStartMs = 0;
static uint32_t lastMatchDurationSec = 0;
static uint32_t slowFieldUntil = 0;
static uint16_t bossesSpawned = 0;
static uint8_t bossVolleyCounter = 0;
static uint32_t lastWebEnergyRegenMs = 0;
static bool lastFireButtonState[RAUMSCHIFF_MAX_PLAYERS];
static bool highScoreSaved = false;
static char playerNames[RAUMSCHIFF_MAX_PLAYERS][16] = {"P1", "P2"};
static char lastMatchName[36] = "";

static const uint8_t fieldWidth = 32;
static const uint8_t fieldHeight = 16;
static const uint8_t inputThreshold = 60;
static const uint8_t dashInputThreshold = 72;
static const uint16_t playerMoveIntervalMs = 95;
static const uint16_t projectileMoveIntervalMs = 55;
static const uint16_t powerUpMoveIntervalMs = 240;
static const uint16_t shootIntervalMs = 333;
static const uint16_t chargedShootIntervalMs = 650;
static const uint16_t dashCooldownMs = 2000;
static const uint16_t dashInvulnerableMs = 220;
static const uint16_t webHeartbeatTimeoutMs = 3000;
static const uint16_t multiShotDurationMs = 9000;
static const uint16_t damageBoostDurationMs = 12000;
static const uint16_t slowFieldDurationMs = 7000;
static const uint16_t laserDurationMs = 130;
static const uint16_t bossScoreGapBase = 300;
static const uint16_t bossScoreGapGrowth = 120;
static const uint16_t webEnergyMax = 250;
static const uint16_t webEnergyStart = 110;
static const uint8_t p2InputAuto = 0;
static const uint8_t p2InputEsp32 = 1;
static const uint8_t p2InputI2c = 2;

static portMUX_TYPE webInputMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t webSpawnRequestSize = 0;
static volatile uint16_t webSpawnRequestHp = 0;
static volatile uint8_t webSpawnRequestSpeed = 1;
static volatile uint8_t webSpawnRequestKind = ENEMY_SMALL;
static volatile uint16_t webBossRequestHp = 0;
static volatile uint8_t webBossRequestSize = 3;
static volatile uint8_t webHazardRequestType = HAZARD_NONE;
static volatile uint8_t webHazardRequestRadius = 2;
static volatile uint8_t webHazardRequestSpeed = 1;
static volatile uint16_t webHazardRequestHp = 80;
static volatile uint8_t webPowerUpRequests = 0;
static volatile uint32_t webLastInputMs = 0;
static volatile uint16_t webEnergy = webEnergyStart;
static volatile uint8_t p2InputMode = p2InputAuto;
static volatile uint16_t directorSpawnIntervalMs = 1200;
static volatile uint16_t directorAsteroidMoveMs = 220;
static volatile uint8_t directorMaxActiveAsteroids = 8;
static volatile uint8_t directorSmallAsteroidPercent = 35;
static volatile uint8_t directorPowerUpChancePercent = 25;
static volatile bool directorAutoWhenWebConnected = false;

struct InputState
{
    int8_t dx;
    int8_t dy;
    int16_t axisX;
    int16_t axisY;
    bool shoot;
    bool dash;
    bool charge;
    bool touched;
};

static CRGB playerColor(uint8_t index)
{
    if (index == 0) return CRGB::Cyan;
    if (index == 1) return CRGB::Lime;
    return CRGB::DodgerBlue;
}

static uint8_t activeLivingPlayers()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < RAUMSCHIFF_MAX_PLAYERS; i++)
        if (spieler[i].active && spieler[i].hp > 0)
            count++;
    return count;
}

static bool anyLivingPlayer()
{
    return activeLivingPlayers() > 0;
}

static uint8_t activeAsteroidCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < asteroidCount; i++)
        if (asteroiden[i].active)
            count++;
    return count;
}

static uint8_t activePowerUpCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < powerUpCount; i++)
        if (powerUps[i].active)
            count++;
    return count;
}

static uint8_t activeHazardCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < hazardCount; i++)
        if (hazards[i].active)
            count++;
    return count;
}

static bool webConnectedNow()
{
    uint32_t last = webLastInputMs;
    return last != 0 && (uint32_t)(millis() - last) < webHeartbeatTimeoutMs;
}

static uint8_t secondsLeft(uint32_t untilMs)
{
    uint32_t now = millis();
    if ((long)(untilMs - now) <= 0)
        return 0;
    uint32_t remaining = (untilMs - now + 999) / 1000;
    return remaining > 255 ? 255 : (uint8_t)remaining;
}

static void addWebEnergy(uint16_t amount)
{
    portENTER_CRITICAL(&webInputMux);
    uint16_t current = webEnergy;
    current = current + amount > webEnergyMax ? webEnergyMax : current + amount;
    webEnergy = current;
    portEXIT_CRITICAL(&webInputMux);
}

static bool spendWebEnergy(uint16_t cost)
{
    bool ok = false;
    portENTER_CRITICAL(&webInputMux);
    webLastInputMs = millis();
    if (webEnergy >= cost)
    {
        webEnergy -= cost;
        ok = true;
    }
    portEXIT_CRITICAL(&webInputMux);
    return ok;
}

static String shortPlayerName(uint8_t index)
{
    if (index >= RAUMSCHIFF_MAX_PLAYERS)
        return "???";

    String name = String(playerNames[index]);
    name.trim();
    if (name == "")
        name = String("P") + String(index + 1);
    if (name.length() > 3)
        name = name.substring(0, 3);
    name.toUpperCase();
    return name;
}

static String playerNameOrDefault(uint8_t index)
{
    if (index >= RAUMSCHIFF_MAX_PLAYERS)
        return "Player";

    String name = String(playerNames[index]);
    name.trim();
    if (name == "")
        name = String("P") + String(index + 1);
    return name;
}

static void updateWebEnergy()
{
    uint32_t now = millis();
    if (!webConnectedNow() || gameState == MENU || gameState == GAME_OVER)
    {
        lastWebEnergyRegenMs = now;
        return;
    }

    if (lastWebEnergyRegenMs == 0)
        lastWebEnergyRegenMs = now;

    while (now - lastWebEnergyRegenMs >= 1000UL)
    {
        addWebEnergy(6);
        lastWebEnergyRegenMs += 1000UL;
    }
}

static uint16_t baseShotDamage(uint8_t playerIndex)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return 1;

    uint16_t damage = 1 + spieler[playerIndex].damageLevel;
    if (secondsLeft(spieler[playerIndex].damageBoostUntil) > 0)
        damage += 3;
    return damage;
}

static uint16_t normalShotDamage(uint8_t playerIndex)
{
    uint16_t damage = baseShotDamage(playerIndex);
    if (playerIndex < RAUMSCHIFF_MAX_PLAYERS &&
        secondsLeft(spieler[playerIndex].multiShotUntil) > 0)
        damage *= 3;
    return damage;
}

static uint16_t chargedShotDamage(uint8_t playerIndex)
{
    return baseShotDamage(playerIndex) * 3;
}

static uint8_t damageLevelFromProgress(uint8_t playerIndex)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return 0;

    uint8_t fromScore = spieler[playerIndex].score / 40;
    uint8_t fromKills = spieler[playerIndex].kills / 8;
    uint8_t level = fromScore + fromKills;
    if (level > 20)
        level = 20;
    return level;
}

static uint32_t nextDamageScore(uint8_t playerIndex)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return 40;

    uint32_t scoreBand = (spieler[playerIndex].score / 40) + 1;
    return scoreBand * 40;
}

static void refreshDamageLevel(uint8_t playerIndex)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return;

    uint8_t newLevel = damageLevelFromProgress(playerIndex);
    if (newLevel > spieler[playerIndex].damageLevel)
        Audio::playBuff();
    spieler[playerIndex].damageLevel = newLevel;
}

static void scheduleNextBoss()
{
    uint32_t gap = bossScoreGapBase + min((uint32_t)720, (uint32_t)bossesSpawned * bossScoreGapGrowth);
    nextBossScore = score + gap;
}

static void resetPlayer(uint8_t index, bool active)
{
    spieler[index].x = 4;
    spieler[index].y = 4 + index * 4;
    spieler[index].size = 1;
    spieler[index].hp = 3;
    spieler[index].score = 0;
    spieler[index].kills = 0;
    spieler[index].damageLevel = 0;
    spieler[index].shield = 0;
    spieler[index].shootCooldown = 0;
    spieler[index].dashCooldown = 0;
    spieler[index].invulnerableUntil = 0;
    spieler[index].multiShotUntil = 0;
    spieler[index].damageBoostUntil = 0;
    spieler[index].invertUntil = 0;
    spieler[index].active = active;
    spieler[index].ready = false;
}

GameState getGameState() { return gameState; }
uint32_t getRaumschiffScore() { return score; }
uint32_t getRaumschiffTeamScore() { return score; }

void raumschiffAddScore(uint8_t points)
{
    raumschiffAddScoreTo(0, points);
}

void raumschiffAddScoreTo(uint8_t playerIndex, uint8_t points)
{
    score += points;
    if (playerIndex < RAUMSCHIFF_MAX_PLAYERS)
    {
        spieler[playerIndex].score += points;
        refreshDamageLevel(playerIndex);
    }
}

static String teamName()
{
    String name = String(playerNames[0]) + "+" + String(playerNames[1]);
    name.trim();
    if (name == "+")
        return "Team";
    return name;
}

void raumschiffRegisterKill(uint8_t playerIndex, uint16_t points, int dropX, int dropY)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        playerIndex = 0;

    score += points;
    spieler[playerIndex].score += points;
    spieler[playerIndex].kills++;

    refreshDamageLevel(playerIndex);
    addWebEnergy(points * 3 + 8);

    raumschiffMaybeDropPowerUp(dropX, dropY);
}

void raumschiffGameOver()
{
    if (gameState == GAME_OVER)
        return;

    gameState = GAME_OVER;
    Audio::playExplosion();

    if (!highScoreSaved)
    {
        lastMatchScore = score;
        lastMatchP1Score = spieler[0].score;
        lastMatchP2Score = spieler[1].score;
        lastMatchP1Kills = spieler[0].kills;
        lastMatchP2Kills = spieler[1].kills;
        lastMatchBosses = bossesSpawned;
        lastMatchDurationSec = matchStartMs == 0 ? 0 : (millis() - matchStartMs) / 1000UL;
        String team = teamName();
        team.toCharArray(lastMatchName, sizeof(lastMatchName));
        saveHighScore(team, (int)score);

        for (uint8_t i = 0; i < RAUMSCHIFF_MAX_PLAYERS; i++)
        {
            String name = String(playerNames[i]);
            name.trim();
            if (i == 0 && name == "P1" && currentUser != "")
                name = currentUser;
            if (name != "")
                saveHighScore(name, (int)spieler[i].score);
        }
        highScoreSaved = true;
    }
}

void raumschiffSetWebInput(int8_t dx, int8_t dy, bool shoot, bool dash, bool charge)
{
    (void)dx;
    (void)dy;
    (void)shoot;
    (void)dash;
    (void)charge;
    portENTER_CRITICAL(&webInputMux);
    webLastInputMs = millis();
    portEXIT_CRITICAL(&webInputMux);
}

void raumschiffRequestWebSpawn(uint8_t size, uint16_t hp, uint8_t speed, uint8_t kind)
{
    if (size < 1) size = 1;
    if (size > 5) size = 5;
    if (hp > 8000) hp = 8000;
    if (speed < 1) speed = 1;
    if (speed > 16) speed = 16;
    if (kind > ENEMY_MINIBOSS) kind = ENEMY_SMALL;

    uint16_t cost = 8 + size * 3 + speed + kind * 10 + hp / 80;
    if (cost > 160) cost = 160;
    if (!spendWebEnergy(cost))
        return;

    portENTER_CRITICAL(&webInputMux);
    webSpawnRequestSize = size;
    webSpawnRequestHp = hp;
    webSpawnRequestSpeed = speed;
    webSpawnRequestKind = kind;
    webLastInputMs = millis();
    portEXIT_CRITICAL(&webInputMux);
}

void raumschiffRequestBoss(uint16_t hp, uint8_t size)
{
    if (hp < 1) hp = 250;
    if (hp > 20000) hp = 20000;
    if (size < 3) size = 3;
    if (size > 7) size = 7;

    uint16_t cost = 80 + hp / 150 + size * 8;
    if (cost > webEnergyMax) cost = webEnergyMax;
    if (!spendWebEnergy(cost))
        return;

    portENTER_CRITICAL(&webInputMux);
    webBossRequestHp = hp;
    webBossRequestSize = size;
    webLastInputMs = millis();
    portEXIT_CRITICAL(&webInputMux);
}

void raumschiffRequestHazard(uint8_t type, uint8_t radius, uint8_t speed, uint16_t hp)
{
    if (type == HAZARD_NONE) type = HAZARD_BLACK_HOLE;
    if (radius < 1) radius = 1;
    if (radius > 7) radius = 7;
    if (speed < 1) speed = 1;
    if (speed > 12) speed = 12;
    if (hp < 1) hp = 80;
    if (hp > 6000) hp = 6000;

    uint16_t cost = 45 + radius * 8 + speed * 3 + hp / 100;
    if (cost > webEnergyMax) cost = webEnergyMax;
    if (!spendWebEnergy(cost))
        return;

    portENTER_CRITICAL(&webInputMux);
    webHazardRequestType = type;
    webHazardRequestRadius = radius;
    webHazardRequestSpeed = speed;
    webHazardRequestHp = hp;
    webLastInputMs = millis();
    portEXIT_CRITICAL(&webInputMux);
}

void raumschiffRequestPowerUp()
{
    if (!spendWebEnergy(30))
        return;

    portENTER_CRITICAL(&webInputMux);
    if (webPowerUpRequests < 250)
        webPowerUpRequests++;
    webLastInputMs = millis();
    portEXIT_CRITICAL(&webInputMux);
}

void raumschiffSetP2InputMode(uint8_t mode)
{
    if (mode > p2InputI2c)
        mode = p2InputAuto;

    portENTER_CRITICAL(&webInputMux);
    p2InputMode = mode;
    webLastInputMs = millis();
    portEXIT_CRITICAL(&webInputMux);
}

void raumschiffSetDirectorSettings(uint16_t spawnIntervalMs,
                                   uint8_t maxActiveAsteroids,
                                   uint8_t smallAsteroidPercent,
                                   uint16_t asteroidMoveMs,
                                   uint8_t powerUpChancePercent,
                                   bool autoWhenWebConnected)
{
    spawnIntervalMs = constrain(spawnIntervalMs, (uint16_t)80, (uint16_t)8000);
    maxActiveAsteroids = constrain(maxActiveAsteroids, (uint8_t)1, asteroidCount);
    smallAsteroidPercent = constrain(smallAsteroidPercent, (uint8_t)0, (uint8_t)100);
    asteroidMoveMs = constrain(asteroidMoveMs, (uint16_t)15, (uint16_t)1500);
    powerUpChancePercent = constrain(powerUpChancePercent, (uint8_t)0, (uint8_t)100);

    portENTER_CRITICAL(&webInputMux);
    directorSpawnIntervalMs = spawnIntervalMs;
    directorMaxActiveAsteroids = maxActiveAsteroids;
    directorSmallAsteroidPercent = smallAsteroidPercent;
    directorAsteroidMoveMs = asteroidMoveMs;
    directorPowerUpChancePercent = powerUpChancePercent;
    directorAutoWhenWebConnected = autoWhenWebConnected;
    webLastInputMs = millis();
    portEXIT_CRITICAL(&webInputMux);
}

void raumschiffSetPlayerName(uint8_t playerIndex, const String& name)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return;

    uint8_t out = 0;
    for (uint16_t i = 0; i < name.length() && out < sizeof(playerNames[playerIndex]) - 1; i++)
    {
        char c = name.charAt(i);
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-')
        {
            playerNames[playerIndex][out++] = c;
        }
    }

    if (out == 0)
    {
        playerNames[playerIndex][0] = 'P';
        playerNames[playerIndex][1] = '1' + playerIndex;
        playerNames[playerIndex][2] = '\0';
        return;
    }

    playerNames[playerIndex][out] = '\0';
}

static bool spawnPowerUpAt(int x, int y)
{
    for (uint8_t i = 0; i < powerUpCount; i++)
    {
        if (!powerUps[i].active)
        {
            powerUps[i].x = constrain(x, 2, fieldWidth - 3);
            powerUps[i].y = constrain(y, 1, fieldHeight - 2);
            powerUps[i].prevX = powerUps[i].x;
            powerUps[i].prevY = powerUps[i].y;
            powerUps[i].effect = (uint8_t)random(POWERUP_SHIELD, POWERUP_LASER + 1);
            powerUps[i].active = true;
            return true;
        }
    }
    return false;
}

void raumschiffMaybeDropPowerUp(int x, int y)
{
    uint8_t chance = directorPowerUpChancePercent;
    if (chance == 0)
        return;
    if (random(0, 100) < chance)
        spawnPowerUpAt(x, y);
}

static bool activateLaserBeam(uint8_t playerIndex, int y, uint16_t damage, uint32_t now)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return false;

    for (uint8_t i = 0; i < laserBeamCount; i++)
    {
        if (!laserBeams[i].active)
        {
            laserBeams[i].owner = playerIndex;
            laserBeams[i].y = constrain(y, 0, fieldHeight - 1);
            laserBeams[i].damage = damage;
            laserBeams[i].activeUntil = now + laserDurationMs;
            laserBeams[i].damageApplied = false;
            laserBeams[i].active = true;
            return true;
        }
    }
    return false;
}

static bool fireLaser(uint8_t playerIndex)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return false;

    uint32_t now = millis();
    uint16_t damage = baseShotDamage(playerIndex) * 6;
    int centerY = spieler[playerIndex].y;

    bool fired = false;
    fired = activateLaserBeam(playerIndex, centerY - 1, damage, now) || fired;
    fired = activateLaserBeam(playerIndex, centerY, damage, now) || fired;
    fired = activateLaserBeam(playerIndex, centerY + 1, damage, now) || fired;

    if (fired)
        Audio::playBoss();
    return fired;
}

static void finishBoss(uint8_t owner, bool awardScore)
{
    if (!boss.active)
        return;

    int dropX = boss.x;
    int dropY = boss.y;
    boss.active = false;
    boss.phase = 0;

    if (awardScore && owner < RAUMSCHIFF_MAX_PLAYERS)
        raumschiffRegisterKill(owner, 40, dropX, dropY);

    bossesSpawned++;
    scheduleNextBoss();
    Audio::playExplosion();
}

void raumschiffApplyPowerUpToPlayer(uint8_t playerIndex, uint8_t effect)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return;

    Spieler& p = spieler[playerIndex];
    uint32_t now = millis();

    switch (effect)
    {
    case POWERUP_SHIELD:
        if (p.shield < 3)
            p.shield++;
        Audio::playBuff();
        break;

    case POWERUP_MULTI_SHOT:
        p.multiShotUntil = now + multiShotDurationMs;
        Audio::playBuff();
        break;

    case POWERUP_HEAL:
        if (p.hp < 5)
            p.hp++;
        Audio::playBuff();
        break;

    case POWERUP_EMP:
    {
        bool bossDefeated = false;
        for (uint8_t i = 0; i < asteroidCount; i++)
            asteroiden[i].active = false;
        for (uint8_t i = 0; i < projectileCount; i++)
            enemyProjectiles[i].active = false;
        if (boss.active)
        {
            int damage = min(8, boss.hp);
            boss.hp -= damage;
            if (boss.hp <= 0)
            {
                finishBoss(playerIndex, true);
                bossDefeated = true;
            }
        }
        if (!bossDefeated)
            Audio::playExplosion();
        break;
    }

    case POWERUP_SLOW_FIELD:
        slowFieldUntil = now + slowFieldDurationMs;
        Audio::playBuff();
        break;

    case POWERUP_DAMAGE_BOOST:
        p.damageBoostUntil = now + damageBoostDurationMs;
        Audio::playBuff();
        break;

    case POWERUP_LASER:
        fireLaser(playerIndex);
        break;

    default:
        break;
    }
}

void raumschiffDamageBoss(uint8_t owner, uint16_t damage)
{
    if (!boss.active || damage == 0)
        return;

    if (boss.hp > (int)damage)
    {
        boss.hp -= damage;
        if (owner < RAUMSCHIFF_MAX_PLAYERS)
        {
            uint16_t chipScore = damage / 10 + 1;
            if (chipScore > 12) chipScore = 12;
            raumschiffAddScoreTo(owner, (uint8_t)chipScore);
        }
        Audio::playHit();
        return;
    }

    finishBoss(owner, true);
}

bool raumschiffWebPlayerConnected()
{
    return webConnectedNow();
}

String raumschiffStateJson()
{
    String json = "{";
    json.reserve(1800);
    json += "\"state\":" + String((int)gameState);
    json += ",\"score\":" + String(score);
    json += ",\"web\":" + String(webConnectedNow() ? 1 : 0);
    json += ",\"webEnergy\":" + String((uint16_t)webEnergy);
    json += ",\"webEnergyMax\":" + String(webEnergyMax);
    json += ",\"p2InputMode\":" + String((uint8_t)p2InputMode);
    json += ",\"boss\":{\"active\":" + String(boss.active ? 1 : 0);
    json += ",\"hp\":" + String(boss.active ? boss.hp : 0);
    json += ",\"maxHp\":" + String(boss.maxHp);
    json += ",\"phase\":" + String(boss.phase);
    json += ",\"size\":" + String(boss.size);
    json += "}";
    json += ",\"director\":{\"spawnMs\":" + String((uint16_t)directorSpawnIntervalMs);
    json += ",\"moveMs\":" + String((uint16_t)directorAsteroidMoveMs);
    json += ",\"maxAsteroids\":" + String((uint8_t)directorMaxActiveAsteroids);
    json += ",\"smallPercent\":" + String((uint8_t)directorSmallAsteroidPercent);
    json += ",\"powerChance\":" + String((uint8_t)directorPowerUpChancePercent);
    json += ",\"autoWeb\":" + String(directorAutoWhenWebConnected ? 1 : 0);
    json += ",\"activeAsteroids\":" + String(activeAsteroidCount());
    json += ",\"activePowerUps\":" + String(activePowerUpCount());
    json += ",\"activeHazards\":" + String(activeHazardCount());
    json += ",\"slow\":" + String(secondsLeft(slowFieldUntil));
    json += "}";
    json += ",\"lastMatch\":{\"name\":\"" + String(lastMatchName) + "\"";
    json += ",\"score\":" + String(lastMatchScore);
    json += ",\"p1\":" + String(lastMatchP1Score);
    json += ",\"p2\":" + String(lastMatchP2Score);
    json += ",\"p1Name\":\"" + playerNameOrDefault(0) + "\"";
    json += ",\"p2Name\":\"" + playerNameOrDefault(1) + "\"";
    json += ",\"p1Kills\":" + String(lastMatchP1Kills);
    json += ",\"p2Kills\":" + String(lastMatchP2Kills);
    json += ",\"bosses\":" + String(lastMatchBosses);
    json += ",\"duration\":" + String(lastMatchDurationSec);
    json += "}";
    PlayerData top[3];
    int topCount = 0;
    getTopScores(top, topCount);
    if (topCount > 3) topCount = 3;
    json += ",\"top\":[";
    for (int i = 0; i < topCount; i++)
    {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + top[i].name + "\",\"score\":" + String(top[i].score) + "}";
    }
    json += "]";
    json += ",\"players\":[";
    for (uint8_t i = 0; i < RAUMSCHIFF_MAX_PLAYERS; i++)
    {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + String(playerNames[i]) + "\"";
        json += ",\"shortName\":\"" + shortPlayerName(i) + "\"";
        json += ",\"active\":" + String(spieler[i].active ? 1 : 0);
        json += ",\"alive\":" + String(spieler[i].hp > 0 ? 1 : 0);
        json += ",\"hp\":" + String(spieler[i].hp);
        json += ",\"score\":" + String(spieler[i].score);
        json += ",\"kills\":" + String(spieler[i].kills);
        json += ",\"level\":" + String(spieler[i].damageLevel);
        json += ",\"shield\":" + String(spieler[i].shield);
        json += ",\"multi\":" + String(secondsLeft(spieler[i].multiShotUntil));
        json += ",\"boost\":" + String(secondsLeft(spieler[i].damageBoostUntil));
        json += ",\"invert\":" + String(secondsLeft(spieler[i].invertUntil));
        json += ",\"baseDamage\":" + String(baseShotDamage(i));
        json += ",\"damage\":" + String(normalShotDamage(i));
        json += ",\"chargedDamage\":" + String(chargedShotDamage(i));
        json += ",\"nextDamageScore\":" + String(nextDamageScore(i));
        json += ",\"x\":" + String(spieler[i].x);
        json += ",\"y\":" + String(spieler[i].y);
        json += "}";
    }
    json += "]}";
    return json;
}

Spieler& getSpieler() { return spieler[0]; }
Spieler* getSpielerListe() { return spieler; }
Projectile* getPlayerProjectiles() { return playerProjectiles; }
Projectile* getEnemyProjectiles() { return enemyProjectiles; }
Asteroid* getAsteroiden() { return asteroiden; }
PowerUp* getPowerUps() { return powerUps; }
LaserBeam* getLaserBeams() { return laserBeams; }
Hazard* getHazards() { return hazards; }
Boss& getBoss() { return boss; }
uint8_t getPlayerCount() { return RAUMSCHIFF_MAX_PLAYERS; }
uint8_t getProjectileCount() { return projectileCount; }
uint8_t getAsteroidCount() { return asteroidCount; }
uint8_t getPowerUpCount() { return powerUpCount; }
uint8_t getLaserBeamCount() { return laserBeamCount; }
uint8_t getHazardCount() { return hazardCount; }

static int8_t stepFromAxis(int value, uint8_t threshold, bool positiveMeansNegative)
{
    if (value > threshold) return positiveMeansNegative ? -1 : 1;
    if (value < -threshold) return positiveMeansNegative ? 1 : -1;
    return 0;
}

static int8_t stepFromAxis(int value, bool positiveMeansNegative)
{
    return stepFromAxis(value, inputThreshold, positiveMeansNegative);
}

static bool dashDirectionFromInput(const InputState& input, bool inverted, int8_t& dx, int8_t& dy)
{
    dy = stepFromAxis(input.axisX, dashInputThreshold, true);
    dx = stepFromAxis(input.axisY, dashInputThreshold, false);
    if (inverted)
    {
        dx = -dx;
        dy = -dy;
    }
    if (dx == 0 && dy == 0)
        return false;
    return true;
}

static InputState emptyInput()
{
    InputState input = {0, 0, 0, 0, false, false, false, false};
    return input;
}

static InputState readJoystickInput(uint8_t playerIndex, Joystick& joystick)
{
    InputState input = emptyInput();
    int x = joystick.readXPercent();
    int y = joystick.readYPercent();
    input.axisX = x;
    input.axisY = y;

    input.dy = stepFromAxis(x, true);
    input.dx = stepFromAxis(y, false);

    bool pressed = joystick.isPressed();
    input.shoot = pressed && !lastFireButtonState[playerIndex];
    lastFireButtonState[playerIndex] = pressed;

    if (joystick.doppelklickZaehler > 0)
    {
        joystick.doppelklickZaehler--;
        input.dash = true;
    }

    if (joystick.langKlickZaehler > 0)
    {
        joystick.langKlickZaehler--;
        input.charge = true;
    }

    input.touched = input.dx != 0 || input.dy != 0 || pressed ||
                    input.shoot || input.dash || input.charge;
    return input;
}

static InputState readP2Input()
{
    uint8_t mode = p2InputMode;
    if (mode == p2InputEsp32)
        return readJoystickInput(1, joystick2);
    if (mode == p2InputI2c)
        return readJoystickInput(1, joystick3);

    InputState espInput = readJoystickInput(1, joystick2);
    if (espInput.touched)
        return espInput;
    return readJoystickInput(1, joystick3);
}

static bool allocatePlayerProjectile(uint8_t playerIndex, int8_t dy, uint8_t damage, bool charged)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return false;

    Spieler& p = spieler[playerIndex];
    for (uint8_t i = 0; i < projectileCount; i++)
    {
        if (!playerProjectiles[i].active)
        {
            playerProjectiles[i].x = p.x + 1;
            playerProjectiles[i].y = p.y;
            playerProjectiles[i].prevX = playerProjectiles[i].x;
            playerProjectiles[i].prevY = playerProjectiles[i].y;
            playerProjectiles[i].dx = 1;
            playerProjectiles[i].dy = dy;
            playerProjectiles[i].owner = playerIndex;
            playerProjectiles[i].damage = damage;
            playerProjectiles[i].charged = charged;
            playerProjectiles[i].active = true;
            return true;
        }
    }
    return false;
}

static void shoot(uint8_t playerIndex, bool charged)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return;

    Spieler& p = spieler[playerIndex];
    if (!p.active || p.hp <= 0)
        return;

    uint32_t now = millis();
    uint16_t interval = charged ? chargedShootIntervalMs : shootIntervalMs;
    if (now - p.shootCooldown < interval)
        return;

    bool fired = false;
    if (charged)
    {
        fired = allocatePlayerProjectile(playerIndex, 0, chargedShotDamage(playerIndex), true);
    }
    else if (secondsLeft(p.multiShotUntil) > 0)
    {
        uint16_t damage = baseShotDamage(playerIndex);
        fired = allocatePlayerProjectile(playerIndex, -1, damage, false);
        fired = allocatePlayerProjectile(playerIndex, 0, damage, false) || fired;
        fired = allocatePlayerProjectile(playerIndex, 1, damage, false) || fired;
    }
    else
    {
        fired = allocatePlayerProjectile(playerIndex, 0, baseShotDamage(playerIndex), false);
    }

    if (fired)
    {
        p.shootCooldown = now;
        Audio::playShot();
    }
}

static void dash(uint8_t playerIndex, int8_t dx, int8_t dy)
{
    Spieler& p = spieler[playerIndex];
    if (!p.active || p.hp <= 0)
        return;

    uint32_t now = millis();
    if (now - p.dashCooldown < dashCooldownMs)
        return;

    if (dx == 0 && dy == 0)
        return;

    p.x = constrain(p.x + dx * 4, 2, fieldWidth - 8);
    p.y = constrain(p.y + dy * 4, 1, fieldHeight - 2);
    p.dashCooldown = now;
    p.invulnerableUntil = now + dashInvulnerableMs;
    Audio::playBuff();
}

static void applyInput(uint8_t playerIndex, const InputState& input)
{
    if (playerIndex >= RAUMSCHIFF_MAX_PLAYERS)
        return;

    Spieler& p = spieler[playerIndex];
    if (input.touched && p.hp > 0)
        p.active = true;

    if (!p.active || p.hp <= 0)
        return;

    uint32_t now = millis();
    int8_t moveDx = input.dx;
    int8_t moveDy = input.dy;
    bool inverted = secondsLeft(p.invertUntil) > 0;
    if (inverted)
    {
        moveDx = -moveDx;
        moveDy = -moveDy;
    }

    if ((moveDx != 0 || moveDy != 0) && now - lastMoveMs[playerIndex] >= playerMoveIntervalMs)
    {
        p.x = constrain(p.x + moveDx, 2, fieldWidth - 8);
        p.y = constrain(p.y + moveDy, 1, fieldHeight - 2);
        lastMoveMs[playerIndex] = now;
    }

    if (input.dash)
    {
        int8_t dashDx = 0;
        int8_t dashDy = 0;
        if (dashDirectionFromInput(input, inverted, dashDx, dashDy))
            dash(playerIndex, dashDx, dashDy);
    }

    if (input.charge)
        shoot(playerIndex, true);

    if (input.shoot)
        shoot(playerIndex, false);
}

static uint16_t defaultHpFor(uint8_t size, uint8_t kind)
{
    if (kind == ENEMY_MINIBOSS)
        return 28 + (score / 14);
    if (kind == ENEMY_HEAVY || size >= 4)
        return 10 + (score / 20);
    if (size == 3)
        return 5 + (score / 28);
    if (size == 2)
        return 2 + (score > 110 ? 1 : 0);
    return 1 + (score > 180 ? 1 : 0);
}

static bool spawnAsteroidWith(uint8_t size, uint16_t hp, uint8_t speed, uint8_t kind)
{
    if (size < 1) size = 1;
    if (size > 5) size = 5;
    if (kind > ENEMY_MINIBOSS) kind = ENEMY_SMALL;
    if (kind == ENEMY_MINIBOSS && size < 3) size = 3;
    if (hp < 1)
        hp = defaultHpFor(size, kind);
    if (speed < 1) speed = 1;
    if (speed > 16) speed = 16;

    for (uint8_t i = 0; i < asteroidCount; i++)
    {
        if (!asteroiden[i].active)
        {
            asteroiden[i].size = size;
            asteroiden[i].x = fieldWidth - size;
            asteroiden[i].y = random(1, fieldHeight - size);
            asteroiden[i].prevX = asteroiden[i].x;
            asteroiden[i].prevY = asteroiden[i].y;
            asteroiden[i].hp = hp;
            asteroiden[i].maxHp = hp;
            asteroiden[i].speed = speed;
            asteroiden[i].kind = kind;
            asteroiden[i].active = true;
            return true;
        }
    }
    return false;
}

static void spawnBossWith(uint16_t hp, uint8_t size)
{
    if (hp < 1) hp = 250;
    if (size < 3) size = 3;
    if (size > 7) size = 7;

    boss.active = true;
    boss.x = fieldWidth - 4;
    boss.y = fieldHeight / 2;
    boss.hp = hp;
    boss.maxHp = hp;
    boss.phase = 1;
    boss.size = size;
    boss.attackTimer = millis();
    bossVolleyCounter = 0;
    gameState = BOSS;
    Audio::playBoss();
}

static bool spawnHazardWith(uint8_t type, uint8_t radius, uint8_t speed, uint16_t hp)
{
    if (type == HAZARD_NONE) type = HAZARD_BLACK_HOLE;
    if (radius < 1) radius = 1;
    if (radius > 7) radius = 7;
    if (speed < 1) speed = 1;
    if (speed > 12) speed = 12;
    if (hp < 1) hp = 80;

    for (uint8_t i = 0; i < hazardCount; i++)
    {
        if (!hazards[i].active)
        {
            hazards[i].x = fieldWidth - 2;
            hazards[i].y = random(radius + 1, fieldHeight - radius - 1);
            hazards[i].prevX = hazards[i].x;
            hazards[i].prevY = hazards[i].y;
            hazards[i].type = type;
            hazards[i].radius = radius;
            hazards[i].speed = speed;
            hazards[i].hp = hp;
            hazards[i].active = true;
            return true;
        }
    }
    return false;
}

static void spawnAsteroid()
{
    uint8_t requestedSize = 0;
    uint16_t requestedHp = 0;
    uint8_t requestedSpeed = 1;
    uint8_t requestedKind = ENEMY_SMALL;
    uint16_t requestedBossHp = 0;
    uint8_t requestedBossSize = 3;
    uint8_t requestedHazardType = HAZARD_NONE;
    uint8_t requestedHazardRadius = 2;
    uint8_t requestedHazardSpeed = 1;
    uint16_t requestedHazardHp = 80;
    uint8_t powerUpRequests = 0;
    portENTER_CRITICAL(&webInputMux);
    requestedSize = webSpawnRequestSize;
    requestedHp = webSpawnRequestHp;
    requestedSpeed = webSpawnRequestSpeed;
    requestedKind = webSpawnRequestKind;
    requestedBossHp = webBossRequestHp;
    requestedBossSize = webBossRequestSize;
    requestedHazardType = webHazardRequestType;
    requestedHazardRadius = webHazardRequestRadius;
    requestedHazardSpeed = webHazardRequestSpeed;
    requestedHazardHp = webHazardRequestHp;
    powerUpRequests = webPowerUpRequests;
    webSpawnRequestSize = 0;
    webSpawnRequestHp = 0;
    webSpawnRequestSpeed = 1;
    webSpawnRequestKind = ENEMY_SMALL;
    webBossRequestHp = 0;
    webHazardRequestType = HAZARD_NONE;
    webPowerUpRequests = 0;
    portEXIT_CRITICAL(&webInputMux);

    while (powerUpRequests > 0)
    {
        spawnPowerUpAt(fieldWidth - 2, random(2, fieldHeight - 2));
        powerUpRequests--;
    }

    if (requestedSize > 0)
    {
        if (spawnAsteroidWith(requestedSize, requestedHp, requestedSpeed, requestedKind))
            return;
    }

    if (requestedBossHp > 0)
        spawnBossWith(requestedBossHp, requestedBossSize);

    if (requestedHazardType != HAZARD_NONE)
        spawnHazardWith(requestedHazardType, requestedHazardRadius, requestedHazardSpeed, requestedHazardHp);

    bool webActive = webConnectedNow();
    if ((webActive && !directorAutoWhenWebConnected) || gameState == BOSS)
        return;

    uint8_t maxAsteroids = directorMaxActiveAsteroids;
    if (activeAsteroidCount() >= maxAsteroids)
        return;

    uint8_t living = activeLivingPlayers();
    if (living < 1) living = 1;
    uint16_t interval = directorSpawnIntervalMs;
    if (!webActive)
    {
        uint16_t pressure = (score > 60 ? 60 : score) * 8 + (living - 1) * 120;
        interval = interval > 900 + pressure ? interval - pressure : 900;
    }

    uint32_t now = millis();
    if (now - lastAsteroidSpawnMs < interval)
        return;

    uint8_t size = 1;
    uint8_t kind = ENEMY_SMALL;
    uint8_t roll = random(0, 100);
    if (score > 180 && roll > 97)
    {
        size = 3 + random(0, 2);
        kind = ENEMY_MINIBOSS;
    }
    else if (score > 70 && roll >= directorSmallAsteroidPercent + 45)
    {
        size = 3 + random(0, 2);
        kind = ENEMY_HEAVY;
    }
    else if (roll >= directorSmallAsteroidPercent)
    {
        size = 2;
        kind = ENEMY_MEDIUM;
    }

    uint32_t speedBoost = score / 70;
    if (speedBoost > 6) speedBoost = 6;
    uint8_t speed = 1 + speedBoost;
    uint32_t speedChance = score;
    if (speedChance > 80) speedChance = 80;
    if (random(0, 100) < speedChance)
        speed++;
    if (speed > 12) speed = 12;

    if (score > 55 &&
        activeHazardCount() == 0 &&
        now - lastAutoHazardSpawnMs > 6500 &&
        random(0, 100) < (webActive && directorAutoWhenWebConnected ? 12 : 6))
    {
        uint8_t radius = score > 220 ? 3 : 2;
        uint16_t hp = 35 + min((uint32_t)120, score / 3);
        uint8_t hazardSpeed = 1 + min((uint32_t)3, score / 120);
        if (spawnHazardWith(HAZARD_BLACK_HOLE, radius, hazardSpeed, hp))
            lastAutoHazardSpawnMs = now;
    }

    if (spawnAsteroidWith(size, 0, speed, kind))
        lastAsteroidSpawnMs = now;
}

static void updateProjectiles()
{
    uint32_t now = millis();
    if (now - lastProjectileMoveMs < projectileMoveIntervalMs)
        return;
    lastProjectileMoveMs = now;

    for (uint8_t i = 0; i < projectileCount; i++)
    {
        if (playerProjectiles[i].active)
        {
            playerProjectiles[i].prevX = playerProjectiles[i].x;
            playerProjectiles[i].prevY = playerProjectiles[i].y;
            playerProjectiles[i].x += playerProjectiles[i].dx;
            playerProjectiles[i].y += playerProjectiles[i].dy;

            if (playerProjectiles[i].x >= fieldWidth ||
                playerProjectiles[i].y < 0 ||
                playerProjectiles[i].y >= fieldHeight)
                playerProjectiles[i].active = false;
        }

        if (enemyProjectiles[i].active)
        {
            enemyProjectiles[i].prevX = enemyProjectiles[i].x;
            enemyProjectiles[i].prevY = enemyProjectiles[i].y;
            enemyProjectiles[i].x += enemyProjectiles[i].dx;
            enemyProjectiles[i].y += enemyProjectiles[i].dy;

            if (enemyProjectiles[i].x < 0 ||
                enemyProjectiles[i].y < 0 ||
                enemyProjectiles[i].y >= fieldHeight)
                enemyProjectiles[i].active = false;
        }
    }
}

static void updateAsteroids()
{
    uint32_t now = millis();
    uint16_t moveInterval = directorAsteroidMoveMs;
    if (secondsLeft(slowFieldUntil) > 0)
        moveInterval = min((uint16_t)600, (uint16_t)(moveInterval * 2));

    if (now - lastAsteroidMoveMs < moveInterval)
        return;
    lastAsteroidMoveMs = now;

    for (uint8_t i = 0; i < asteroidCount; i++)
    {
        if (!asteroiden[i].active)
            continue;

        asteroiden[i].prevX = asteroiden[i].x;
        asteroiden[i].prevY = asteroiden[i].y;
        asteroiden[i].x -= asteroiden[i].speed < 1 ? 1 : asteroiden[i].speed;
        if (asteroiden[i].x + asteroiden[i].size <= 0)
            asteroiden[i].active = false;
    }
}

static void updateHazards()
{
    uint32_t now = millis();
    uint16_t moveInterval = directorAsteroidMoveMs;
    if (now - lastHazardMoveMs < moveInterval)
        return;
    lastHazardMoveMs = now;

    for (uint8_t i = 0; i < hazardCount; i++)
    {
        if (!hazards[i].active)
            continue;

        hazards[i].prevX = hazards[i].x;
        hazards[i].prevY = hazards[i].y;
        hazards[i].x -= hazards[i].speed < 1 ? 1 : hazards[i].speed;
        if (hazards[i].x + hazards[i].radius < 0)
            hazards[i].active = false;
    }
}

static void updateLaserBeams()
{
    uint32_t now = millis();
    for (uint8_t i = 0; i < laserBeamCount; i++)
    {
        if (laserBeams[i].active &&
            (long)(laserBeams[i].activeUntil - now) <= 0)
            laserBeams[i].active = false;
    }
}

static void updatePowerUps()
{
    uint32_t now = millis();
    if (now - lastPowerUpMoveMs < powerUpMoveIntervalMs)
        return;
    lastPowerUpMoveMs = now;

    for (uint8_t i = 0; i < powerUpCount; i++)
    {
        if (!powerUps[i].active)
            continue;

        powerUps[i].prevX = powerUps[i].x;
        powerUps[i].prevY = powerUps[i].y;
        powerUps[i].x--;
        if (powerUps[i].x < 0)
            powerUps[i].active = false;
    }
}

static bool spawnEnemyProjectileAt(int x, int y, int dx, int dy, uint16_t damage)
{
    for (uint8_t i = 0; i < projectileCount; i++)
    {
        if (!enemyProjectiles[i].active)
        {
            enemyProjectiles[i].x = x;
            enemyProjectiles[i].y = constrain(y, 0, fieldHeight - 1);
            enemyProjectiles[i].prevX = enemyProjectiles[i].x;
            enemyProjectiles[i].prevY = enemyProjectiles[i].y;
            enemyProjectiles[i].dx = dx;
            enemyProjectiles[i].dy = dy;
            enemyProjectiles[i].owner = 255;
            enemyProjectiles[i].damage = damage;
            enemyProjectiles[i].charged = false;
            enemyProjectiles[i].active = true;
            return true;
        }
    }
    return false;
}

static bool spawnBossEscort(uint8_t kind, uint8_t size, uint16_t hp, uint8_t speed, int8_t yOffset)
{
    for (uint8_t i = 0; i < asteroidCount; i++)
    {
        if (!asteroiden[i].active)
        {
            asteroiden[i].size = size;
            asteroiden[i].x = constrain(boss.x - size - 2, 10, fieldWidth - size);
            asteroiden[i].y = constrain(boss.y + yOffset, 1, fieldHeight - size - 1);
            asteroiden[i].prevX = asteroiden[i].x;
            asteroiden[i].prevY = asteroiden[i].y;
            asteroiden[i].hp = hp;
            asteroiden[i].maxHp = hp;
            asteroiden[i].speed = speed;
            asteroiden[i].kind = kind;
            asteroiden[i].active = true;
            return true;
        }
    }
    return false;
}

static void bossLogic()
{
    if (!boss.active)
        return;

    uint32_t now = millis();
    uint8_t hpPercent = boss.maxHp > 0 ? (uint32_t)boss.hp * 100 / boss.maxHp : 100;
    boss.phase = 1;
    if (hpPercent <= 60)
        boss.phase = 2;
    if (hpPercent <= 28)
        boss.phase = 3;

    uint16_t interval = 1100;
    if (boss.phase == 2)
        interval = 950;
    else if (boss.phase >= 3)
        interval = 850;

    if (now - boss.attackTimer <= interval)
        return;

    bossVolleyCounter++;

    if (boss.phase == 1)
    {
        if ((bossVolleyCounter % 3) == 0)
        {
            spawnEnemyProjectileAt(boss.x - 2, boss.y - 1, -1, -1, 1);
            spawnEnemyProjectileAt(boss.x - 2, boss.y, -1, 0, 1);
            spawnEnemyProjectileAt(boss.x - 2, boss.y + 1, -1, 1, 1);
        }
        else
        {
            spawnEnemyProjectileAt(boss.x - 2, boss.y + random(-1, 2), -1, 0, 2);
        }
    }
    else if (boss.phase == 2)
    {
        spawnEnemyProjectileAt(boss.x - 2, boss.y - 2, -1, -1, 1);
        spawnEnemyProjectileAt(boss.x - 2, boss.y, -1, 0, 2);
        spawnEnemyProjectileAt(boss.x - 2, boss.y + 2, -1, 1, 1);

        if ((bossVolleyCounter % 3) == 0)
            spawnBossEscort(ENEMY_MEDIUM, 2, 4 + score / 45, 2 + min((uint32_t)2, score / 160), random(-2, 3));
    }
    else
    {
        spawnEnemyProjectileAt(boss.x - 2, boss.y - 3, -1, -2, 1);
        spawnEnemyProjectileAt(boss.x - 2, boss.y - 1, -1, -1, 2);
        spawnEnemyProjectileAt(boss.x - 2, boss.y, -1, 0, 3);
        spawnEnemyProjectileAt(boss.x - 2, boss.y + 1, -1, 1, 2);
        spawnEnemyProjectileAt(boss.x - 2, boss.y + 3, -1, 2, 1);

        if ((bossVolleyCounter % 2) == 0)
        {
            spawnBossEscort(ENEMY_SMALL, 1, 2 + score / 80, 3 + min((uint32_t)2, score / 180), -2);
            spawnBossEscort((bossVolleyCounter % 4) == 0 ? ENEMY_HEAVY : ENEMY_MEDIUM,
                            (bossVolleyCounter % 4) == 0 ? 3 : 2,
                            (bossVolleyCounter % 4) == 0 ? (8 + score / 30) : (4 + score / 50),
                            2 + min((uint32_t)3, score / 150),
                            2);
        }
    }

    if (boss.phase >= 3 && (bossVolleyCounter % 4) == 0)
    {
        boss.hp += min(8, boss.maxHp - boss.hp);
    }

    boss.y = constrain(boss.y + random(-(int)boss.phase, boss.phase + 1), 3, fieldHeight - 4);
    boss.attackTimer = now;
    Audio::playBoss();
}

static void drawShip(uint8_t index)
{
    Spieler& p = spieler[index];
    if (!p.active || p.hp <= 0)
        return;

    CRGB col = playerColor(index);
    CRGB dim = col;
    dim.nscale8(95);

    bool blink = (uint32_t)(millis() - p.invulnerableUntil) < dashInvulnerableMs &&
                 ((millis() / 70) % 2 == 0);

    CRGB low = col;
    low.nscale8(38);
    CRGB mid = col;
    mid.nscale8(105);

    setPixel(p.x, p.y, blink ? CRGB::White : col);
    setPixel(p.x - 1, p.y, p.hp >= 2 ? dim : low);
    setPixel(p.x - 1, p.y - 1, p.hp >= 3 ? dim : low);
    setPixel(p.x - 1, p.y + 1, p.hp >= 1 ? mid : low);

    if ((millis() / 90 + index) % 2 == 0)
        setPixel(p.x - 2, p.y, CRGB::OrangeRed);
}

static void drawAsteroid(const Asteroid& a)
{
    CRGB base = CRGB::Orange;
    if (a.kind == ENEMY_MEDIUM) base = CRGB::OrangeRed;
    else if (a.kind == ENEMY_HEAVY) base = CRGB::Red;
    else if (a.kind == ENEMY_MINIBOSS) base = CRGB::DeepPink;

    for (uint8_t dx = 0; dx < a.size; dx++)
        for (uint8_t dy = 0; dy < a.size; dy++)
            setPixel(a.x + dx, a.y + dy, base);

    setPixel(a.x, a.y, a.kind == ENEMY_MINIBOSS ? CRGB::White : CRGB::Gold);
}

static void drawPowerUp(const PowerUp& p)
{
    CRGB color = ((millis() / 120) % 2 == 0) ? CRGB(255, 20, 147) : CRGB::White;
    setPixel(p.x, p.y, color);
    setPixel(p.x - 1, p.y, CRGB(120, 20, 90));
}

static void drawBoss()
{
    if (!boss.active)
        return;

    int8_t half = boss.size / 2;
    CRGB edge = boss.phase >= 3 ? CRGB::Red : (boss.phase == 2 ? CRGB::OrangeRed : CRGB::Magenta);
    CRGB core = boss.phase >= 3 && ((millis() / 120) % 2 == 0) ? CRGB::White : CRGB::Purple;

    for (int8_t dx = -half; dx <= half; dx++)
    {
        for (int8_t dy = -half; dy <= half; dy++)
        {
            bool border = abs(dx) == half || abs(dy) == half;
            CRGB col = border ? edge : core;
            if (dx == 0 && dy == 0)
                col = CRGB::White;
            setPixel(boss.x + dx, boss.y + dy, col);
        }
    }

    setPixel(boss.x - half - 1, boss.y, edge);
    setPixel(boss.x + half + 1, boss.y, edge);
}

static void drawHazard(const Hazard& h)
{
    if (h.type != HAZARD_BLACK_HOLE)
        return;

    CRGB outer = ((millis() / 120) % 2 == 0) ? CRGB::Aqua : CRGB::DarkViolet;
    CRGB core = CRGB::White;
    setPixel(h.x, h.y, core);
    for (int8_t d = -h.radius; d <= (int8_t)h.radius; d++)
    {
        if (d == 0) continue;
        setPixel(h.x + d, h.y, outer);
        setPixel(h.x, h.y + d, outer);
    }
}

static void drawLaser(const LaserBeam& beam)
{
    CRGB color = ((millis() / 40) % 2 == 0) ? CRGB::White : CRGB::Yellow;
    for (uint8_t x = 0; x < fieldWidth; x++)
        setPixel(x, beam.y, color);
}

static void drawPlayerHealthDots()
{
    uint8_t slot = 0;
    for (uint8_t p = 0; p < RAUMSCHIFF_MAX_PLAYERS; p++)
    {
        if (!spieler[p].active)
            continue;
        CRGB col = playerColor(p);
        CRGB dim = col;
        dim.nscale8(45);
        uint8_t y = fieldHeight - 1 - slot;
        for (uint8_t hp = 0; hp < 3; hp++)
            setPixel(hp, y, spieler[p].hp > hp ? col : dim);
        slot++;
    }
}

static void drawBossHpBar()
{
    if (!boss.active || boss.maxHp <= 0)
        return;

    uint8_t barWidth = 24;
    uint8_t filled = (uint32_t)boss.hp * barWidth / boss.maxHp;
    if (filled > barWidth) filled = barWidth;
    for (uint8_t i = 0; i < barWidth; i++)
    {
        CRGB col = i < filled ? CRGB::Magenta : CRGB(24, 0, 24);
        setPixel(8 + i, 0, col);
    }
}

static void render()
{
    FastLED.clear();

    for (uint8_t i = 0; i < RAUMSCHIFF_MAX_PLAYERS; i++)
        drawShip(i);

    for (uint8_t i = 0; i < projectileCount; i++)
    {
        if (playerProjectiles[i].active)
        {
            CRGB col = playerProjectiles[i].charged ? CRGB::White : CRGB::Yellow;
            setPixel(playerProjectiles[i].x, playerProjectiles[i].y, col);
            if (playerProjectiles[i].charged)
                setPixel(playerProjectiles[i].x - 1, playerProjectiles[i].y, playerColor(playerProjectiles[i].owner));
        }

        if (enemyProjectiles[i].active)
            setPixel(enemyProjectiles[i].x, enemyProjectiles[i].y, CRGB::Red);
    }

    for (uint8_t i = 0; i < laserBeamCount; i++)
        if (laserBeams[i].active)
            drawLaser(laserBeams[i]);

    for (uint8_t i = 0; i < asteroidCount; i++)
        if (asteroiden[i].active)
            drawAsteroid(asteroiden[i]);

    for (uint8_t i = 0; i < hazardCount; i++)
        if (hazards[i].active)
            drawHazard(hazards[i]);

    for (uint8_t i = 0; i < powerUpCount; i++)
        if (powerUps[i].active)
            drawPowerUp(powerUps[i]);

    drawBoss();
    drawBossHpBar();
    drawPlayerHealthDots();

    FastLED.show();
}

static void renderMenu()
{
    FastLED.clear();
    uint32_t now = millis();

    for (uint8_t x = 0; x < fieldWidth; x++)
    {
        setPixel(x, 0, CHSV(x * 8 + now / 20, 220, 80));
        setPixel(x, fieldHeight - 1, CHSV(160 + x * 5 + now / 25, 220, 70));
    }

    for (uint8_t i = 0; i < RAUMSCHIFF_MAX_PLAYERS; i++)
    {
        spieler[i].active = (i == 0) || spieler[i].active;
        drawShip(i);
    }

    setPixel(18, 6 + ((now / 240) % 4), CRGB::Orange);
    setPixel(22, 8 + ((now / 180) % 3), CRGB::OrangeRed);
    setPixel(27, 7 + ((now / 300) % 5), CRGB::Red);

    FastLED.show();
}

static void renderGameOver()
{
    uint32_t now = millis();
    if (now - lastGameOverRenderMs < 140)
        return;
    lastGameOverRenderMs = now;

    FastLED.clear();
    CRGB col = ((now / 280) % 2 == 0) ? CRGB::Red : CRGB::OrangeRed;
    drawChar3x5(5, 3, 'G', col);
    drawChar3x5(11, 3, 'O', col);
    uint32_t shownScore = score;
    if (shownScore > 9999) shownScore = 9999;
    uint8_t thousands = shownScore / 1000;
    uint8_t hundreds = (shownScore / 100) % 10;
    uint8_t tens = (shownScore / 10) % 10;
    uint8_t ones = shownScore % 10;
    CRGB scoreCol = CRGB::White;
    if (thousands > 0) drawDigitW(5, 10, thousands, scoreCol);
    if (shownScore >= 100) drawDigitW(10, 10, hundreds, scoreCol);
    if (shownScore >= 10) drawDigitW(15, 10, tens, scoreCol);
    drawDigitW(20, 10, ones, scoreCol);
    FastLED.show();
}

static void updatePlaying()
{
    updateWebEnergy();

    InputState p1 = readJoystickInput(0, joystick1);
    InputState p2 = readP2Input();

    applyInput(0, p1);
    applyInput(1, p2);

    spawnAsteroid();
    updateProjectiles();
    updateAsteroids();
    updateHazards();
    updatePowerUps();
    updateLaserBeams();
    collisionUpdate();

    if (!anyLivingPlayer())
        raumschiffGameOver();

    render();
}

void raumschiffGameTick()
{
    if (gameState == MENU)
    {
        InputState p1 = readJoystickInput(0, joystick1);
        InputState p2 = readP2Input();

        if (p2.touched)
            spieler[1].active = true;
        if (p1.charge)
            spieler[0].ready = true;
        if (p2.charge)
            spieler[1].ready = true;

        if (p1.dash)
        {
            Audio::playUI();
            matchStartMs = millis();
            lastWebEnergyRegenMs = matchStartMs;
            gameState = PLAYING;
        }

        renderMenu();
        return;
    }

    if (gameState == PLAYING)
    {
        updatePlaying();

        if (!boss.active && score >= nextBossScore)
        {
            uint32_t scoreBoost = score / 2;
            if (scoreBoost > 1400) scoreBoost = 1400;
            uint16_t hp = 260 + activeLivingPlayers() * 110 + scoreBoost + bossesSpawned * 55;
            uint32_t sizeBoost = score / 250;
            if (sizeBoost > 4) sizeBoost = 4;
            uint8_t size = 3 + sizeBoost;
            spawnBossWith(hp, size);
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
        renderGameOver();
        return;
    }
}

void raumschiffResetGame()
{
    gameState = MENU;

    resetPlayer(0, true);
    resetPlayer(1, false);

    score = 0;
    lastProjectileMoveMs = 0;
    lastAsteroidMoveMs = 0;
    lastHazardMoveMs = 0;
    lastPowerUpMoveMs = 0;
    lastAsteroidSpawnMs = 0;
    lastAutoHazardSpawnMs = 0;
    lastGameOverRenderMs = 0;
    nextBossScore = bossScoreGapBase;
    matchStartMs = 0;
    lastWebEnergyRegenMs = 0;
    slowFieldUntil = 0;
    bossesSpawned = 0;
    bossVolleyCounter = 0;
    highScoreSaved = false;

    portENTER_CRITICAL(&webInputMux);
    webSpawnRequestSize = 0;
    webSpawnRequestHp = 0;
    webSpawnRequestSpeed = 1;
    webSpawnRequestKind = ENEMY_SMALL;
    webBossRequestHp = 0;
    webBossRequestSize = 3;
    webHazardRequestType = HAZARD_NONE;
    webHazardRequestRadius = 2;
    webHazardRequestSpeed = 1;
    webHazardRequestHp = 80;
    webPowerUpRequests = 0;
    webEnergy = webEnergyStart;
    portEXIT_CRITICAL(&webInputMux);

    for (uint8_t i = 0; i < RAUMSCHIFF_MAX_PLAYERS; i++)
    {
        lastMoveMs[i] = 0;
        lastFireButtonState[i] = false;
    }
    lastFireButtonState[0] = joystick1.isPressed();
    lastFireButtonState[1] = false;

    joystick1.reset();
    joystick2.reset();
    joystick3.reset();

    boss.active = false;
    boss.x = 28;
    boss.y = 8;
    boss.hp = 0;
    boss.maxHp = 0;
    boss.phase = 0;
    boss.size = 3;
    boss.attackTimer = 0;

    for (uint8_t i = 0; i < projectileCount; i++)
    {
        playerProjectiles[i].active = false;
        enemyProjectiles[i].active = false;
        playerProjectiles[i].x = 0;
        playerProjectiles[i].y = 0;
        playerProjectiles[i].prevX = 0;
        playerProjectiles[i].prevY = 0;
        enemyProjectiles[i].x = 0;
        enemyProjectiles[i].y = 0;
        enemyProjectiles[i].prevX = 0;
        enemyProjectiles[i].prevY = 0;
    }

    for (uint8_t i = 0; i < asteroidCount; i++)
    {
        asteroiden[i].active = false;
        asteroiden[i].x = 0;
        asteroiden[i].y = 0;
        asteroiden[i].prevX = 0;
        asteroiden[i].prevY = 0;
        asteroiden[i].hp = 0;
        asteroiden[i].maxHp = 0;
        asteroiden[i].size = 1;
        asteroiden[i].speed = 1;
        asteroiden[i].kind = ENEMY_SMALL;
    }

    for (uint8_t i = 0; i < powerUpCount; i++)
    {
        powerUps[i].active = false;
        powerUps[i].x = 0;
        powerUps[i].y = 0;
        powerUps[i].prevX = 0;
        powerUps[i].prevY = 0;
        powerUps[i].effect = POWERUP_NONE;
    }

    for (uint8_t i = 0; i < laserBeamCount; i++)
    {
        laserBeams[i].active = false;
        laserBeams[i].owner = 0;
        laserBeams[i].y = 0;
        laserBeams[i].damage = 0;
        laserBeams[i].activeUntil = 0;
        laserBeams[i].damageApplied = false;
    }

    for (uint8_t i = 0; i < hazardCount; i++)
    {
        hazards[i].active = false;
        hazards[i].x = 0;
        hazards[i].y = 0;
        hazards[i].prevX = 0;
        hazards[i].prevY = 0;
        hazards[i].type = HAZARD_NONE;
        hazards[i].radius = 1;
        hazards[i].speed = 1;
        hazards[i].hp = 0;
    }
}
