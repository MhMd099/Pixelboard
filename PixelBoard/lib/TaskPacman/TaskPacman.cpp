#include "TaskPacman.h"

#include <Arduino.h>
#include <FastLED.h>
#include <string.h>

#include "Config.h"
#include "HardwareUtils.h"
#include "Joystick.h"
#include "SoundUtils.h"

extern volatile int aktiverTask;
extern Joystick joystick1;
extern Joystick joystick2;
extern Joystick joystick3;

namespace {
const int WIDTH = 32;
const int HEIGHT = 16;
const int PLAYER_COUNT = 2;
const int GHOST_COUNT = 4;
const int DEADZONE = 60;
const unsigned long PLAYER_STEP_MS = 145;
const unsigned long GHOST_STEP_MS = 220;
const unsigned long GHOST_AUTO_STEP_MS = 430;
const unsigned long GHOST_SPRINT_STEP_MS = 105;
const unsigned long GHOST_SWITCH_MS = 200;
const unsigned long GHOST_SPRINT_MS = 900;
const unsigned long GHOST_SPRINT_COOLDOWN_MS = 3200;
const unsigned long POWER_MS = 7000;

enum Tile : uint8_t {
    TILE_EMPTY = 0,
    TILE_WALL = 1,
    TILE_DOT = 2,
    TILE_POWER = 3
};

enum PacmanState {
    PAC_LOBBY,
    PAC_COUNTDOWN,
    PAC_PLAYING,
    PAC_GAMEOVER
};

struct Vec {
    int8_t x;
    int8_t y;
};

struct Player {
    int8_t x;
    int8_t y;
    Vec dir;
    Vec wanted;
    bool active;
    bool alive;
    int score;
    CRGB color;
};

struct Ghost {
    int8_t x;
    int8_t y;
    int8_t startX;
    int8_t startY;
    Vec dir;
    unsigned long lastMoveMs;
    unsigned long respawnUntilMs;
    CRGB color;
};

PacmanState state = PAC_LOBBY;
Tile mapTiles[HEIGHT][WIDTH];
Player players[PLAYER_COUNT];
Ghost ghosts[GHOST_COUNT];
uint8_t activeGhostIndex = 0;
int dotsLeft = 0;
int cachedBestScore = 0;
bool highscoreSaved = false;
bool lastStartPressed = false;
unsigned long stateStartedMs = 0;
unsigned long lastPlayerMoveMs = 0;
unsigned long lastGhostSwitchMs = 0;
unsigned long frightenedUntilMs = 0;
unsigned long sprintUntilMs = 0;
unsigned long sprintCooldownUntilMs = 0;

Vec makeVec(int8_t x, int8_t y) {
    Vec v = {x, y};
    return v;
}

bool samePos(int x1, int y1, int x2, int y2) {
    return x1 == x2 && y1 == y2;
}

bool inBounds(int x, int y) {
    return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT;
}

bool isWall(int x, int y) {
    if (!inBounds(x, y)) return true;
    return mapTiles[y][x] == TILE_WALL;
}

void setWall(int x, int y) {
    if (inBounds(x, y)) mapTiles[y][x] = TILE_WALL;
}

void clearSpawnTiles() {
    const int8_t clearList[][2] = {
        {1, 1}, {2, 1}, {3, 1}, {1, 2}, {2, 2},
        {29, 14}, {30, 14}, {28, 14}, {30, 13}, {29, 13},
        {14, 7}, {15, 7}, {16, 7}, {17, 7}, {18, 7}, {15, 8}, {16, 8}, {17, 8}
    };
    for (size_t i = 0; i < sizeof(clearList) / sizeof(clearList[0]); i++) {
        int x = clearList[i][0];
        int y = clearList[i][1];
        if (inBounds(x, y)) mapTiles[y][x] = TILE_EMPTY;
    }
}

void buildMap() {
    dotsLeft = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (x == 0 || x == WIDTH - 1 || y == 0 || y == HEIGHT - 1) mapTiles[y][x] = TILE_WALL;
            else mapTiles[y][x] = TILE_DOT;
        }
    }

    for (int y = 2; y < 14; y++) {
        if (y != 3 && y != 7 && y != 11) setWall(7, y);
        if (y != 4 && y != 8 && y != 12) setWall(16, y);
        if (y != 5 && y != 9 && y != 13) setWall(24, y);
    }
    for (int x = 3; x < 29; x++) {
        if (x != 5 && x != 12 && x != 20 && x != 27) setWall(x, 4);
        if (x != 4 && x != 10 && x != 18 && x != 26) setWall(x, 8);
        if (x != 6 && x != 14 && x != 22 && x != 28) setWall(x, 12);
    }

    clearSpawnTiles();
    mapTiles[1][1] = TILE_POWER;
    mapTiles[1][30] = TILE_POWER;
    mapTiles[14][1] = TILE_POWER;
    mapTiles[14][30] = TILE_POWER;

    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (mapTiles[y][x] == TILE_DOT || mapTiles[y][x] == TILE_POWER) dotsLeft++;
        }
    }
}

Vec readGridDir(Joystick& stick) {
    int xPerc = -stick.readXPercent();
    int yPerc = -stick.readYPercent();

    if (abs(yPerc) > abs(xPerc)) {
        if (yPerc > DEADZONE) return makeVec(1, 0);
        if (yPerc < -DEADZONE) return makeVec(-1, 0);
    } else {
        if (xPerc < -DEADZONE) return makeVec(0, 1);
        if (xPerc > DEADZONE) return makeVec(0, -1);
    }
    return makeVec(0, 0);
}

int manhattan(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

bool canMove(int x, int y, Vec dir) {
    return !isWall(x + dir.x, y + dir.y);
}

void moveEntity(int8_t& x, int8_t& y, Vec dir) {
    if (canMove(x, y, dir)) {
        x += dir.x;
        y += dir.y;
    }
}

void initPlayers() {
    players[0].x = 2;
    players[0].y = 1;
    players[0].dir = makeVec(1, 0);
    players[0].wanted = makeVec(1, 0);
    players[0].active = pacmanP1User != "";
    players[0].alive = players[0].active;
    players[0].score = 0;
    players[0].color = CRGB::Yellow;

    players[1].x = 29;
    players[1].y = 14;
    players[1].dir = makeVec(-1, 0);
    players[1].wanted = makeVec(-1, 0);
    players[1].active = pacmanP2User != "";
    players[1].alive = players[1].active;
    players[1].score = 0;
    players[1].color = CRGB(80, 255, 80);
}

void initGhosts() {
    const int8_t starts[GHOST_COUNT][2] = {{15, 7}, {17, 7}, {15, 8}, {17, 8}};
    const CRGB colors[GHOST_COUNT] = {CRGB::Red, CRGB::HotPink, CRGB::Cyan, CRGB::Orange};
    for (int i = 0; i < GHOST_COUNT; i++) {
        ghosts[i].x = starts[i][0];
        ghosts[i].y = starts[i][1];
        ghosts[i].startX = starts[i][0];
        ghosts[i].startY = starts[i][1];
        ghosts[i].dir = makeVec(i % 2 == 0 ? -1 : 1, 0);
        ghosts[i].lastMoveMs = 0;
        ghosts[i].respawnUntilMs = 0;
        ghosts[i].color = colors[i];
    }
    activeGhostIndex = 0;
}

bool hasAnyPlayer() {
    return pacmanP1User != "" || pacmanP2User != "";
}

bool anyPlayerAlive() {
    for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
        if (players[i].active && players[i].alive) return true;
    }
    return false;
}

uint8_t primaryTargetIndex(uint8_t ghostIdx) {
    uint8_t best = 0;
    int bestDist = 32767;
    bool found = false;
    for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
        if (!players[i].active || !players[i].alive) continue;
        int d = manhattan(ghosts[ghostIdx].x, ghosts[ghostIdx].y, players[i].x, players[i].y);
        if (!found || d < bestDist) {
            best = i;
            bestDist = d;
            found = true;
        }
    }
    return best;
}

void resetGame() {
    buildMap();
    initPlayers();
    initGhosts();
    highscoreSaved = false;
    frightenedUntilMs = 0;
    sprintUntilMs = 0;
    sprintCooldownUntilMs = 0;
    lastPlayerMoveMs = 0;
    lastGhostSwitchMs = 0;
}

void consumeTile(Player& player, unsigned long now) {
    Tile& tile = mapTiles[player.y][player.x];
    if (tile == TILE_DOT) {
        tile = TILE_EMPTY;
        dotsLeft--;
        player.score += 10;
        playSound(SND_EAT);
    } else if (tile == TILE_POWER) {
        tile = TILE_EMPTY;
        dotsLeft--;
        player.score += 50;
        frightenedUntilMs = now + POWER_MS;
        playSound(SND_BUFF);
    }
}

void updateOnePlayer(Player& player, Joystick& stick, unsigned long now) {
    if (!player.active || !player.alive) return;

    Vec input = readGridDir(stick);
    if (input.x != 0 || input.y != 0) player.wanted = input;

    if (canMove(player.x, player.y, player.wanted)) player.dir = player.wanted;
    moveEntity(player.x, player.y, player.dir);
    consumeTile(player, now);
}

void updatePlayers(unsigned long now) {
    if (now - lastPlayerMoveMs < PLAYER_STEP_MS) return;
    lastPlayerMoveMs = now;
    updateOnePlayer(players[0], joystick1, now);
    updateOnePlayer(players[1], joystick3, now);
}

Vec targetForGhost(uint8_t idx) {
    if (frightenedUntilMs > millis()) {
        int cornerX = (idx == 0 || idx == 2) ? 30 : 1;
        int cornerY = (idx < 2) ? 1 : 14;
        return makeVec(cornerX, cornerY);
    }

    uint8_t targetIdx = primaryTargetIndex(idx);
    Player& target = players[targetIdx];
    if (idx == 0) return makeVec(target.x, target.y);
    if (idx == 1) return makeVec(target.x + target.dir.x * 3, target.y + target.dir.y * 3);
    if (idx == 2) return makeVec(WIDTH - 1 - target.x, target.y);

    if (manhattan(ghosts[idx].x, ghosts[idx].y, target.x, target.y) < 6) return makeVec(1, 14);
    return makeVec(target.x, target.y);
}

Vec chooseAutoDir(uint8_t idx) {
    static const Vec dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    Ghost& g = ghosts[idx];
    Vec target = targetForGhost(idx);
    int bestScore = 32767;
    Vec best = g.dir;
    Vec reverse = makeVec(-g.dir.x, -g.dir.y);

    for (int i = 0; i < 4; i++) {
        Vec d = dirs[i];
        if (!canMove(g.x, g.y, d)) continue;
        if ((d.x == reverse.x && d.y == reverse.y) && canMove(g.x, g.y, g.dir)) continue;
        int dist = manhattan(g.x + d.x, g.y + d.y, target.x, target.y);
        if (dist < bestScore) {
            bestScore = dist;
            best = d;
        }
    }
    return best;
}

void cycleGhost(unsigned long now) {
    if (now - lastGhostSwitchMs < GHOST_SWITCH_MS) return;
    activeGhostIndex = (activeGhostIndex + 1) % GHOST_COUNT;
    lastGhostSwitchMs = now;
    playSound(SND_SELECT);
}

void readGhostClicks(unsigned long now) {
    if (!pacmanGhostManual) {
        joystick2.einfacherKlickZaehler = 0;
        joystick2.langKlickZaehler = 0;
        return;
    }

    int clicks = joystick2.einfacherKlickZaehler;
    int holds = joystick2.langKlickZaehler;
    joystick2.einfacherKlickZaehler = 0;
    joystick2.langKlickZaehler = 0;

    if (clicks > 0) cycleGhost(now);
    if (holds > 0 && now >= sprintCooldownUntilMs) {
        sprintUntilMs = now + GHOST_SPRINT_MS;
        sprintCooldownUntilMs = now + GHOST_SPRINT_COOLDOWN_MS;
        playSound(SND_SHOOT);
    }
}

void updateControlledGhost(unsigned long now) {
    Ghost& g = ghosts[activeGhostIndex];
    if (now < g.respawnUntilMs) return;

    unsigned long interval = (now < sprintUntilMs) ? GHOST_SPRINT_STEP_MS : GHOST_STEP_MS;
    if (now - g.lastMoveMs < interval) return;

    Vec input = readGridDir(joystick2);
    if (input.x != 0 || input.y != 0) g.dir = input;
    moveEntity(g.x, g.y, g.dir);
    g.lastMoveMs = now;
}

void updateAutoGhost(uint8_t idx, unsigned long now) {
    Ghost& g = ghosts[idx];
    if (now < g.respawnUntilMs) return;
    if (pacmanGhostManual && idx == activeGhostIndex) return;
    if (now - g.lastMoveMs < GHOST_AUTO_STEP_MS) return;

    g.dir = chooseAutoDir(idx);
    moveEntity(g.x, g.y, g.dir);
    g.lastMoveMs = now;
}

void updateGhosts(unsigned long now) {
    readGhostClicks(now);
    if (pacmanGhostManual) updateControlledGhost(now);
    for (uint8_t i = 0; i < GHOST_COUNT; i++) updateAutoGhost(i, now);
}

void respawnGhost(uint8_t idx, unsigned long now) {
    ghosts[idx].x = ghosts[idx].startX;
    ghosts[idx].y = ghosts[idx].startY;
    ghosts[idx].dir = makeVec(idx % 2 == 0 ? -1 : 1, 0);
    ghosts[idx].respawnUntilMs = now + 1500UL;
}

void checkCollisions(unsigned long now) {
    for (uint8_t gIdx = 0; gIdx < GHOST_COUNT; gIdx++) {
        Ghost& g = ghosts[gIdx];
        if (now < g.respawnUntilMs) continue;

        for (uint8_t pIdx = 0; pIdx < PLAYER_COUNT; pIdx++) {
            Player& p = players[pIdx];
            if (!p.active || !p.alive) continue;
            if (!samePos(p.x, p.y, g.x, g.y)) continue;

            if (now < frightenedUntilMs) {
                p.score += 200;
                respawnGhost(gIdx, now);
                playSound(SND_EXPLOSION);
            } else {
                p.alive = false;
                playSound(SND_DIE);
            }
        }
    }

    if (!anyPlayerAlive()) {
        state = PAC_GAMEOVER;
        stateStartedMs = now;
    }
}

void saveScoreIfNeeded() {
    if (highscoreSaved) return;
    if (players[0].active && pacmanP1User != "") savePacmanHighScore(pacmanP1User, players[0].score);
    if (players[1].active && pacmanP2User != "" && pacmanP2User != pacmanP1User) savePacmanHighScore(pacmanP2User, players[1].score);
    cachedBestScore = max(cachedBestScore, players[0].score);
    cachedBestScore = max(cachedBestScore, players[1].score);
    highscoreSaved = true;
}

void drawNumber(int value, int x, int y, CRGB color) {
    value = constrain(value, 0, 999);
    if (value >= 100) {
        drawDigitW(x, y, value / 100, color);
        drawDigitW(x + 4, y, (value / 10) % 10, color);
        drawDigitW(x + 8, y, value % 10, color);
    } else if (value >= 10) {
        drawDigitW(x + 2, y, value / 10, color);
        drawDigitW(x + 6, y, value % 10, color);
    } else {
        drawDigitW(x + 5, y, value, color);
    }
}

void renderWord(const char* word, int x, int y, CRGB color) {
    for (int i = 0; word[i] != '\0'; i++) drawChar3x5(x + i * 4, y, word[i], color);
}

void renderMap() {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            Tile tile = mapTiles[y][x];
            if (tile == TILE_WALL) setPixel(x, y, CRGB(0, 28, 90));
            else if (tile == TILE_DOT) setPixel(x, y, CRGB(45, 38, 18));
            else if (tile == TILE_POWER) setPixel(x, y, CRGB::White);
        }
    }
}

void renderGame(unsigned long now) {
    FastLED.clear();
    renderMap();

    for (uint8_t i = 0; i < PLAYER_COUNT; i++) {
        Player& p = players[i];
        if (!p.active) continue;
        CRGB c = p.alive ? p.color : CRGB(45, 45, 45);
        if (i == 0 && (now / 140) % 2) c = CRGB(255, 180, 0);
        setPixel(p.x, p.y, c);
    }

    for (uint8_t i = 0; i < GHOST_COUNT; i++) {
        Ghost& g = ghosts[i];
        if (now < g.respawnUntilMs && (now / 160) % 2) continue;

        CRGB col = (now < frightenedUntilMs) ? CRGB(40, 70, 255) : g.color;
        if (pacmanGhostManual && i == activeGhostIndex && (now / 180) % 2 == 0) col = CRGB::White;
        setPixel(g.x, g.y, col);
    }

    drawNumber(players[0].score / 10, 1, 10, CRGB::Yellow);
    if (players[1].active) drawNumber(players[1].score / 10, 20, 10, CRGB(80, 255, 80));
}

void renderLobby(unsigned long now) {
    FastLED.clear();
    renderWord("PAC", 2, 2, themeCol(now / 18, 255));
    renderWord(pacmanGhostManual ? "MAN" : "AUT", 17, 2, pacmanGhostManual ? CRGB::Red : CRGB::Cyan);
    if (!hasAnyPlayer()) {
        renderWord("WEB", 2, 10, CRGB::Orange);
    } else {
        drawNumber(cachedBestScore / 10, 10, 10, CRGB(80, 160, 255));
    }
}

void renderCountdown(unsigned long now) {
    renderGame(now);
    int left = 3 - (int)((now - stateStartedMs) / 700);
    if (left < 1) left = 1;
    drawDigitW(14, 5, left, CRGB::Yellow);
}

void renderGameOver() {
    FastLED.clear();
    renderWord("END", 2, 1, CRGB::Red);
    drawNumber(max(players[0].score, players[1].score) / 10, 10, 9, CRGB::Yellow);
}

void beginCountdown() {
    resetGame();
    state = PAC_COUNTDOWN;
    stateStartedMs = millis();
    playSound(SND_SELECT);
}

void tickPacman() {
    //ich musste es hier selber einfügenb, weil der joystick i2c invertiert war
    joystick3.setInverted(true,true);
        //ich musste es hier selber einfügenb, weil der joystick invertiert war

    joystick1.setInverted(true,true);
    //es geht aber nicht, i2c funktioniert,aber joystick eins ist immer ncoh invertiert
    unsigned long now = millis();
    bool startPressed = joystick1.isPressed() || joystick2.isPressed() || joystick3.isPressed();
    bool startEdge = startPressed && !lastStartPressed;
    lastStartPressed = startPressed;

    switch (state) {
    case PAC_LOBBY:
        renderLobby(now);
        if (startEdge && hasAnyPlayer()) beginCountdown();
        break;

    case PAC_COUNTDOWN:
        renderCountdown(now);
        if (now - stateStartedMs >= 2100UL) {
            state = PAC_PLAYING;
            stateStartedMs = now;
            lastPlayerMoveMs = now;
            for (uint8_t i = 0; i < GHOST_COUNT; i++) ghosts[i].lastMoveMs = now;
        }
        break;

    case PAC_PLAYING:
        updatePlayers(now);
        updateGhosts(now);
        checkCollisions(now);
        if (dotsLeft <= 0) {
            players[0].score += players[0].active ? 250 : 0;
            players[1].score += players[1].active ? 250 : 0;
            state = PAC_GAMEOVER;
            stateStartedMs = now;
            playSound(SND_BUFF);
        }
        if (state == PAC_GAMEOVER) saveScoreIfNeeded();
        renderGame(now);
        break;

    case PAC_GAMEOVER:
        saveScoreIfNeeded();
        renderGameOver();
        if (startEdge) {
            state = PAC_LOBBY;
        }
        break;
    }

    FastLED.show();
}

void refreshCachedBest() {
    cachedBestScore = 0;
    if (pacmanP1User != "") cachedBestScore = max(cachedBestScore, getPacmanHighScore(pacmanP1User));
    if (pacmanP2User != "") cachedBestScore = max(cachedBestScore, getPacmanHighScore(pacmanP2User));
}
}

void taskPacmanHandler(void *pvParameters) {
    int lastTask = -1;

    for (;;) {
        if (aktiverTask != 8) {
            lastTask = -1;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (lastTask != 8) {
            state = PAC_LOBBY;
            resetGame();
            refreshCachedBest();
            joystick2.einfacherKlickZaehler = 0;
            joystick2.langKlickZaehler = 0;
            lastTask = 8;
            Serial.println("Pacman gestartet.");
        }

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        if (aktiverTask == 8) tickPacman();

        unlockDisplay();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
