#include "TaskMario.h"

#include <Arduino.h>
#include <FastLED.h>

#include "Config.h"
#include "HardwareUtils.h"
#include "Joystick.h"
#include "SoundUtils.h"
#include "GameRuntime.h"
#include "RuntimeHealth.h"

extern volatile int aktiverTask;
extern int fokusModus;
extern Joystick joystick1;
extern Joystick joystick3;

uint8_t marioLives = 3;

namespace {
const int SCREEN_W = 32;
const int SCREEN_H = 16;
const int SCALE = 16;
const int WORLD_W = 176;
const int FLAG_X = 168;
const int INPUT_DEADZONE = 58;
const uint32_t TICK_MS = 33;
const uint32_t END_SCREEN_MS = 3500;
const uint8_t MAX_SEGMENTS = 26;
const uint8_t MAX_BLOCKS = 20;
const uint8_t MAX_COINS = 48;
const uint8_t MAX_ENEMY_SPAWNS = 12;

const int16_t WALK_VX = 12;
const int16_t DUCK_VX = 7;
const int16_t JUMP_VY = -24;
const int16_t STOMP_VY = -14;
const int16_t GRAVITY_HOLD = 1;
const int16_t GRAVITY_RELEASE = 5;
const int16_t GRAVITY_FALL = 4;
const int16_t ENEMY_GRAVITY = 3;
const int16_t MAX_FALL_VY = 28;
const uint8_t MAX_JUMP_HOLD_TICKS = 8;
const uint8_t COYOTE_TICKS = 3;
const uint8_t JUMP_BUFFER_TICKS = 3;

enum MarioPower : uint8_t { MARIO_SMALL, MARIO_SUPER, MARIO_FIRE };
enum SegmentType : uint8_t { SEG_GROUND, SEG_PLATFORM };
enum BlockType : uint8_t { BLOCK_BRICK, BLOCK_QUESTION };
enum EnemyType : uint8_t { ENEMY_GOOMBA, ENEMY_KOOPA, ENEMY_CROW };
enum ItemType : uint8_t { ITEM_MUSHROOM, ITEM_FLOWER };

struct Segment {
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t type;
};

struct BlockDef {
    uint8_t x;
    uint8_t y;
    uint8_t type;
};

struct EnemySpawn {
    uint8_t x;
    uint8_t y;
    uint8_t type;
};

struct Player {
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t power;
    uint8_t jumpHoldTicks;
    uint8_t coyoteTicks;
    uint8_t jumpBufferTicks;
    bool onGround;
    bool ducking;
    uint32_t invulnerableUntil;
};

struct Enemy {
    bool active;
    uint8_t type;
    uint8_t state;
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t fixedY;
};

struct Item {
    bool active;
    uint8_t type;
    int16_t x;
    int16_t y;
};

struct Fireball {
    bool active;
    int16_t x;
    int16_t y;
    int16_t vx;
};

struct InputState {
    int8_t move;
    bool jumpHeld;
    bool jumpPressed;
    bool duck;
    bool action;
};

const uint8_t checkpointMarks[] = {2, 42, 72, 104, 134};

Segment levelSegments[MAX_SEGMENTS];
BlockDef levelBlocks[MAX_BLOCKS];
uint16_t levelCoins[MAX_COINS];
EnemySpawn enemySpawns[MAX_ENEMY_SPAWNS];
uint8_t segmentCount = 0;
uint8_t blockCount = 0;
uint8_t coinCount = 0;
uint8_t enemySpawnCount = 0;

bool blockAlive[MAX_BLOCKS];
bool coinCollected[MAX_COINS];
Enemy enemies[8];
Item items[4];
Fireball fireballs[4];

UnifiedGameState mode = GAME_MENU;
GameRuntime runtime;
bool levelWon = false;
Player mario;
uint16_t marioScore = 0;
uint8_t marioCoins = 0;
uint8_t marioLevel = 1;
int16_t cameraX = 0;
uint8_t checkpointIndex = 0;
uint32_t lastTickMs = 0;
uint32_t modeStartedMs = 0;
bool lastUp1 = false;
bool lastUp3 = false;
bool humanControlled = false;
bool humanInputArmed = false;
uint32_t lastBotActionMs = 0;
volatile bool restartRequested = false;

int pixelX(int16_t fixedValue) { return fixedValue / SCALE; }
int pixelY(int16_t fixedValue) { return fixedValue / SCALE; }
uint8_t packedX(uint16_t packed) { return (uint8_t)(packed >> 4); }
uint8_t packedY(uint16_t packed) { return (uint8_t)(packed & 0x0F); }

uint8_t heightFor(uint8_t power, bool ducking) {
    if (power == MARIO_SMALL) return 2;
    return ducking ? 2 : 3;
}

uint8_t playerHeight() {
    return heightFor(mario.power, mario.ducking);
}

bool inWorldY(int y) {
    return y >= 0 && y < SCREEN_H;
}

uint8_t difficultyCap() {
    return marioLevel > 8 ? 8 : marioLevel;
}

bool protectedGroundX(int x) {
    if (x < 14 || x > FLAG_X + 3) return true;
    if (x >= FLAG_X - 5) return true;
    for (size_t i = 0; i < sizeof(checkpointMarks) / sizeof(checkpointMarks[0]); i++) {
        if (abs(x - checkpointMarks[i]) <= 3) return true;
    }
    return false;
}

void addSegment(uint8_t x, uint8_t y, uint8_t w, uint8_t type) {
    if (segmentCount >= MAX_SEGMENTS || w == 0) return;
    levelSegments[segmentCount++] = {x, y, w, type};
}

void addBlock(uint8_t x, uint8_t y, uint8_t type) {
    if (blockCount >= MAX_BLOCKS || x >= WORLD_W || y >= SCREEN_H) return;
    for (uint8_t i = 0; i < blockCount; i++) {
        if (levelBlocks[i].x == x && levelBlocks[i].y == y) return;
    }
    levelBlocks[blockCount] = {x, y, type};
    blockAlive[blockCount] = true;
    blockCount++;
}

void addCoin(uint8_t x, uint8_t y) {
    if (coinCount >= MAX_COINS || x >= WORLD_W || y >= SCREEN_H) return;
    for (uint8_t i = 0; i < coinCount; i++) {
        if (packedX(levelCoins[i]) == x && packedY(levelCoins[i]) == y) return;
    }
    levelCoins[coinCount] = ((uint16_t)x << 4) | (uint16_t)(y & 0x0F);
    coinCollected[coinCount] = false;
    coinCount++;
}

void addEnemySpawn(uint8_t x, uint8_t y, uint8_t type) {
    if (enemySpawnCount >= MAX_ENEMY_SPAWNS || x >= WORLD_W || y >= SCREEN_H) return;
    enemySpawns[enemySpawnCount++] = {x, y, type};
}

void forceGroundSafeZones(bool ground[]) {
    for (int x = 0; x < WORLD_W; x++) {
        if (protectedGroundX(x)) ground[x] = true;
    }
}

void buildGroundSegments(bool ground[]) {
    int x = 0;
    while (x < WORLD_W) {
        while (x < WORLD_W && !ground[x]) x++;
        if (x >= WORLD_W) break;
        int start = x;
        while (x < WORLD_W && ground[x]) x++;
        int w = x - start;
        while (w > 0) {
            uint8_t chunk = (uint8_t)min(w, 255);
            addSegment((uint8_t)start, 15, chunk, SEG_GROUND);
            start += chunk;
            w -= chunk;
        }
    }
}

bool hasGroundBelow(bool ground[], int x) {
    return x >= 0 && x < WORLD_W && ground[x];
}

void generateLevel() {
    randomSeed((uint32_t)micros() ^ ((uint32_t)millis() << 7) ^ ((uint32_t)marioLevel * 2654435761UL));

    segmentCount = 0;
    blockCount = 0;
    coinCount = 0;
    enemySpawnCount = 0;
    for (uint8_t i = 0; i < MAX_BLOCKS; i++) blockAlive[i] = false;
    for (uint8_t i = 0; i < MAX_COINS; i++) coinCollected[i] = false;

    bool ground[WORLD_W];
    for (int x = 0; x < WORLD_W; x++) ground[x] = true;

    uint8_t diff = difficultyCap();
    uint8_t targetGaps = 3 + diff / 2;
    uint8_t maxGap = 3 + min((int)diff / 3, 2);
    uint8_t carved = 0;

    for (uint8_t attempt = 0; attempt < 70 && carved < targetGaps; attempt++) {
        int gapW = random(2, maxGap + 2);
        int sx = random(22, FLAG_X - 12);
        bool ok = true;
        for (int x = sx - 2; x < sx + gapW + 2; x++) {
            if (x <= 0 || x >= WORLD_W || protectedGroundX(x) || !ground[x]) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        for (int x = sx; x < sx + gapW; x++) ground[x] = false;
        carved++;
    }
    forceGroundSafeZones(ground);
    buildGroundSegments(ground);

    uint8_t targetPlatforms = 6 + min((int)diff, 6);
    uint8_t platforms = 0;
    for (uint8_t attempt = 0; attempt < 90 && platforms < targetPlatforms; attempt++) {
        uint8_t w = (uint8_t)random(4, 9);
        uint8_t x = (uint8_t)random(13, FLAG_X - w - 6);
        uint8_t y = (uint8_t)random(8, 13);
        bool ok = true;
        for (uint8_t i = 0; i < segmentCount; i++) {
            const Segment& s = levelSegments[i];
            if (s.type != SEG_PLATFORM || s.y != y) continue;
            if (x < s.x + s.w + 3 && x + w + 3 > s.x) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        addSegment(x, y, w, SEG_PLATFORM);
        platforms++;

        uint8_t coinY = y > 1 ? y - 1 : y;
        for (uint8_t c = 1; c + 1 < w && c < 5; c += 2) addCoin(x + c, coinY);
    }

    uint8_t targetBlocks = 7 + min((int)diff * 2, 10);
    uint8_t blocks = 0;
    for (uint8_t attempt = 0; attempt < 80 && blocks < targetBlocks; attempt++) {
        uint8_t x = (uint8_t)random(16, FLAG_X - 14);
        uint8_t y = (uint8_t)random(7, 11);
        if (protectedGroundX(x)) continue;
        uint8_t type = ((blocks + marioLevel) % 4 == 0) ? BLOCK_QUESTION : BLOCK_BRICK;
        addBlock(x, y, type);
        blocks++;
    }

    uint8_t groundCoins = 14 + min((int)diff * 3, 18);
    for (uint8_t attempt = 0; attempt < 100 && coinCount < MAX_COINS && groundCoins > 0; attempt++) {
        uint8_t x = (uint8_t)random(8, FLAG_X - 6);
        if (!hasGroundBelow(ground, x) || protectedGroundX(x)) continue;
        addCoin(x, 13);
        groundCoins--;
    }

    uint8_t targetEnemies = 4 + min((int)diff, 6);
    for (uint8_t attempt = 0; attempt < 90 && targetEnemies > 0; attempt++) {
        uint8_t x = (uint8_t)random(24, FLAG_X - 10);
        if (!hasGroundBelow(ground, x) || protectedGroundX(x)) continue;
        uint8_t type = (random(0, 100) < 35 + diff * 5) ? ENEMY_KOOPA : ENEMY_GOOMBA;
        addEnemySpawn(x, 14, type);
        targetEnemies--;
    }

    uint8_t crows = 1 + diff / 3;
    for (uint8_t i = 0; i < crows && enemySpawnCount < MAX_ENEMY_SPAWNS; i++) {
        uint8_t x = (uint8_t)random(70, FLAG_X - 5);
        uint8_t y = (uint8_t)random(5, 10);
        addEnemySpawn(x, y, ENEMY_CROW);
    }

    addBlock(18, 10, BLOCK_QUESTION);
    addBlock(19, 10, BLOCK_BRICK);
    addCoin(14, 10);
    addCoin(15, 10);
}

bool segmentSolidAt(int x, int y) {
    for (uint8_t i = 0; i < segmentCount; i++) {
        const Segment& s = levelSegments[i];
        if (y == s.y && x >= s.x && x < s.x + s.w) return true;
    }
    return false;
}

int blockIndexAt(int x, int y) {
    for (uint8_t i = 0; i < blockCount; i++) {
        if (blockAlive[i] && levelBlocks[i].x == x && levelBlocks[i].y == y) return (int)i;
    }
    return -1;
}

bool solidAt(int x, int y) {
    if (x < 0 || x >= WORLD_W || y < 0) return true;
    if (y >= SCREEN_H) return false;
    if (segmentSolidAt(x, y)) return true;
    return blockIndexAt(x, y) >= 0;
}

bool bboxSolidAt(int16_t fx, int16_t fy, uint8_t h) {
    int x = pixelX(fx);
    int top = pixelY(fy);
    for (uint8_t dy = 0; dy < h; dy++) {
        if (solidAt(x, top + dy)) return true;
    }
    return false;
}

bool fitsAt(int16_t fx, int16_t fy, uint8_t h) {
    return !bboxSolidAt(fx, fy, h);
}

bool resolvePlayerEmbedding() {
    uint8_t h = playerHeight();
    if (fitsAt(mario.x, mario.y, h)) return true;

    static const int8_t xOffsets[] = {0, -1, 1, -2, 2};
    for (uint8_t up = 0; up <= 5; up++) {
        for (uint8_t i = 0; i < sizeof(xOffsets) / sizeof(xOffsets[0]); i++) {
            int16_t nx = mario.x + xOffsets[i] * SCALE;
            int16_t ny = mario.y - up * SCALE;
            if (!fitsAt(nx, ny, h)) continue;
            mario.x = nx;
            mario.y = ny;
            mario.vx = 0;
            mario.vy = 0;
            return true;
        }
    }
    return false;
}

void setPowerKeepingFeet(uint8_t newPower) {
    uint8_t oldH = playerHeight();
    int16_t feet = mario.y + oldH * SCALE - 1;

    uint8_t oldPower = mario.power;
    bool oldDucking = mario.ducking;
    mario.power = newPower;
    if (mario.power == MARIO_SMALL) mario.ducking = false;

    uint8_t newH = playerHeight();
    int16_t newY = feet - newH * SCALE + 1;
    if (fitsAt(mario.x, newY, newH)) {
        mario.y = newY;
        return;
    }

    for (uint8_t lift = 1; lift <= 3; lift++) {
        int16_t liftedY = newY - lift * SCALE;
        if (!fitsAt(mario.x, liftedY, newH)) continue;
        mario.y = liftedY;
        return;
    }

    mario.power = oldPower;
    mario.ducking = oldDucking;
    resolvePlayerEmbedding();
}

void setDuckingKeepingFeet(bool wantsDuck) {
    if (mario.power == MARIO_SMALL) {
        mario.ducking = false;
        return;
    }
    if (mario.ducking == wantsDuck) return;

    uint8_t oldH = playerHeight();
    int16_t feet = mario.y + oldH * SCALE - 1;
    bool oldDucking = mario.ducking;
    mario.ducking = wantsDuck;
    uint8_t newH = playerHeight();
    int16_t newY = feet - newH * SCALE + 1;

    if (fitsAt(mario.x, newY, newH)) {
        mario.y = newY;
        return;
    }

    mario.ducking = oldDucking;
}

void spawnItem(uint8_t type, int x, int y) {
    for (uint8_t i = 0; i < 4; i++) {
        if (items[i].active) continue;
        items[i].active = true;
        items[i].type = type;
        items[i].x = x * SCALE;
        items[i].y = y * SCALE;
        return;
    }
}

void hitBlockAbove() {
    int x = pixelX(mario.x);
    int y = pixelY(mario.y) - 1;
    int idx = blockIndexAt(x, y);
    if (idx < 0) return;

    const BlockDef& b = levelBlocks[idx];
    if (b.type == BLOCK_BRICK && mario.power != MARIO_SMALL) {
        blockAlive[idx] = false;
        marioScore += 25;
        playSound(SND_HIT);
    } else if (b.type == BLOCK_QUESTION) {
        blockAlive[idx] = false;
        spawnItem(mario.power == MARIO_SMALL ? ITEM_MUSHROOM : ITEM_FLOWER, b.x, b.y - 1);
        marioScore += 10;
        playSound(SND_UI);
    } else {
        playSound(SND_HIT);
    }
}

void movePlayerX(int16_t delta) {
    int8_t dir = delta > 0 ? 1 : -1;
    int16_t left = abs(delta);
    while (left-- > 0) {
        int16_t nextX = mario.x + dir;
        if (!bboxSolidAt(nextX, mario.y, playerHeight())) {
            mario.x = nextX;
        } else {
            mario.vx = 0;
            break;
        }
    }
}

void movePlayerY(int16_t delta) {
    mario.onGround = false;
    int8_t dir = delta > 0 ? 1 : -1;
    int16_t left = abs(delta);
    while (left-- > 0) {
        int16_t nextY = mario.y + dir;
        if (!bboxSolidAt(mario.x, nextY, playerHeight())) {
            mario.y = nextY;
        } else {
            if (dir > 0) mario.onGround = true;
            else hitBlockAbove();
            mario.vy = 0;
            break;
        }
    }
}

int16_t initialEnemySpeed(uint8_t type) {
    uint8_t diff = difficultyCap();
    if (type == ENEMY_CROW) return -(8 + diff);
    return -(5 + diff / 2);
}

void initEnemies() {
    for (uint8_t i = 0; i < 8; i++) enemies[i].active = false;
    for (uint8_t i = 0; i < enemySpawnCount && i < 8; i++) {
        const EnemySpawn& s = enemySpawns[i];
        enemies[i].active = true;
        enemies[i].type = s.type;
        enemies[i].state = 0;
        enemies[i].x = s.x * SCALE;
        enemies[i].y = s.y * SCALE;
        enemies[i].vx = initialEnemySpeed(s.type);
        enemies[i].vy = 0;
        enemies[i].fixedY = s.y;
    }
}

uint8_t checkpointX() {
    return checkpointMarks[checkpointIndex];
}

int16_t safeSpawnY(uint8_t x, uint8_t h) {
    int16_t fx = x * SCALE;
    for (int y = 0; y < SCREEN_H - h; y++) {
        if (!fitsAt(fx, y * SCALE, h)) continue;
        if (solidAt(x, y + h)) return y * SCALE;
    }
    return 12 * SCALE;
}

void clearTransientObjects() {
    for (uint8_t i = 0; i < 4; i++) {
        items[i].active = false;
        fireballs[i].active = false;
    }
}

void placeMarioAtCheckpoint() {
    mario.x = checkpointX() * SCALE;
    mario.y = safeSpawnY(checkpointX(), heightFor(MARIO_SMALL, false));
    mario.vx = 0;
    mario.vy = 0;
    mario.power = MARIO_SMALL;
    mario.jumpHoldTicks = 0;
    mario.coyoteTicks = 0;
    mario.jumpBufferTicks = 0;
    mario.onGround = false;
    mario.ducking = false;
    mario.invulnerableUntil = millis() + 1300UL;
    int target = checkpointX() - 8;
    if (target < 0) target = 0;
    if (target > WORLD_W - SCREEN_W) target = WORLD_W - SCREEN_W;
    cameraX = target;
    resolvePlayerEmbedding();
}

void resetLevel(bool fullReset) {
    if (fullReset) {
        marioLevel = 1;
        marioLives = 3;
        marioScore = 0;
        marioCoins = 0;
        checkpointIndex = 0;
        generateLevel();
    }
    for (uint8_t i = 0; i < blockCount; i++) blockAlive[i] = true;
    for (uint8_t i = 0; i < coinCount; i++) coinCollected[i] = false;
    clearTransientObjects();
    initEnemies();
    placeMarioAtCheckpoint();
    if (fullReset) mario.invulnerableUntil = 0;
    levelWon = false;
    mode = GAME_MENU;
    modeStartedMs = millis();
}

void startNextLevel() {
    if (marioLevel < 99) marioLevel++;
    // Jede neue Runde beginnt wieder mit vollen Leben. Der erreichte Score
    // bleibt als fortlaufender Gesamtstand erhalten.
    marioLives = 3;
    checkpointIndex = 0;
    generateLevel();
    for (uint8_t i = 0; i < blockCount; i++) blockAlive[i] = true;
    for (uint8_t i = 0; i < coinCount; i++) coinCollected[i] = false;
    clearTransientObjects();
    initEnemies();
    placeMarioAtCheckpoint();
    levelWon = false;
    mode = GAME_PLAYING;
    modeStartedMs = millis();
    playSound(SND_LEVELUP);
}

void respawnFromCheckpoint() {
    clearTransientObjects();
    initEnemies();
    placeMarioAtCheckpoint();
    mode = GAME_PLAYING;
    modeStartedMs = millis();
}

void loseLifeAndRespawn() {
    uint32_t now = millis();
    if (marioLives > 0) marioLives--;
    playSound(SND_DIE);
    if (marioLives == 0) {
        levelWon = false;
        runtime.animateTransition(CRGB::Red);
        mode = GAME_GAME_OVER;
        modeStartedMs = millis();
        return;
    }
    respawnFromCheckpoint();
}

InputState readInput(const GameControlEvents& controls) {
    int x1 = controls.primary.y;
    int y1 = controls.primary.x;
    int x3 = controls.secondary.y;
    int y3 = controls.secondary.x;

    InputState in = {0, false, false, false, false};
    int horizontal = abs(x1) >= abs(x3) ? x1 : x3;
    int vertical = abs(y1) >= abs(y3) ? y1 : y3;

    if (horizontal > INPUT_DEADZONE) in.move = 1;
    else if (horizontal < -INPUT_DEADZONE) in.move = -1;

    bool up1 = y1 > INPUT_DEADZONE;
    bool up3 = y3 > INPUT_DEADZONE;
    in.jumpHeld = up1 || up3;
    in.jumpPressed = (up1 && !lastUp1) || (up3 && !lastUp3);
    lastUp1 = up1;
    lastUp3 = up3;
    in.duck = vertical < -INPUT_DEADZONE;

    if (controls.primary.simpleClick || controls.secondary.simpleClick) {
        in.action = true;
    }
    return in;
}

InputState readBotInput() {
    InputState in = {1, false, false, false, false};
    int px = pixelX(mario.x);
    int py = pixelY(mario.y);
    int feetY = py + playerHeight() - 1;

    bool obstacleAhead = false;
    for (uint8_t h = 0; h < playerHeight(); h++) {
        if (solidAt(px + 1, py + h) || solidAt(px + 2, py + h)) {
            obstacleAhead = true;
            break;
        }
    }
    bool gapAhead = !solidAt(px + 2, feetY + 1) ||
                    !solidAt(px + 3, feetY + 1);
    bool enemyAhead = false;
    for (uint8_t i = 0; i < 8; i++) {
        if (!enemies[i].active) continue;
        int ex = pixelX(enemies[i].x);
        int ey = pixelY(enemies[i].y);
        if (ex > px && ex - px <= 5 && abs(ey - feetY) <= 3) {
            enemyAhead = true;
            break;
        }
    }

    bool shouldJump = mario.onGround && (obstacleAhead || gapAhead || enemyAhead);
    in.jumpPressed = shouldJump;
    in.jumpHeld = shouldJump || (mario.vy < 0 && mario.jumpHoldTicks < 6);
    uint32_t now = millis();
    if (mario.power == MARIO_FIRE && enemyAhead && now - lastBotActionMs >= 520UL) {
        in.action = true;
        lastBotActionMs = now;
    }
    return in;
}

void shootFireball() {
    if (mario.power != MARIO_FIRE) return;
    for (uint8_t i = 0; i < 4; i++) {
        if (fireballs[i].active) continue;
        fireballs[i].active = true;
        fireballs[i].x = mario.x + SCALE;
        fireballs[i].y = mario.y + SCALE;
        fireballs[i].vx = mario.vx < 0 ? -18 : 18;
        playSound(SND_SHOOT);
        return;
    }
}

bool aabbPx(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void collectCoins() {
    int px = pixelX(mario.x);
    int py = pixelY(mario.y);
    uint8_t ph = playerHeight();
    for (uint8_t i = 0; i < coinCount; i++) {
        if (coinCollected[i]) continue;
        int cx = packedX(levelCoins[i]);
        int cy = packedY(levelCoins[i]);
        if (!aabbPx(px, py, 1, ph, cx, cy, 1, 1)) continue;
        coinCollected[i] = true;
        marioCoins++;
        marioScore += 100;
        if (marioCoins >= 100) {
            marioCoins = 0;
            marioLives++;
        }
        playSound(SND_COIN);
    }
}

void collectItems() {
    int px = pixelX(mario.x);
    int py = pixelY(mario.y);
    uint8_t ph = playerHeight();
    for (uint8_t i = 0; i < 4; i++) {
        if (!items[i].active) continue;
        if (!aabbPx(px, py, 1, ph, pixelX(items[i].x), pixelY(items[i].y), 1, 1)) continue;
        if (items[i].type == ITEM_MUSHROOM && mario.power == MARIO_SMALL) setPowerKeepingFeet(MARIO_SUPER);
        else setPowerKeepingFeet(MARIO_FIRE);
        items[i].active = false;
        marioScore += 200;
        playSound(SND_POWERUP);
    }
}

void damageMario() {
    uint32_t now = millis();
    if ((long)(mario.invulnerableUntil - now) > 0) return;
    if (mario.power == MARIO_FIRE) {
        setPowerKeepingFeet(MARIO_SUPER);
        mario.invulnerableUntil = now + 1300UL;
        playSound(SND_HIT);
        return;
    }
    if (mario.power == MARIO_SUPER) {
        setPowerKeepingFeet(MARIO_SMALL);
        mario.invulnerableUntil = now + 1300UL;
        playSound(SND_HIT);
        return;
    }

    loseLifeAndRespawn();
}

bool enemySolidAt(Enemy& e, int16_t fx, int16_t fy) {
    return solidAt(pixelX(fx), pixelY(fy));
}

void updateGroundEnemy(Enemy& e) {
    if (e.state == 1) return;
    e.vy += ENEMY_GRAVITY;
    if (e.vy > MAX_FALL_VY) e.vy = MAX_FALL_VY;

    int8_t xdir = e.vx > 0 ? 1 : -1;
    int16_t nextX = e.x + e.vx;
    int aheadX = pixelX(nextX) + xdir;
    int footY = pixelY(e.y) + 1;
    if (solidAt(pixelX(nextX), pixelY(e.y)) || !solidAt(aheadX, footY)) {
        e.vx = -e.vx;
    } else {
        e.x = nextX;
    }

    int16_t nextY = e.y + e.vy;
    if (!enemySolidAt(e, e.x, nextY + SCALE)) {
        e.y = nextY;
    } else {
        while (!enemySolidAt(e, e.x, e.y + 1 + SCALE)) e.y++;
        e.vy = 0;
    }
}

void updateShell(Enemy& e) {
    if (e.state != 2) return;
    int16_t nextX = e.x + e.vx;
    if (solidAt(pixelX(nextX), pixelY(e.y))) {
        e.vx = -e.vx;
    } else {
        e.x = nextX;
    }
}

void updateEnemies() {
    int cam = cameraX;
    for (uint8_t i = 0; i < 8; i++) {
        Enemy& e = enemies[i];
        if (!e.active) continue;
        if (pixelX(e.x) < cam - 6 || pixelX(e.x) > cam + SCREEN_W + 10) continue;

        if (e.type == ENEMY_CROW) {
            e.x += e.vx;
            e.y = e.fixedY * SCALE;
            if (pixelX(e.x) < cam - 8) e.active = false;
        } else if (e.type == ENEMY_KOOPA && e.state == 2) {
            updateShell(e);
        } else {
            updateGroundEnemy(e);
        }
    }
}

void updateFireballs() {
    for (uint8_t i = 0; i < 4; i++) {
        if (!fireballs[i].active) continue;
        fireballs[i].x += fireballs[i].vx;
        int fx = pixelX(fireballs[i].x);
        int fy = pixelY(fireballs[i].y);
        if (fx < cameraX - 2 || fx > cameraX + SCREEN_W + 2 || solidAt(fx, fy)) {
            fireballs[i].active = false;
            continue;
        }
        for (uint8_t e = 0; e < 8; e++) {
            if (!enemies[e].active) continue;
            if (!aabbPx(fx, fy, 1, 1, pixelX(enemies[e].x), pixelY(enemies[e].y), 1, 1)) continue;
            enemies[e].active = false;
            fireballs[i].active = false;
            marioScore += 150;
            playSound(SND_EXPLOSION);
            break;
        }
    }
}

void checkEnemyCollisions() {
    int px = pixelX(mario.x);
    int py = pixelY(mario.y);
    uint8_t ph = playerHeight();
    for (uint8_t i = 0; i < 8; i++) {
        Enemy& e = enemies[i];
        if (!e.active) continue;
        int ex = pixelX(e.x);
        int ey = pixelY(e.y);
        if (!aabbPx(px, py, 1, ph, ex, ey, 1, 1)) continue;

        bool stomp = mario.vy > 0 && (py + ph - 1) <= ey;
        if (stomp) {
            if (e.type == ENEMY_KOOPA && e.state == 0) {
                e.state = 1;
                e.vx = 0;
            } else if (e.type == ENEMY_KOOPA && e.state == 1) {
                e.state = 2;
                e.vx = (px <= ex) ? 18 : -18;
            } else {
                e.active = false;
            }
            mario.vy = STOMP_VY;
            marioScore += 100;
            playSound(SND_HIT);
        } else if (e.type == ENEMY_KOOPA && e.state == 1) {
            e.state = 2;
            e.vx = (px <= ex) ? 18 : -18;
            playSound(SND_SHOOT);
        } else {
            damageMario();
        }
    }
}

void updateCamera() {
    int target = pixelX(mario.x) - 8;
    if (target < 0) target = 0;
    if (target > WORLD_W - SCREEN_W) target = WORLD_W - SCREEN_W;
    cameraX = target;
}

void updateCheckpoint() {
    int px = pixelX(mario.x);
    while (checkpointIndex + 1 < sizeof(checkpointMarks) / sizeof(checkpointMarks[0]) &&
           px >= checkpointMarks[checkpointIndex + 1]) {
        checkpointIndex++;
        playSound(SND_CHECKPOINT);
    }
}

void startJump() {
    mario.vy = JUMP_VY;
    mario.jumpHoldTicks = 0;
    mario.jumpBufferTicks = 0;
    mario.coyoteTicks = 0;
    mario.onGround = false;
    playSound(SND_JUMP);
}

void applyMarioGravity(bool jumpHeld) {
    int16_t gravity = GRAVITY_FALL;
    if (mario.vy < 0) {
        if (jumpHeld && mario.jumpHoldTicks < MAX_JUMP_HOLD_TICKS) {
            gravity = GRAVITY_HOLD;
            mario.jumpHoldTicks++;
        } else {
            gravity = GRAVITY_RELEASE;
        }
    }

    mario.vy += gravity;
    if (mario.vy > MAX_FALL_VY) mario.vy = MAX_FALL_VY;
}

void updatePlaying(const GameControlEvents& controls) {
    InputState in = {0, false, false, false, false};
    if (humanControlled) {
        if (!humanInputArmed) {
            bool neutral = abs(controls.primary.x) <= INPUT_DEADZONE &&
                           abs(controls.primary.y) <= INPUT_DEADZONE &&
                           abs(controls.secondary.x) <= INPUT_DEADZONE &&
                           abs(controls.secondary.y) <= INPUT_DEADZONE &&
                           !joystick1.isPressed() && !joystick3.isPressed();
            if (neutral) {
                humanInputArmed = true;
                lastUp1 = false;
                lastUp3 = false;
            }
        } else {
            in = readInput(controls);
        }
    } else {
        in = readBotInput();
    }
    if (!resolvePlayerEmbedding()) {
        respawnFromCheckpoint();
        return;
    }
    setDuckingKeepingFeet(in.duck && mario.power != MARIO_SMALL);
    mario.vx = in.move * (mario.ducking ? DUCK_VX : WALK_VX);
    if (in.action) shootFireball();

    if (in.jumpPressed) mario.jumpBufferTicks = JUMP_BUFFER_TICKS;
    else if (mario.jumpBufferTicks > 0) mario.jumpBufferTicks--;

    if (mario.onGround) mario.coyoteTicks = COYOTE_TICKS;
    else if (mario.coyoteTicks > 0) mario.coyoteTicks--;

    if (mario.jumpBufferTicks > 0 && (mario.onGround || mario.coyoteTicks > 0)) startJump();

    applyMarioGravity(in.jumpHeld);
    movePlayerX(mario.vx);
    movePlayerY(mario.vy);
    if (!resolvePlayerEmbedding()) {
        respawnFromCheckpoint();
        return;
    }

    collectCoins();
    collectItems();
    updateCamera();
    updateCheckpoint();
    updateEnemies();
    updateFireballs();
    checkEnemyCollisions();
    if (mode != GAME_PLAYING) return;

    if (pixelY(mario.y) >= SCREEN_H) {
        loseLifeAndRespawn();
        return;
    }
    if (pixelX(mario.x) >= FLAG_X) {
        marioScore += 1000;
        levelWon = true;
        runtime.animateTransition(CRGB::Green);
        mode = GAME_GAME_OVER;
        modeStartedMs = millis();
        playSound(SND_LEVELUP);
    }
}

void drawWorldPixel(int wx, int y, CRGB color) {
    int sx = wx - cameraX;
    if (sx >= 0 && sx < SCREEN_W && inWorldY(y)) setPixel(sx, y, color);
}

void renderWorld() {
    FastLED.clear();
    for (uint8_t x = 0; x < SCREEN_W; x++) {
        if (((x + cameraX) & 7) == 0) setPixel(x, 1, CRGB(8, 10, 22));
    }

    for (uint8_t i = 0; i < segmentCount; i++) {
        const Segment& s = levelSegments[i];
        CRGB col = s.type == SEG_GROUND ? CRGB(90, 52, 22) : CRGB(70, 95, 45);
        for (uint8_t x = 0; x < s.w; x++) drawWorldPixel(s.x + x, s.y, col);
    }

    for (uint8_t i = 0; i < blockCount; i++) {
        if (!blockAlive[i]) continue;
        CRGB col = levelBlocks[i].type == BLOCK_QUESTION ? CRGB::Orange : CRGB(145, 70, 32);
        drawWorldPixel(levelBlocks[i].x, levelBlocks[i].y, col);
    }

    for (uint8_t i = 0; i < coinCount; i++) {
        if (coinCollected[i]) continue;
        drawWorldPixel(packedX(levelCoins[i]), packedY(levelCoins[i]), CRGB::Yellow);
    }

    for (size_t i = 1; i < sizeof(checkpointMarks) / sizeof(checkpointMarks[0]); i++) {
        CRGB col = (i <= checkpointIndex) ? CRGB::Green : CRGB(40, 80, 160);
        drawWorldPixel(checkpointMarks[i], 14, col);
        drawWorldPixel(checkpointMarks[i], 13, col);
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (!items[i].active) continue;
        drawWorldPixel(pixelX(items[i].x), pixelY(items[i].y), items[i].type == ITEM_FLOWER ? CRGB::Magenta : CRGB::Red);
    }

    for (uint8_t i = 0; i < 8; i++) {
        if (!enemies[i].active) continue;
        CRGB col = CRGB::DarkOrange;
        if (enemies[i].type == ENEMY_KOOPA) col = enemies[i].state == 0 ? CRGB::Green : CRGB(70, 180, 70);
        if (enemies[i].type == ENEMY_CROW) col = CRGB(120, 90, 220);
        drawWorldPixel(pixelX(enemies[i].x), pixelY(enemies[i].y), col);
    }

    for (uint8_t i = 0; i < 4; i++)
        if (fireballs[i].active) drawWorldPixel(pixelX(fireballs[i].x), pixelY(fireballs[i].y), CRGB::OrangeRed);

    for (uint8_t y = 4; y < 15; y++) drawWorldPixel(FLAG_X, y, CRGB::White);
    drawWorldPixel(FLAG_X + 1, 4, CRGB::Green);
    drawWorldPixel(FLAG_X + 2, 4, CRGB::Green);
    drawWorldPixel(FLAG_X + 1, 5, CRGB::Green);
}

void drawMario() {
    int sx = pixelX(mario.x) - cameraX;
    int sy = pixelY(mario.y);
    if (sx < 0 || sx >= SCREEN_W) return;
    CRGB col = CRGB::Red;
    if (mario.power == MARIO_SUPER) col = CRGB::OrangeRed;
    if (mario.power == MARIO_FIRE) col = CRGB::White;
    if ((long)(mario.invulnerableUntil - millis()) > 0 && (millis() / 120) % 2) col.nscale8_video(65);
    uint8_t h = playerHeight();
    for (uint8_t i = 0; i < h; i++) {
        if (inWorldY(sy + i)) setPixel(sx, sy + i, col);
    }
}

void drawHud() {
    for (uint8_t i = 0; i < marioLives && i < 5; i++) setPixel(i, 0, CRGB::Red);
    uint8_t coinDots = marioCoins > 5 ? 5 : marioCoins;
    for (uint8_t i = 0; i < coinDots; i++) setPixel(31 - i, 0, CRGB::Yellow);
}

void renderPlaying() {
    renderWorld();
    drawMario();
    drawHud();
    FastLED.show();
}

void renderLobby() {
    uint32_t now = millis();
    FastLED.clear();

    // Ruhige Mini-Level-Szene: Boden, Frageblock und Rohr bleiben fest,
    // waehrend Mario sichtbar laeuft und ueber das Hindernis springt.
    for (int x = 0; x < SCREEN_W; x++) {
        setPixel(x, 14, CRGB(30, 150, 45));
        setPixel(x, 15, CRGB(105, 53, 24));
    }

    for (int x = 12; x <= 16; x++) setPixel(x, 9, CRGB(205, 105, 20));
    CRGB questionColor = ((now / 220UL) & 1) ? CRGB::Yellow : CRGB::Orange;
    setPixel(14, 9, questionColor);
    setPixel(14, 8, questionColor);

    for (int x = 25; x <= 28; x++) setPixel(x, 12, CRGB(25, 190, 55));
    for (int y = 10; y <= 13; y++) {
        setPixel(26, y, CRGB(20, 145, 40));
        setPixel(27, y, CRGB(35, 205, 60));
    }

    uint8_t phase = (now / 90UL) % 58;
    int sx = 2 + phase / 2;
    int jumpHeight = 0;
    if (sx >= 10 && sx <= 19) {
        int distance = abs(14 - sx);
        jumpHeight = max(0, 5 - distance);
    } else if (sx >= 23 && sx <= 30) {
        int distance = abs(27 - sx);
        jumpHeight = max(0, 5 - distance);
    }

    int feetY = 13 - jumpHeight;
    CRGB marioColor = ((now / 150UL) & 1) ? CRGB::Red : CRGB::OrangeRed;
    setPixel(sx, feetY - 1, marioColor);
    setPixel(sx, feetY, CRGB(45, 80, 210));
    if (jumpHeight == 0) {
        int leg = ((now / 90UL) & 1) ? -1 : 1;
        setPixel(sx + leg, feetY, CRGB(105, 52, 24));
    }

    bool coinCollected = sx >= 13 && sx <= 16 && jumpHeight >= 3;
    if (!coinCollected) {
        int coinY = 5 + ((now / 180UL) & 1);
        setPixel(14, coinY, CRGB::Yellow);
        setPixel(15, coinY, CRGB(255, 170, 20));
    }

    int goombaX = 20 + ((now / 260UL) & 1);
    setPixel(goombaX, 13, CRGB(150, 65, 20));
    setPixel(goombaX + 1, 13, CRGB(95, 42, 18));
    FastLED.show();
}

void drawCenteredWord(const char* word, int y, CRGB color) {
    int length = strlen(word);
    int width = length > 0 ? length * 4 - 1 : 0;
    int startX = max(0, (SCREEN_W - width) / 2);
    for (int i = 0; i < length; i++)
        drawChar3x5(startX + i * 4, y, word[i], color);
}

void drawCenteredScore(uint16_t value, int y, CRGB color) {
    uint16_t shown = value;
    uint16_t divisor = 1;
    int digits = 1;
    while (shown / divisor >= 10 && digits < 5) {
        divisor *= 10;
        digits++;
    }
    int width = digits * 4 - 1;
    int x = (SCREEN_W - width) / 2;
    while (divisor > 0) {
        drawDigitW(x, y, (shown / divisor) % 10, color);
        x += 4;
        divisor /= 10;
    }
}

void renderEnd(bool won) {
    FastLED.clear();
    CRGB resultColor = won ? CRGB::Green : CRGB::Red;
    drawCenteredWord(won ? "WIN" : "LOSE", 0, resultColor);
    drawCenteredWord("SCORE", 6, CRGB::Yellow);
    drawCenteredScore(marioScore, 11, CRGB::White);
    FastLED.show();
}
}

void marioRequestRestart() {
    restartRequested = true;
}

String marioStateJson() {
    String power = "small";
    if (mario.power == MARIO_SUPER) power = "super";
    if (mario.power == MARIO_FIRE) power = "fire";
    String state = "lobby";
    if (mode == GAME_COUNTDOWN) state = "countdown";
    else if (mode == GAME_PLAYING) state = "playing";
    else if (mode == GAME_PAUSED) state = "paused";
    else if (mode == GAME_GAME_OVER) state = levelWon ? "win" : "gameover";

    String json = "{";
    json += "\"state\":\"" + state + "\"";
    json += ",\"level\":" + String(marioLevel);
    json += ",\"lives\":" + String(marioLives);
    json += ",\"score\":" + String(marioScore);
    json += ",\"coins\":" + String(marioCoins);
    json += ",\"power\":\"" + power + "\"";
    json += ",\"x\":" + String(pixelX(mario.x));
    json += ",\"camera\":" + String(cameraX);
    json += ",\"checkpoint\":" + String(checkpointX());
    json += "}";
    return json;
}

void renderMarioMenuPreview() {
    renderLobby();
}

void taskMarioHandler(void *pvParameters) {
    int lastTask = -1;

    for (;;) {
        if (aktiverTask != 10) {
            feedRuntimeWatchdog();
            lastTask = -1;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (lastTask != 10 || restartRequested) {
            restartRequested = false;
            humanControlled = false;
            humanInputArmed = false;
            joystick1.reset();
            joystick3.reset();
            resetLevel(true);
            mode = GAME_COUNTDOWN;
            modeStartedMs = millis();
            runtime.begin();
            lastTask = 10;
            Serial.println("Mario gestartet.");
        }

        if (!lockDisplay(20)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t now = millis();
        GameControlEvents controls = runtime.poll(joystick1, joystick3);
        if ((controls.primaryDoubleClick || controls.secondaryDoubleClick) && !humanControlled) {
            humanControlled = true;
            humanInputArmed = false;
            mario.vx = 0;
            controls.primaryDoubleClick = false;
            controls.secondaryDoubleClick = false;
            controls.doubleClick = false;
            playSound(SND_POWERUP);
        }
        if (controls.pauseToggle && (mode == GAME_PLAYING || mode == GAME_PAUSED))
            runtime.applyPause(mode);
        if (mode != GAME_PLAYING) {
            joystick1.einfacherKlickZaehler = 0;
            joystick3.einfacherKlickZaehler = 0;
        }
        if (now - lastTickMs >= TICK_MS) {
            lastTickMs = now;
            if (mode == GAME_PLAYING) updatePlaying(controls);
        }

        if (mode == GAME_MENU) {
            renderLobby();
        } else if (mode == GAME_COUNTDOWN) {
            renderPlaying();
            uint32_t elapsed = now - modeStartedMs;
            runtime.drawCountdownOverlay(3 - min(2UL, elapsed / 700UL));
            FastLED.show();
            if (elapsed >= 2100UL) {
                mode = GAME_PLAYING;
                lastTickMs = now;
            }
        } else if (mode == GAME_PLAYING) {
            renderPlaying();
        } else if (mode == GAME_PAUSED) {
            runtime.drawPauseOverlay();
        } else if (levelWon) {
            renderEnd(true);
            if (millis() - modeStartedMs > END_SCREEN_MS) {
                startNextLevel();
            }
        } else {
            renderEnd(false);
            if (now - modeStartedMs >= END_SCREEN_MS) {
                resetLevel(true);
                runtime.animateTransition(CRGB::Green);
                mode = GAME_COUNTDOWN;
                modeStartedMs = now;
                lastUp1 = false;
                lastUp3 = false;
                playSound(SND_SELECT);
            }
        }

        unlockDisplay();
        runtime.waitForNextFrame(16);
    }
}
