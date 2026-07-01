#include "TaskPong.h"

#include <Arduino.h>
#include <FastLED.h>
#include <string.h>

#include "Config.h"
#include "HardwareUtils.h"
#include "Joystick.h"
#include "SoundUtils.h"

extern volatile int aktiverTask;
extern Joystick joystick1;
extern Joystick joystick3;

namespace {
const int WIDTH = 32;
const int HEIGHT = 16;
const int SCALE = 256;
const int PADDLE_H = 4;
const int LEFT_X = 1;
const int RIGHT_X = 30;
const int INPUT_DEADZONE = 60;
const int INPUT_STEP_MS = 55;
const int BOT_STEP_MS = 95;
const int INITIAL_VX = 84;
const int MAX_VX = 240;
const int MAX_VY = 185;
const int WIN_SCORE = 7;

enum PongState {
    PONG_MENU,
    PONG_COUNTDOWN,
    PONG_PLAYING,
    PONG_SCORE,
    PONG_GAMEOVER
};

struct Particle {
    int8_t x;
    int8_t y;
    uint8_t ttl;
    CRGB color;
};

PongState state = PONG_MENU;
int leftY = 6;
int rightY = 6;
int leftScore = 0;
int rightScore = 0;
int ballX = 16 * SCALE;
int ballY = 8 * SCALE;
int lastBallX = 16 * SCALE;
int lastBallY = 8 * SCALE;
int ballVx = INITIAL_VX;
int ballVy = 80;
int ballVxAbs = INITIAL_VX;
int lastLeftMove = 0;
int lastRightMove = 0;
int matchBestRally = 0;
int rallyHits = 0;
int serveDir = 1;
bool savedMatch = false;
bool lastStartPressed = false;
int cachedBestRally = 0;
unsigned long lastLeftInputMs = 0;
unsigned long lastRightInputMs = 0;
unsigned long lastLeftBotMs = 0;
unsigned long lastRightBotMs = 0;
unsigned long stateStartedMs = 0;
Particle particles[8];

int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

void clearParticles() {
    for (int i = 0; i < 8; i++) particles[i].ttl = 0;
}

void spawnImpact(int x, int y, int dir) {
    static const int8_t offsets[3] = {-1, 0, 1};
    for (int i = 0; i < 3; i++) {
        int slot = -1;
        for (int p = 0; p < 8; p++) {
            if (particles[p].ttl == 0) {
                slot = p;
                break;
            }
        }
        if (slot < 0) return;
        particles[slot].x = clampInt(x - dir, 0, WIDTH - 1);
        particles[slot].y = clampInt(y + offsets[(rallyHits + i) % 3], 0, HEIGHT - 1);
        particles[slot].ttl = 6 - i;
        particles[slot].color = (dir > 0) ? CRGB::Cyan : CRGB::Orange;
    }
}

void updateParticles() {
    for (int i = 0; i < 8; i++) {
        if (particles[i].ttl > 0) particles[i].ttl--;
    }
}

int readVertical(Joystick& stick) {
    int x = stick.readXPercent();
    if (x > INPUT_DEADZONE) return -1;
    if (x < -INPUT_DEADZONE) return 1;
    return 0;
}

void resetPaddles() {
    leftY = 6;
    rightY = 6;
    lastLeftMove = 0;
    lastRightMove = 0;
}

void serveBall(int dir) {
    serveDir = (dir >= 0) ? 1 : -1;
    ballX = 16 * SCALE;
    ballY = 8 * SCALE;
    lastBallX = ballX;
    lastBallY = ballY;
    ballVxAbs = INITIAL_VX;
    ballVx = ballVxAbs * serveDir;
    int vy = 32 + (int)random(0, 34);
    if (random(0, 2) == 0) vy = -vy;
    ballVy = vy;
    rallyHits = 0;
    clearParticles();
}

void resetMatch() {
    leftScore = 0;
    rightScore = 0;
    matchBestRally = 0;
    savedMatch = false;
    resetPaddles();
    serveBall(random(0, 2) == 0 ? -1 : 1);
}

void refreshCachedBest() {
    cachedBestRally = 0;
    if (pongLeftUser != "") cachedBestRally = max(cachedBestRally, getPongHighScore(pongLeftUser));
    if (pongRightUser != "") cachedBestRally = max(cachedBestRally, getPongHighScore(pongRightUser));
}

void beginCountdown() {
    state = PONG_COUNTDOWN;
    stateStartedMs = millis();
    resetPaddles();
    serveBall(serveDir);
    playSound(SND_SELECT);
}

void applyInput(unsigned long now) {
    int leftMove = 0;
    int rightMove = 0;

    if (pongLeftUser != "") leftMove = readVertical(joystick3);
    if (pongRightUser != "") rightMove = readVertical(joystick1);

    if (pongLeftUser == "" && now - lastLeftBotMs >= BOT_STEP_MS) {
        int target = (ballVx < 0) ? (ballY / SCALE) : (HEIGHT / 2);
        int center = leftY + PADDLE_H / 2;
        if (target < center - 1) leftMove = -1;
        else if (target > center + 1) leftMove = 1;
        lastLeftBotMs = now;
    }

    if (pongRightUser == "" && now - lastRightBotMs >= BOT_STEP_MS) {
        int target = (ballVx > 0) ? (ballY / SCALE) : (HEIGHT / 2);
        int center = rightY + PADDLE_H / 2;
        if (target < center - 1) rightMove = -1;
        else if (target > center + 1) rightMove = 1;
        lastRightBotMs = now;
    }

    if (leftMove != 0 && now - lastLeftInputMs >= INPUT_STEP_MS) {
        leftY = clampInt(leftY + leftMove, 0, HEIGHT - PADDLE_H);
        lastLeftInputMs = now;
        lastLeftMove = leftMove;
    } else if (leftMove == 0) {
        lastLeftMove = 0;
    }

    if (rightMove != 0 && now - lastRightInputMs >= INPUT_STEP_MS) {
        rightY = clampInt(rightY + rightMove, 0, HEIGHT - PADDLE_H);
        lastRightInputMs = now;
        lastRightMove = rightMove;
    } else if (rightMove == 0) {
        lastRightMove = 0;
    }
}

void drawNumber2(int value, int x, int y, CRGB color) {
    value = clampInt(value, 0, 99);
    if (value >= 10) {
        drawDigitW(x, y, value / 10, color);
        drawDigitW(x + 5, y, value % 10, color);
    } else {
        drawDigitW(x + 2, y, value, color);
    }
}

void drawPaddle(int x, int y, CRGB color) {
    for (int i = 0; i < PADDLE_H; i++) {
        uint8_t v = (i == 0 || i == PADDLE_H - 1) ? 150 : 255;
        CRGB c = color;
        c.nscale8_video(v);
        setPixel(x, y + i, c);
    }
}

void handlePaddleHit(bool leftPaddle, int hitY) {
    int paddleY = leftPaddle ? leftY : rightY;
    int move = leftPaddle ? lastLeftMove : lastRightMove;
    int center2 = paddleY * 2 + PADDLE_H - 1;
    int rel2 = hitY * 2 - center2;
    int targetVy = rel2 * (move == 0 ? 26 : 34) + move * 44;

    ballVxAbs = clampInt(ballVxAbs + 4, INITIAL_VX, MAX_VX);
    ballVx = leftPaddle ? ballVxAbs : -ballVxAbs;
    ballVy = clampInt(targetVy, -MAX_VY, MAX_VY);
    ballX = (leftPaddle ? (LEFT_X + 2) : (RIGHT_X - 2)) * SCALE;

    rallyHits++;
    if (rallyHits > matchBestRally) matchBestRally = rallyHits;
    spawnImpact(leftPaddle ? LEFT_X + 1 : RIGHT_X - 1, hitY, leftPaddle ? 1 : -1);
    playSound(ballVxAbs > 230 ? SND_SHOOT : SND_HIT);
}

bool paddleCovers(int paddleY, int y) {
    return y >= paddleY && y < paddleY + PADDLE_H;
}

void updateBall() {
    lastBallX = ballX;
    lastBallY = ballY;
    ballX += ballVx;
    ballY += ballVy;

    if (ballY < 0) {
        ballY = -ballY;
        ballVy = -ballVy;
    } else if (ballY > (HEIGHT - 1) * SCALE) {
        ballY = (HEIGHT - 1) * SCALE - (ballY - (HEIGHT - 1) * SCALE);
        ballVy = -ballVy;
    }

    int lastPx = lastBallX / SCALE;
    int px = ballX / SCALE;
    int hitY = clampInt(((lastBallY + ballY) / 2) / SCALE, 0, HEIGHT - 1);

    if (ballVx < 0 && lastPx > LEFT_X + 1 && px <= LEFT_X + 1 && paddleCovers(leftY, hitY)) {
        handlePaddleHit(true, hitY);
        return;
    }

    if (ballVx > 0 && lastPx < RIGHT_X - 1 && px >= RIGHT_X - 1 && paddleCovers(rightY, hitY)) {
        handlePaddleHit(false, hitY);
        return;
    }

    if (ballX < -SCALE) {
        rightScore++;
        serveDir = -1;
        state = PONG_SCORE;
        stateStartedMs = millis();
        playSound(SND_DIE);
    } else if (ballX > WIDTH * SCALE) {
        leftScore++;
        serveDir = 1;
        state = PONG_SCORE;
        stateStartedMs = millis();
        playSound(SND_DIE);
    }
}

void renderField() {
    FastLED.clear();
    for (int y = 1; y < HEIGHT; y += 3) setPixel(15, y, CRGB(20, 28, 38));

    drawNumber2(leftScore, 9, 0, CRGB(70, 90, 120));
    drawNumber2(rightScore, 18, 0, CRGB(70, 90, 120));

    drawPaddle(LEFT_X, leftY, CRGB::Cyan);
    drawPaddle(RIGHT_X, rightY, CRGB::Orange);

    int trailX = clampInt(lastBallX / SCALE, 0, WIDTH - 1);
    int trailY = clampInt(lastBallY / SCALE, 0, HEIGHT - 1);
    setPixel(trailX, trailY, CRGB(55, 55, 65));

    int px = clampInt(ballX / SCALE, 0, WIDTH - 1);
    int py = clampInt(ballY / SCALE, 0, HEIGHT - 1);
    setPixel(px, py, CRGB::White);

    for (int i = 0; i < 8; i++) {
        if (particles[i].ttl > 0) {
            CRGB c = particles[i].color;
            c.nscale8_video(45 + particles[i].ttl * 32);
            setPixel(particles[i].x, particles[i].y, c);
        }
    }
}

void renderCenteredWord(const char* word, int y, CRGB color) {
    int len = strlen(word);
    int x = (WIDTH - len * 5) / 2;
    for (int i = 0; i < len; i++) drawChar3x5(x + i * 5, y, word[i], color);
}

void renderName3(const String& name, int x, int y, CRGB color) {
    const char* text = (name == "") ? "BOT" : name.c_str();
    for (int i = 0; i < 3 && text[i] != '\0'; i++) {
        drawChar3x5(x + i * 4, y, text[i], color);
    }
}

void renderMenu() {
    FastLED.clear();
    unsigned long now = millis();
    if (pongLeftUser == "" && pongRightUser == "") {
        renderCenteredWord("LOG", 3, CRGB::Orange);
        renderCenteredWord("WEB", 9, CRGB(60, 110, 255));
    } else {
        renderName3(pongLeftUser, 1, 2, CRGB::Cyan);
        renderCenteredWord("VS", 5, themeCol(now / 18, 255));
        renderName3(pongRightUser, 19, 2, CRGB::Orange);
        drawNumber2(cachedBestRally, 12, 10, CRGB(120, 180, 255));
    }
}

void renderCountdown(unsigned long now) {
    renderField();
    int left = 3 - (int)((now - stateStartedMs) / 700);
    if (left < 1) left = 1;
    drawDigitW(14, 5, left, CRGB::Yellow);
}

void renderGameOver() {
    FastLED.clear();
    if (leftScore > rightScore) renderName3(pongLeftUser, 9, 1, CRGB::Cyan);
    else renderName3(pongRightUser, 9, 1, CRGB::Orange);
    drawNumber2(matchBestRally, 11, 9, CRGB::Yellow);
}

void saveMatchHighscores() {
    if (pongLeftUser != "") savePongHighScore(pongLeftUser, matchBestRally);
    if (pongRightUser != "" && pongRightUser != pongLeftUser) savePongHighScore(pongRightUser, matchBestRally);
    if (matchBestRally > cachedBestRally) cachedBestRally = matchBestRally;
}

void tickPong() {
    unsigned long now = millis();
    bool startPressed = joystick1.isPressed() || joystick3.isPressed();
    bool startEdge = startPressed && !lastStartPressed;
    lastStartPressed = startPressed;

    switch (state) {
    case PONG_MENU:
        renderMenu();
        if (startEdge && (pongLeftUser != "" || pongRightUser != "")) {
            resetMatch();
            beginCountdown();
        }
        break;

    case PONG_COUNTDOWN:
        applyInput(now);
        renderCountdown(now);
        if (now - stateStartedMs >= 2100) {
            state = PONG_PLAYING;
            stateStartedMs = now;
        }
        break;

    case PONG_PLAYING:
        applyInput(now);
        updateBall();
        updateParticles();
        renderField();
        break;

    case PONG_SCORE:
        updateParticles();
        renderField();
        if (leftScore >= WIN_SCORE || rightScore >= WIN_SCORE) {
            if (!savedMatch) {
                saveMatchHighscores();
                savedMatch = true;
            }
            if (now - stateStartedMs >= 900) {
                state = PONG_GAMEOVER;
                stateStartedMs = now;
            }
        } else if (now - stateStartedMs >= 900) {
            beginCountdown();
        }
        break;

    case PONG_GAMEOVER:
        renderGameOver();
        if (startEdge) {
            state = PONG_MENU;
        }
        break;
    }

    FastLED.show();
}
}

void taskPongHandler(void *pvParameters) {
    int lastTask = -1;

    for (;;) {
        if (aktiverTask != 7) {
            lastTask = -1;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (lastTask != 7) {
            state = PONG_MENU;
            resetMatch();
            refreshCachedBest();
            lastTask = 7;
            Serial.println("Pong gestartet.");
        }

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        if (aktiverTask == 7) tickPong();

        unlockDisplay();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
