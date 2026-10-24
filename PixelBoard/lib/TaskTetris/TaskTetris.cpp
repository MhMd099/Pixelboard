#include "TaskTetris.h"

#include <Arduino.h>
#include <FastLED.h>
#include <string.h>

#include "Config.h"
#include "HardwareUtils.h"
#include "Joystick.h"
#include "SoundUtils.h"
#include "GameRuntime.h"
#include "RuntimeHealth.h"

extern volatile int aktiverTask;
extern Joystick joystick1;
extern Joystick joystick3;

namespace {
const int LOGICAL_W = 16;
const int LOGICAL_H = 32;
const int FIELD_W = 10;
const int FIELD_H = 20;
const int FIELD_X = 3;
const int FIELD_Y = 9;
const int HOLD_X = 0;
const int HOLD_Y = 5;
const int SCORE_X = 0;
const int SCORE_Y = 0;
const int NEXT_X = 12;
const int NEXT_Y = 5;
const int INPUT_DEADZONE = 62;
const unsigned long MOVE_REPEAT_MS = 125;
const unsigned long BUTTON_HOLD_MS = 700;
const unsigned long BOT_ACTION_MS = 150;
const unsigned long FAST_BOT_ACTION_MS = 75;
const unsigned long BOT_DROP_DELAY_MS = 700;
const uint32_t MAX_TETRIS_SCORE = 999999UL;

struct Piece {
    int8_t type;
    int8_t rot;
    int8_t x;
    int8_t y;
};

UnifiedGameState state = GAME_MENU;
GameRuntime runtime;
unsigned long countdownStartedMs = 0;
uint8_t board[FIELD_H][FIELD_W];
Piece current;
int8_t holdType = -1;
int8_t nextType = 0;
bool holdUsed = false;
uint32_t score = 0;
uint16_t linesCleared = 0;
unsigned long lastFallMs = 0;
unsigned long lastMoveMs = 0;
bool lastUpHeld = false;
bool lastButton = false;
bool holdTriggered = false;
unsigned long buttonDownMs = 0;
bool humanControlled = false;
bool humanInputArmed = false;
int8_t botTargetRot = 0;
int8_t botTargetX = 3;
unsigned long lastBotActionMs = 0;
unsigned long botAlignedSinceMs = 0;
unsigned long gameOverStartedMs = 0;

const CRGB pieceColors[7] = {
    CRGB::Cyan,
    CRGB::Yellow,
    CRGB(160, 70, 255),
    CRGB::Green,
    CRGB::Red,
    CRGB::Blue,
    CRGB::Orange
};

const int8_t shapes[7][4][4][2] = {
    {{{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}}, {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}}},
    {{{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}},
    {{{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}}},
    {{{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}}},
    {{{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}}},
    {{{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}}, {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}}},
    {{{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}}}
};

void setRotatedPixel(int lx, int ly, CRGB color) {
    if (lx < 0 || lx >= LOGICAL_W || ly < 0 || ly >= LOGICAL_H) return;
    int px = 31 - ly;
    int py = lx;
    setPixel(px, py, color);
}

void drawLogicalChar(int x, int y, char c, CRGB color) {
    static const uint8_t glyphs[10][5] = {
        {0x7,0x5,0x5,0x5,0x7}, {0x2,0x6,0x2,0x2,0x7}, {0x7,0x1,0x7,0x4,0x7}, {0x7,0x1,0x7,0x1,0x7},
        {0x5,0x5,0x7,0x1,0x1}, {0x7,0x4,0x7,0x1,0x7}, {0x7,0x4,0x7,0x5,0x7}, {0x7,0x1,0x1,0x1,0x1},
        {0x7,0x5,0x7,0x5,0x7}, {0x7,0x5,0x7,0x1,0x7}
    };
    if (c < '0' || c > '9') return;
    const uint8_t* g = glyphs[c - '0'];
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if (g[row] & (1 << (2 - col))) setRotatedPixel(x + col, y + row, color);
        }
    }
}

void drawNumberSmall(uint32_t value, int x, int y, CRGB color) {
    if (value > 9999) value = 9999;
    char buf[5];
    buf[0] = '0' + (value / 1000) % 10;
    buf[1] = '0' + (value / 100) % 10;
    buf[2] = '0' + (value / 10) % 10;
    buf[3] = '0' + value % 10;
    buf[4] = '\0';
    int start = value >= 1000 ? 0 : (value >= 100 ? 1 : (value >= 10 ? 2 : 3));
    for (int i = start; i < 4; i++) drawLogicalChar(x + (i - start) * 4, y, buf[i], color);
}

bool pieceCell(const Piece& p, int i, int& x, int& y) {
    if (p.type < 0 || p.type >= 7) return false;
    x = p.x + shapes[p.type][p.rot & 3][i][0];
    y = p.y + shapes[p.type][p.rot & 3][i][1];
    return true;
}

bool validPiece(const Piece& p) {
    for (int i = 0; i < 4; i++) {
        int x, y;
        pieceCell(p, i, x, y);
        if (x < 0 || x >= FIELD_W || y < 0 || y >= FIELD_H) return false;
        if (board[y][x] != 0) return false;
    }
    return true;
}

void planBotPlacement() {
    long bestValue = -2147483647L;
    botTargetRot = 0;
    botTargetX = 3;
    botAlignedSinceMs = 0;

    for (int8_t rot = 0; rot < 4; rot++) {
        for (int8_t targetX = -2; targetX < FIELD_W; targetX++) {
            Piece test = {current.type, rot, targetX, 0};
            if (!validPiece(test)) continue;
            while (true) {
                Piece next = test;
                next.y++;
                if (!validPiece(next)) break;
                test = next;
            }

            uint8_t simulated[FIELD_H][FIELD_W];
            memcpy(simulated, board, sizeof(simulated));
            for (int i = 0; i < 4; i++) {
                int x, y;
                pieceCell(test, i, x, y);
                if (x >= 0 && x < FIELD_W && y >= 0 && y < FIELD_H)
                    simulated[y][x] = current.type + 1;
            }

            int completed = 0;
            for (int y = FIELD_H - 1; y >= 0; y--) {
                bool full = true;
                for (int x = 0; x < FIELD_W; x++) {
                    if (simulated[y][x] == 0) {
                        full = false;
                        break;
                    }
                }
                if (!full) continue;

                completed++;
                for (int yy = y; yy > 0; yy--)
                    memcpy(simulated[yy], simulated[yy - 1], FIELD_W);
                memset(simulated[0], 0, FIELD_W);
                y++;
            }

            int heights[FIELD_W] = {0};
            int holes = 0;
            int coveredHoleDepth = 0;
            int aggregateHeight = 0;
            int maximumHeight = 0;
            for (int x = 0; x < FIELD_W; x++) {
                bool blockSeen = false;
                int blocksAbove = 0;
                for (int y = 0; y < FIELD_H; y++) {
                    if (simulated[y][x] != 0) {
                        if (!blockSeen) heights[x] = FIELD_H - y;
                        blockSeen = true;
                        blocksAbove++;
                    } else if (blockSeen) {
                        holes++;
                        coveredHoleDepth += blocksAbove;
                    }
                }
                aggregateHeight += heights[x];
                maximumHeight = max(maximumHeight, heights[x]);
            }
            int bumpiness = 0;
            for (int x = 0; x < FIELD_W - 1; x++)
                bumpiness += abs(heights[x] - heights[x + 1]);

            int wells = 0;
            for (int x = 0; x < FIELD_W; x++) {
                int leftHeight = x == 0 ? FIELD_H : heights[x - 1];
                int rightHeight = x == FIELD_W - 1 ? FIELD_H : heights[x + 1];
                int depth = min(leftHeight, rightHeight) - heights[x];
                if (depth > 0) wells += depth * (depth + 1) / 2;
            }

            int minPieceY = FIELD_H;
            int maxPieceY = 0;
            for (int i = 0; i < 4; i++) {
                int x, y;
                pieceCell(test, i, x, y);
                minPieceY = min(minPieceY, y);
                maxPieceY = max(maxPieceY, y);
            }
            int landingHeight = FIELD_H - (minPieceY + maxPieceY) / 2;

            long value = completed * 14000L - holes * 6500L -
                         coveredHoleDepth * 700L - aggregateHeight * 45L -
                         bumpiness * 40L - maximumHeight * 110L -
                         wells * 75L - landingHeight * 20L;
            if (value > bestValue) {
                bestValue = value;
                botTargetRot = rot;
                botTargetX = targetX;
            }
        }
    }
}

int8_t randomPiece() {
    return (int8_t)random(0, 7);
}

uint32_t visibleScore() {
    return score % 10000UL;
}

void syncDashboardScore() {
    tetrisCurrentScore = score;
    tetrisBoardScore = visibleScore();
    tetrisPlaying = state == GAME_PLAYING;
}

unsigned long currentFallInterval() {
    uint16_t normalMs = constrain((int)tetrisNormalFallMs, 120, 1500);
    uint16_t minMs = constrain((int)tetrisMinFallMs, 80, normalMs);
    uint16_t scoreStep = constrain((int)tetrisSpeedScoreStep, 50, 2000);
    uint16_t speedStep = constrain((int)tetrisSpeedStepMs, 0, 250);
    if (speedStep == 0) return normalMs;
    uint32_t steps = score / scoreStep;
    unsigned long maximumReduction = normalMs - minMs;
    uint32_t stepsToMinimum = (maximumReduction + speedStep - 1) / speedStep;
    if (steps >= stepsToMinimum) return minMs;
    unsigned long reduction = steps * (unsigned long)speedStep;
    return normalMs - reduction;
}

void addScoreSafe(uint32_t points) {
    if (score >= MAX_TETRIS_SCORE - min(points, MAX_TETRIS_SCORE))
        score = MAX_TETRIS_SCORE;
    else
        score += points;
}

void spawnPiece(int8_t type) {
    current.type = type;
    current.rot = 0;
    current.x = 3;
    current.y = 0;
    holdUsed = false;
    if (!validPiece(current)) {
        runtime.animateTransition(CRGB::Red);
        state = GAME_GAME_OVER;
        gameOverStartedMs = millis();
        playSound(SND_DIE);
        return;
    }
    planBotPlacement();
}

void clearBoard() {
    for (int y = 0; y < FIELD_H; y++)
        for (int x = 0; x < FIELD_W; x++)
            board[y][x] = 0;
}

void resetGame() {
    clearBoard();
    score = 0;
    linesCleared = 0;
    holdType = -1;
    nextType = randomPiece();
    holdUsed = false;
    lastFallMs = millis();
    lastMoveMs = 0;
    lastUpHeld = false;
    lastButton = false;
    holdTriggered = false;
    buttonDownMs = 0;
    countdownStartedMs = millis();
    state = GAME_COUNTDOWN;
    spawnPiece(randomPiece());
    syncDashboardScore();
}

void lockPiece() {
    for (int i = 0; i < 4; i++) {
        int x, y;
        pieceCell(current, i, x, y);
        if (x >= 0 && x < FIELD_W && y >= 0 && y < FIELD_H) board[y][x] = current.type + 1;
    }
}

void clearLines() {
    uint8_t cleared = 0;
    for (int y = FIELD_H - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < FIELD_W; x++) {
            if (board[y][x] == 0) {
                full = false;
                break;
            }
        }
        if (!full) continue;

        cleared++;
        for (int yy = y; yy > 0; yy--)
            for (int x = 0; x < FIELD_W; x++)
                board[yy][x] = board[yy - 1][x];
        for (int x = 0; x < FIELD_W; x++) board[0][x] = 0;
        y++;
    }
    if (cleared > 0) {
        linesCleared = (uint16_t)min(65535UL, (unsigned long)linesCleared + cleared);
        addScoreSafe(cleared == 1 ? 100 : cleared == 2 ? 300 : cleared == 3 ? 500 : 800);
        playSound(SND_EXPLOSION);
    }
}

void bakeAndSpawn() {
    lockPiece();
    clearLines();
    int8_t spawnType = nextType;
    nextType = randomPiece();
    spawnPiece(spawnType);
    lastFallMs = millis();
}

bool movePiece(int dx) {
    Piece moved = current;
    moved.x += dx;
    if (validPiece(moved)) {
        current = moved;
        playSound(SND_UI);
        return true;
    }
    return false;
}

bool rotatePiece() {
    Piece rotated = current;
    rotated.rot = (rotated.rot + 1) & 3;
    if (!validPiece(rotated)) {
        rotated.x--;
        if (!validPiece(rotated)) {
            rotated.x += 2;
            if (!validPiece(rotated)) return false;
        }
    }
    current = rotated;
    playSound(SND_SELECT);
    return true;
}

void holdPiece() {
    if (holdUsed) return;
    int8_t old = holdType;
    holdType = current.type;
    holdUsed = true;
    if (old < 0) {
        int8_t spawnType = nextType;
        nextType = randomPiece();
        spawnPiece(spawnType);
    }
    else spawnPiece(old);
    holdUsed = true;
    playSound(SND_BUFF);
}

void hardDrop(bool awardScore = true) {
    Piece dropped = current;
    while (true) {
        Piece next = dropped;
        next.y++;
        if (!validPiece(next)) break;
        dropped = next;
    }
    current = dropped;
    if (awardScore) addScoreSafe(2);
    bakeAndSpawn();
    playSound(SND_SHOOT);
}

bool runBot(unsigned long now) {
    unsigned long actionMs = tetrisFastBotMode ? FAST_BOT_ACTION_MS : BOT_ACTION_MS;
    if (now - lastBotActionMs < actionMs) return false;
    lastBotActionMs = now;
    if ((current.rot & 3) != (botTargetRot & 3)) {
        botAlignedSinceMs = 0;
        if (rotatePiece()) return false;

        // Manche Stellungen (besonders der lange I-Stein nahe am oberen
        // Stapel) lassen sich erst nach einer seitlichen Bewegung drehen.
        if (current.x < botTargetX && movePiece(1)) return false;
        if (current.x > botTargetX && movePiece(-1)) return false;

        // Ist auch der Ausweichschritt blockiert, wird nicht endlos derselbe
        // Zug versucht. Der Stein faellt weiter und kann sauber Game-over
        // ausloesen.
        botTargetRot = current.rot;
        botTargetX = current.x;
        botAlignedSinceMs = now;
        return false;
    }
    if (current.x < botTargetX) {
        botAlignedSinceMs = 0;
        movePiece(1);
        return false;
    }
    if (current.x > botTargetX) {
        botAlignedSinceMs = 0;
        movePiece(-1);
        return false;
    }
    if (tetrisFastBotMode) {
        botAlignedSinceMs = 0;
        hardDrop(false);
        return true;
    }
    if (botAlignedSinceMs == 0) {
        botAlignedSinceMs = now;
        return false;
    }
    if (now - botAlignedSinceMs < BOT_DROP_DELAY_MS) return false;
    botAlignedSinceMs = 0;
    hardDrop(false);
    return true;
}

void fallOne() {
    Piece moved = current;
    moved.y++;
    if (validPiece(moved)) {
        current = moved;
    } else {
        bakeAndSpawn();
    }
}

void handleButton(unsigned long now) {
    bool pressed = joystick1.isPressed();
    if (pressed && !lastButton) {
        buttonDownMs = now;
        holdTriggered = false;
    }
    if (pressed && !holdTriggered && now - buttonDownMs >= BUTTON_HOLD_MS) {
        holdPiece();
        holdTriggered = true;
    }
    if (!pressed && lastButton && !holdTriggered) rotatePiece();
    lastButton = pressed;

    joystick1.einfacherKlickZaehler = 0;
    joystick1.doppelklickZaehler = 0;
    joystick1.langKlickZaehler = 0;
}

void handleStick(unsigned long now, const GameStickState& stick,
                 bool& softDrop, bool& didHardDrop) {
    int horizontal = stick.y;
    int vertical = stick.x;

    if (now - lastMoveMs >= MOVE_REPEAT_MS) {
        if (horizontal > INPUT_DEADZONE) {
            movePiece(1);
            lastMoveMs = now;
        } else if (horizontal < -INPUT_DEADZONE) {
            movePiece(-1);
            lastMoveMs = now;
        }
    }

    softDrop = vertical < -INPUT_DEADZONE;
    bool upHeld = vertical > INPUT_DEADZONE;
    if (upHeld && !lastUpHeld) {
        hardDrop();
        didHardDrop = true;
    }
    lastUpHeld = upHeld;
}

void drawCell(int fieldX, int fieldY, CRGB color) {
    int lx = FIELD_X + fieldX;
    int ly = FIELD_Y + fieldY;
    setRotatedPixel(lx, ly, color);
}

void drawPieceAt(const Piece& p, CRGB color) {
    for (int i = 0; i < 4; i++) {
        int x, y;
        pieceCell(p, i, x, y);
        if (x >= 0 && x < FIELD_W && y >= 0 && y < FIELD_H) drawCell(x, y, color);
    }
}

Piece landingPiece() {
    Piece ghost = current;
    while (true) {
        Piece next = ghost;
        next.y++;
        if (!validPiece(next)) break;
        ghost = next;
    }
    return ghost;
}

void drawGhostPiece(unsigned long now) {
    Piece ghost = landingPiece();
    if (ghost.y == current.y) return;
    CRGB ghostColor = pieceColors[current.type];
    ghostColor.nscale8_video(((now / 260) & 1) ? 82 : 45);
    drawPieceAt(ghost, ghostColor);
}

void drawPreviewPiece(int8_t type, int ox, int oy, bool dim) {
    if (type < 0 || type >= 7) return;
    CRGB color = pieceColors[type];
    if (dim) color.nscale8_video(95);
    Piece p = {type, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
        int x, y;
        pieceCell(p, i, x, y);
        setRotatedPixel(ox + x, oy + y, color);
    }
}

void drawHold() {
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            setRotatedPixel(HOLD_X + x, HOLD_Y + y, CRGB(8, 14, 26));
    drawPreviewPiece(holdType, HOLD_X, HOLD_Y, false);
}

void drawNext() {
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            setRotatedPixel(NEXT_X + x, NEXT_Y + y, CRGB(8, 14, 26));
    drawPreviewPiece(nextType, NEXT_X, NEXT_Y, false);
}

void renderPlaying(bool showFrame = true) {
    FastLED.clear();
    drawHold();
    drawNext();

    for (int y = 0; y < FIELD_H; y++) {
        for (int x = 0; x < FIELD_W; x++) {
            if (board[y][x] != 0) drawCell(x, y, pieceColors[board[y][x] - 1]);
            else drawCell(x, y, CRGB(3, 4, 8));
        }
    }
    for (int y = 0; y < FIELD_H; y++) {
        setRotatedPixel(FIELD_X - 1, FIELD_Y + y, CRGB(20, 45, 70));
        setRotatedPixel(FIELD_X + FIELD_W, FIELD_Y + y, CRGB(20, 45, 70));
    }

    drawGhostPiece(millis());
    drawPieceAt(current, pieceColors[current.type]);
    syncDashboardScore();
    drawNumberSmall(visibleScore(), SCORE_X, SCORE_Y, CRGB(120, 180, 255));
    if (showFrame) FastLED.show();
}

void renderMenu() {
    FastLED.clear();
    unsigned long now = millis();

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            setRotatedPixel(HOLD_X + x, HOLD_Y + y, CRGB(8, 14, 26));
            setRotatedPixel(NEXT_X + x, NEXT_Y + y, CRGB(8, 14, 26));
        }
    }
    drawPreviewPiece((now / 900) % 7, HOLD_X, HOLD_Y, true);
    drawPreviewPiece((now / 700 + 3) % 7, NEXT_X, NEXT_Y, false);

    for (int y = 0; y < FIELD_H; y++) {
        for (int x = 0; x < FIELD_W; x++) {
            if (((x + y) & 3) == 0) drawCell(x, y, CRGB(4, 7, 14));
        }
        setRotatedPixel(FIELD_X - 1, FIELD_Y + y, CRGB(15, 38, 65));
        setRotatedPixel(FIELD_X + FIELD_W, FIELD_Y + y, CRGB(15, 38, 65));
    }

    const uint8_t stack[][3] = {
        {0,19,1},{1,19,1},{2,19,4},{3,19,4},{4,19,5},{5,19,5},{6,19,6},{7,19,6},{8,19,2},{9,19,2},
        {0,18,3},{1,18,3},{4,18,5},{5,18,0},{6,18,0},{9,18,2},
        {2,17,4},{3,17,4},{7,17,6},{8,17,6},{9,17,6},
        {0,16,2},{1,16,2},{2,16,2},{5,16,3},{6,16,3}
    };
    for (size_t i = 0; i < sizeof(stack) / sizeof(stack[0]); i++) {
        drawCell(stack[i][0], stack[i][1], pieceColors[stack[i][2] % 7]);
    }

    Piece falling = {(int8_t)((now / 1100) % 7), 0, 3, (int8_t)(1 + (now / 180) % 10)};
    drawPieceAt(falling, pieceColors[falling.type]);
    FastLED.show();
}

void renderGameOver() {
    FastLED.clear();
    syncDashboardScore();
    drawNumberSmall(visibleScore(), 0, 8, CRGB::Red);
    for (int y = 20; y < 28; y++)
        for (int x = 4; x < 12; x++)
            if (((x + y) & 1) == 0) setRotatedPixel(x, y, CRGB(60, 0, 0));
    FastLED.show();
}

void tickTetris() {
    unsigned long now = millis();
    GameControlEvents controls = runtime.poll(joystick1, joystick3);
    if (controls.primaryDoubleClick && !humanControlled) {
        humanControlled = true;
        humanInputArmed = false;
        lastUpHeld = false;
        lastButton = false;
        playSound(SND_POWERUP);
    }
    joystick1.einfacherKlickZaehler = 0;
    joystick3.einfacherKlickZaehler = 0;
    if (controls.pauseToggle && (state == GAME_PLAYING || state == GAME_PAUSED))
        runtime.applyPause(state);

    if (state == GAME_MENU) {
        tetrisPlaying = false;
        renderMenu();
        return;
    }

    if (state == GAME_COUNTDOWN) {
        tetrisPlaying = false;
        // Nur ein einziges Display-Update pro Frame verhindert das Flackern
        // zwischen Spielfeld und Countdown-Zahl.
        renderPlaying(false);
        unsigned long elapsed = now - countdownStartedMs;
        runtime.drawCountdownOverlay(3 - min(2UL, elapsed / 700UL));
        FastLED.show();
        if (elapsed >= 2100UL) {
            state = GAME_PLAYING;
            lastFallMs = now;
        }
        return;
    }

    if (state == GAME_PAUSED) {
        tetrisPlaying = false;
        runtime.drawPauseOverlay();
        return;
    }

    if (state == GAME_GAME_OVER) {
        tetrisPlaying = false;
        renderGameOver();
        if (now - gameOverStartedMs >= 1800UL) {
            runtime.animateTransition(CRGB::Blue);
            resetGame();
        }
        return;
    }

    bool softDrop = false;
    bool didHardDrop = false;
    if (humanControlled) {
        if (!humanInputArmed) {
            bool neutral = abs(controls.primary.x) <= INPUT_DEADZONE &&
                           abs(controls.primary.y) <= INPUT_DEADZONE &&
                           !joystick1.isPressed();
            if (neutral) humanInputArmed = true;
        } else {
            handleButton(now);
            handleStick(now, controls.primary, softDrop, didHardDrop);
        }
    } else {
        didHardDrop = runBot(now);
    }

    if (!didHardDrop) {
        unsigned long interval = softDrop ? constrain((int)tetrisSoftFallMs, 20, 500) : currentFallInterval();
        if (now - lastFallMs >= interval) {
            fallOne();
            lastFallMs = now;
            if (softDrop) addScoreSafe(1);
        }
    }

    if (state == GAME_GAME_OVER) {
        renderGameOver();
        return;
    }

    renderPlaying();
}
}

void renderTetrisMenuPreview() {
    renderMenu();
}

void taskTetrisHandler(void *pvParameters) {
    int lastTask = -1;

    for (;;) {
        if (aktiverTask != 9) {
            feedRuntimeWatchdog();
            tetrisPlaying = false;
            lastTask = -1;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (lastTask != 9) {
            humanControlled = false;
            humanInputArmed = false;
            runtime.begin();
            joystick1.reset();
            joystick3.reset();
            resetGame();
            lastTask = 9;
            Serial.println("Tetris gestartet.");
        }

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        if (aktiverTask == 9) tickTetris();

        unlockDisplay();
        runtime.waitForNextFrame(16);
    }
}
