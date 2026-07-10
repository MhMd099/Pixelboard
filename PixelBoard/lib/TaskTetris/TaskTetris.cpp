#include "TaskTetris.h"

#include <Arduino.h>
#include <FastLED.h>

#include "Config.h"
#include "HardwareUtils.h"
#include "Joystick.h"
#include "SoundUtils.h"

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
const int HOLD_Y = 0;
const int SCORE_X = 4;
const int SCORE_Y = 0;
const int NEXT_X = 12;
const int NEXT_Y = 5;
const int INPUT_DEADZONE = 62;
const unsigned long MOVE_REPEAT_MS = 125;
const unsigned long BUTTON_HOLD_MS = 700;

enum TetrisState {
    TETRIS_MENU,
    TETRIS_PLAYING,
    TETRIS_GAMEOVER
};

struct Piece {
    int8_t type;
    int8_t rot;
    int8_t x;
    int8_t y;
};

TetrisState state = TETRIS_MENU;
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
bool scoreSaved = false;
unsigned long buttonDownMs = 0;

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
    if (value > 999) value = 999;
    char buf[4];
    buf[0] = '0' + (value / 100) % 10;
    buf[1] = '0' + (value / 10) % 10;
    buf[2] = '0' + value % 10;
    buf[3] = '\0';
    int start = value >= 100 ? 0 : (value >= 10 ? 1 : 2);
    for (int i = start; i < 3; i++) drawLogicalChar(x + (i - start) * 4, y, buf[i], color);
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

int8_t randomPiece() {
    return (int8_t)random(0, 7);
}

uint32_t visibleScore() {
    return score % 1000UL;
}

void syncDashboardScore() {
    tetrisCurrentScore = score;
    tetrisBoardScore = visibleScore();
    tetrisPlaying = state == TETRIS_PLAYING;
}

unsigned long currentFallInterval() {
    uint16_t normalMs = constrain((int)tetrisNormalFallMs, 120, 1500);
    uint16_t minMs = constrain((int)tetrisMinFallMs, 80, normalMs);
    uint16_t scoreStep = constrain((int)tetrisSpeedScoreStep, 50, 2000);
    uint16_t speedStep = constrain((int)tetrisSpeedStepMs, 0, 250);
    uint32_t steps = score / scoreStep;
    unsigned long reduction = steps * (unsigned long)speedStep;
    if (reduction >= normalMs - minMs) return minMs;
    return normalMs - reduction;
}

void saveTetrisScoreIfNeeded() {
    if (scoreSaved || score == 0) return;
    String user = currentUser;
    user.trim();
    if (user == "") user = "Gast";
    saveTetrisHighScore(user, (int)min(score, (uint32_t)2147483647UL));
    tetrisLastScore = score;
    scoreSaved = true;
}

void spawnPiece(int8_t type) {
    current.type = type;
    current.rot = 0;
    current.x = 3;
    current.y = 0;
    holdUsed = false;
    if (!validPiece(current)) {
        state = TETRIS_GAMEOVER;
        playSound(SND_DIE);
    }
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
    scoreSaved = false;
    buttonDownMs = 0;
    state = TETRIS_PLAYING;
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
        linesCleared += cleared;
        score += (cleared == 1 ? 100 : cleared == 2 ? 300 : cleared == 3 ? 500 : 800);
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

void movePiece(int dx) {
    Piece moved = current;
    moved.x += dx;
    if (validPiece(moved)) {
        current = moved;
        playSound(SND_UI);
    }
}

void rotatePiece() {
    Piece rotated = current;
    rotated.rot = (rotated.rot + 1) & 3;
    if (!validPiece(rotated)) {
        rotated.x--;
        if (!validPiece(rotated)) {
            rotated.x += 2;
            if (!validPiece(rotated)) return;
        }
    }
    current = rotated;
    playSound(SND_SELECT);
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

void hardDrop() {
    Piece dropped = current;
    while (true) {
        Piece next = dropped;
        next.y++;
        if (!validPiece(next)) break;
        dropped = next;
    }
    current = dropped;
    score += 2;
    bakeAndSpawn();
    playSound(SND_SHOOT);
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

void handleStick(unsigned long now, bool& softDrop, bool& didHardDrop) {
    int horizontal = joystick1.readYPercent();
    int vertical = joystick1.readXPercent();

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

void renderPlaying() {
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
    FastLED.show();
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
    drawNumberSmall(visibleScore(), 3, 8, CRGB::Red);
    for (int y = 20; y < 28; y++)
        for (int x = 4; x < 12; x++)
            if (((x + y) & 1) == 0) setRotatedPixel(x, y, CRGB(60, 0, 0));
    FastLED.show();
}

void tickTetris() {
    unsigned long now = millis();
    bool startDoubleClick = joystick1.doppelklickZaehler > 0 ||
                            joystick3.doppelklickZaehler > 0;
    joystick1.einfacherKlickZaehler = 0;
    joystick1.doppelklickZaehler = 0;
    joystick1.langKlickZaehler = 0;
    joystick3.einfacherKlickZaehler = 0;
    joystick3.doppelklickZaehler = 0;
    joystick3.langKlickZaehler = 0;

    if (state == TETRIS_MENU) {
        tetrisPlaying = false;
        renderMenu();
        if (startDoubleClick) resetGame();
        return;
    }

    if (state == TETRIS_GAMEOVER) {
        tetrisPlaying = false;
        saveTetrisScoreIfNeeded();
        renderGameOver();
        if (startDoubleClick) resetGame();
        return;
    }

    handleButton(now);
    bool softDrop = false;
    bool didHardDrop = false;
    handleStick(now, softDrop, didHardDrop);

    if (!didHardDrop) {
        unsigned long interval = softDrop ? constrain((int)tetrisSoftFallMs, 20, 500) : currentFallInterval();
        if (now - lastFallMs >= interval) {
            fallOne();
            lastFallMs = now;
            if (softDrop) score++;
        }
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
            tetrisPlaying = false;
            lastTask = -1;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (lastTask != 9) {
            state = TETRIS_MENU;
            joystick1.reset();
            joystick3.reset();
            lastTask = 9;
            Serial.println("Tetris gestartet.");
        }

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        if (aktiverTask == 9) tickTetris();

        unlockDisplay();
        bool soft = state == TETRIS_PLAYING && joystick1.readXPercent() < -INPUT_DEADZONE;
        vTaskDelay(pdMS_TO_TICKS(soft ? 8 : 16));
    }
}
