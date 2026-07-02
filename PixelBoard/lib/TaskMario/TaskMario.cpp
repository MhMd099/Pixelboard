#include "TaskMario.h"

#include <Arduino.h>
#include <FastLED.h>

#include "Config.h"
#include "HardwareUtils.h"
#include "Joystick.h"
#include "SoundUtils.h"

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

const int16_t WALK_VX = 12;
const int16_t DUCK_VX = 7;
const int16_t JUMP_VY = -28;
const int16_t JUMP_HOLD_BOOST = -2;
const int16_t GRAVITY = 3;
const int16_t MAX_FALL_VY = 26;
const uint8_t MAX_JUMP_HOLD_TICKS = 7;

enum MarioMode : uint8_t { MARIO_LOBBY, MARIO_PLAYING, MARIO_WIN, MARIO_GAMEOVER };
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

const Segment levelSegments[] = {
    {0, 15, 34, SEG_GROUND}, {38, 15, 24, SEG_GROUND}, {66, 15, 28, SEG_GROUND},
    {98, 15, 28, SEG_GROUND}, {131, 15, 45, SEG_GROUND},
    {12, 11, 8, SEG_PLATFORM}, {25, 9, 5, SEG_PLATFORM}, {46, 12, 8, SEG_PLATFORM},
    {72, 10, 9, SEG_PLATFORM}, {108, 11, 10, SEG_PLATFORM}, {136, 8, 7, SEG_PLATFORM},
    {151, 12, 9, SEG_PLATFORM}
};

const BlockDef levelBlocks[] = {
    {18, 10, BLOCK_QUESTION}, {19, 10, BLOCK_BRICK}, {20, 10, BLOCK_BRICK},
    {52, 9, BLOCK_QUESTION}, {53, 9, BLOCK_BRICK}, {54, 9, BLOCK_BRICK},
    {76, 7, BLOCK_BRICK}, {77, 7, BLOCK_QUESTION}, {78, 7, BLOCK_BRICK},
    {112, 9, BLOCK_QUESTION}, {113, 9, BLOCK_BRICK}, {140, 6, BLOCK_QUESTION}
};

const uint16_t levelCoins[] = {
    0x0E0A, 0x0F0A, 0x100A, 0x1A08, 0x1B08, 0x2F0B, 0x300B, 0x4A09,
    0x4B09, 0x4C09, 0x6E0A, 0x6F0A, 0x700A, 0x8907, 0x8A07, 0x990B,
    0x9A0B, 0x9B0B, 0xA20D, 0xA30D
};

const EnemySpawn enemySpawns[] = {
    {30, 14, ENEMY_GOOMBA}, {58, 14, ENEMY_KOOPA}, {84, 14, ENEMY_GOOMBA},
    {116, 14, ENEMY_KOOPA}, {145, 7, ENEMY_CROW}, {158, 10, ENEMY_CROW}
};

bool blockAlive[sizeof(levelBlocks) / sizeof(levelBlocks[0])];
bool coinCollected[sizeof(levelCoins) / sizeof(levelCoins[0])];
Enemy enemies[8];
Item items[4];
Fireball fireballs[4];

MarioMode mode = MARIO_LOBBY;
Player mario;
uint16_t marioScore = 0;
uint8_t marioCoins = 0;
int16_t cameraX = 0;
uint32_t lastTickMs = 0;
uint32_t modeStartedMs = 0;
bool lastUp1 = false;
bool lastUp3 = false;
volatile bool restartRequested = false;

int pixelX(int16_t fixedValue) { return fixedValue / SCALE; }
int pixelY(int16_t fixedValue) { return fixedValue / SCALE; }
uint8_t packedX(uint16_t packed) { return (uint8_t)(packed >> 4); }
uint8_t packedY(uint16_t packed) { return (uint8_t)(packed & 0x0F); }

uint8_t playerHeight() {
    if (mario.power == MARIO_SMALL) return 2;
    return mario.ducking ? 2 : 3;
}

bool inWorldY(int y) {
    return y >= 0 && y < SCREEN_H;
}

bool segmentSolidAt(int x, int y) {
    for (size_t i = 0; i < sizeof(levelSegments) / sizeof(levelSegments[0]); i++) {
        const Segment& s = levelSegments[i];
        if (y == s.y && x >= s.x && x < s.x + s.w) return true;
    }
    return false;
}

int blockIndexAt(int x, int y) {
    for (size_t i = 0; i < sizeof(levelBlocks) / sizeof(levelBlocks[0]); i++) {
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
        playSound(SND_BUFF);
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

void initEnemies() {
    for (uint8_t i = 0; i < 8; i++) enemies[i].active = false;
    for (size_t i = 0; i < sizeof(enemySpawns) / sizeof(enemySpawns[0]) && i < 8; i++) {
        const EnemySpawn& s = enemySpawns[i];
        enemies[i].active = true;
        enemies[i].type = s.type;
        enemies[i].state = 0;
        enemies[i].x = s.x * SCALE;
        enemies[i].y = s.y * SCALE;
        enemies[i].vx = (s.type == ENEMY_CROW) ? -8 : -5;
        enemies[i].vy = 0;
        enemies[i].fixedY = s.y;
    }
}

void resetLevel(bool resetLives) {
    if (resetLives) marioLives = 3;
    mario.x = 2 * SCALE;
    mario.y = 12 * SCALE;
    mario.vx = 0;
    mario.vy = 0;
    mario.power = MARIO_SMALL;
    mario.jumpHoldTicks = 0;
    mario.onGround = false;
    mario.ducking = false;
    mario.invulnerableUntil = 0;
    marioScore = 0;
    marioCoins = 0;
    cameraX = 0;
    for (size_t i = 0; i < sizeof(blockAlive) / sizeof(blockAlive[0]); i++) blockAlive[i] = true;
    for (size_t i = 0; i < sizeof(coinCollected) / sizeof(coinCollected[0]); i++) coinCollected[i] = false;
    for (uint8_t i = 0; i < 4; i++) {
        items[i].active = false;
        fireballs[i].active = false;
    }
    initEnemies();
    mode = MARIO_LOBBY;
    modeStartedMs = millis();
}

InputState readInput() {
    int x1 = joystick1.readYPercent();
    int y1 = joystick1.readXPercent();
    int x3 = joystick3.readYPercent();
    int y3 = joystick3.readXPercent();

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

    if (joystick1.einfacherKlickZaehler > 0 || joystick3.einfacherKlickZaehler > 0) {
        in.action = true;
        joystick1.einfacherKlickZaehler = 0;
        joystick3.einfacherKlickZaehler = 0;
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
    for (size_t i = 0; i < sizeof(levelCoins) / sizeof(levelCoins[0]); i++) {
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
        playSound(SND_EAT);
    }
}

void collectItems() {
    int px = pixelX(mario.x);
    int py = pixelY(mario.y);
    uint8_t ph = playerHeight();
    for (uint8_t i = 0; i < 4; i++) {
        if (!items[i].active) continue;
        if (!aabbPx(px, py, 1, ph, pixelX(items[i].x), pixelY(items[i].y), 1, 1)) continue;
        if (items[i].type == ITEM_MUSHROOM && mario.power == MARIO_SMALL) mario.power = MARIO_SUPER;
        else mario.power = MARIO_FIRE;
        items[i].active = false;
        marioScore += 200;
        playSound(SND_BUFF);
    }
}

void damageMario() {
    uint32_t now = millis();
    if ((long)(mario.invulnerableUntil - now) > 0) return;
    if (mario.power == MARIO_FIRE) {
        mario.power = MARIO_SUPER;
        mario.invulnerableUntil = now + 1300UL;
        playSound(SND_HIT);
        return;
    }
    if (mario.power == MARIO_SUPER) {
        mario.power = MARIO_SMALL;
        mario.ducking = false;
        mario.invulnerableUntil = now + 1300UL;
        playSound(SND_HIT);
        return;
    }

    if (marioLives > 0) marioLives--;
    playSound(SND_DIE);
    if (marioLives == 0) {
        mode = MARIO_GAMEOVER;
        modeStartedMs = now;
    } else {
        resetLevel(false);
        mode = MARIO_PLAYING;
    }
}

bool enemySolidAt(Enemy& e, int16_t fx, int16_t fy) {
    return solidAt(pixelX(fx), pixelY(fy));
}

void updateGroundEnemy(Enemy& e) {
    if (e.state == 1) return;
    e.vy += GRAVITY;
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
            mario.vy = JUMP_VY / 2;
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

void updatePlaying() {
    InputState in = readInput();
    mario.ducking = in.duck && mario.power != MARIO_SMALL;
    mario.vx = in.move * (mario.ducking ? DUCK_VX : WALK_VX);
    if (in.action) shootFireball();

    if (in.jumpPressed && mario.onGround) {
        mario.vy = JUMP_VY;
        mario.jumpHoldTicks = 0;
        mario.onGround = false;
        playSound(SND_SELECT);
    }
    if (in.jumpHeld && !mario.onGround && mario.vy < 0 && mario.jumpHoldTicks < MAX_JUMP_HOLD_TICKS) {
        mario.vy += JUMP_HOLD_BOOST;
        mario.jumpHoldTicks++;
    }

    mario.vy += GRAVITY;
    if (mario.vy > MAX_FALL_VY) mario.vy = MAX_FALL_VY;
    movePlayerX(mario.vx);
    movePlayerY(mario.vy);

    collectCoins();
    collectItems();
    updateCamera();
    updateEnemies();
    updateFireballs();
    checkEnemyCollisions();

    if (pixelY(mario.y) >= SCREEN_H) damageMario();
    if (pixelX(mario.x) >= FLAG_X) {
        marioScore += 1000;
        mode = MARIO_WIN;
        modeStartedMs = millis();
        playSound(SND_BUFF);
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

    for (size_t i = 0; i < sizeof(levelSegments) / sizeof(levelSegments[0]); i++) {
        const Segment& s = levelSegments[i];
        CRGB col = s.type == SEG_GROUND ? CRGB(90, 52, 22) : CRGB(70, 95, 45);
        for (uint8_t x = 0; x < s.w; x++) drawWorldPixel(s.x + x, s.y, col);
    }

    for (size_t i = 0; i < sizeof(levelBlocks) / sizeof(levelBlocks[0]); i++) {
        if (!blockAlive[i]) continue;
        CRGB col = levelBlocks[i].type == BLOCK_QUESTION ? CRGB::Orange : CRGB(145, 70, 32);
        drawWorldPixel(levelBlocks[i].x, levelBlocks[i].y, col);
    }

    for (size_t i = 0; i < sizeof(levelCoins) / sizeof(levelCoins[0]); i++) {
        if (coinCollected[i]) continue;
        drawWorldPixel(packedX(levelCoins[i]), packedY(levelCoins[i]), CRGB::Yellow);
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
    FastLED.clear();
    uint32_t now = millis();
    cameraX = (now / 160) % 40;
    renderWorld();
    int bob = (now / 240) % 2;
    int sx = 8;
    int sy = 11 - bob;
    setPixel(sx, sy, CRGB::Red);
    setPixel(sx, sy + 1, CRGB::Red);
    setPixel(18, 9, CRGB::Yellow);
    setPixel(24, 12, CRGB::DarkOrange);
    setPixel(28, 6, CRGB(120, 90, 220));
    FastLED.show();
}

void renderEnd(CRGB col) {
    FastLED.clear();
    for (int x = 8; x < 24; x++) setPixel(x, 5, col);
    for (int x = 10; x < 22; x++) setPixel(x, 8, CRGB::Yellow);
    drawHud();
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
    if (mode == MARIO_PLAYING) state = "playing";
    else if (mode == MARIO_WIN) state = "win";
    else if (mode == MARIO_GAMEOVER) state = "gameover";

    String json = "{";
    json += "\"state\":\"" + state + "\"";
    json += ",\"lives\":" + String(marioLives);
    json += ",\"score\":" + String(marioScore);
    json += ",\"coins\":" + String(marioCoins);
    json += ",\"power\":\"" + power + "\"";
    json += ",\"x\":" + String(pixelX(mario.x));
    json += ",\"camera\":" + String(cameraX);
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
            lastTask = -1;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (lastTask != 10 || restartRequested) {
            restartRequested = false;
            resetLevel(true);
            lastTask = 10;
            Serial.println("Mario gestartet.");
        }

        if (!lockDisplay(20)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t now = millis();
        if (now - lastTickMs >= TICK_MS) {
            lastTickMs = now;
            if (mode == MARIO_PLAYING) updatePlaying();
        }

        if (mode == MARIO_LOBBY) {
            renderLobby();
            bool start = joystick1.isPressed() || joystick3.isPressed();
            if (start) {
                mode = MARIO_PLAYING;
                modeStartedMs = millis();
                lastUp1 = false;
                lastUp3 = false;
                playSound(SND_SELECT);
            }
        } else if (mode == MARIO_PLAYING) {
            renderPlaying();
        } else if (mode == MARIO_WIN) {
            renderEnd(CRGB::Green);
            if (millis() - modeStartedMs > 1800UL) {
                resetLevel(true);
            }
        } else {
            renderEnd(CRGB::Red);
            if (millis() - modeStartedMs > 1600UL) {
                aktiverTask = -1;
                fokusModus = 0;
            }
        }

        unlockDisplay();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
