#ifndef RAUMSCHIFF_WEB_H
#define RAUMSCHIFF_WEB_H

#include <Arduino.h>
#include "RaumschiffGame.h"

struct RaumschiffPlayerStatus {
    char name[16];
    char shortName[4];
    bool active;
    bool alive;
    int hp;
    uint32_t score;
    uint16_t kills;
    uint8_t level;
    uint8_t shield;
    uint8_t multiSeconds;
    uint8_t boostSeconds;
    uint8_t invertSeconds;
    uint16_t baseDamage;
    uint16_t damage;
    uint16_t chargedDamage;
    uint32_t nextDamageScore;
    int x;
    int y;
};

struct RaumschiffStatusSnapshot {
    UnifiedGameState state;
    uint32_t score;
    bool webConnected;
    uint16_t webEnergy;
    uint16_t webEnergyMax;
    uint8_t p2InputMode;
    Boss boss;
    uint16_t directorSpawnMs;
    uint16_t directorMoveMs;
    uint8_t directorMaxAsteroids;
    uint8_t directorSmallPercent;
    uint8_t directorPowerChance;
    bool directorAutoWeb;
    uint8_t activeAsteroids;
    uint8_t activePowerUps;
    uint8_t activeHazards;
    uint8_t slowSeconds;
    char lastMatchName[36];
    uint32_t lastMatchScore;
    uint32_t lastMatchP1Score;
    uint32_t lastMatchP2Score;
    uint16_t lastMatchP1Kills;
    uint16_t lastMatchP2Kills;
    uint16_t lastMatchBosses;
    uint32_t lastMatchDurationSec;
    RaumschiffPlayerStatus players[RAUMSCHIFF_MAX_PLAYERS];
};

void raumschiffBuildStatusSnapshot(RaumschiffStatusSnapshot& snapshot);
String formatRaumschiffStateJson(const RaumschiffStatusSnapshot& snapshot);

#endif
