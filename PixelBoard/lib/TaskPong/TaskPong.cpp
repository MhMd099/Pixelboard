#include "TaskPong.h"

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
const int WIDTH = 32;
const int HEIGHT = 16;
const int SCALE = 256;
const int PADDLE_H = 4;
const int LEFT_X = 1;
const int RIGHT_X = 30;
const int INPUT_DEADZONE = 60;
const int INPUT_STEP_MS = 55;
const int INITIAL_VX = 108;
const int SPEED_UP_EVERY_HITS = 2;
const int SPEED_STEP = 14;
const int MAX_VX = 286;
const int MAX_VY = 215;
const int WIN_SCORE = 7;

struct Particle {
    int8_t x;
    int8_t y;
    uint8_t ttl;
    CRGB color;
};

UnifiedGameState state = GAME_MENU;
GameRuntime runtime;
bool pointPause = false;
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
int nextServeVxAbs = INITIAL_VX;
int lastLeftMove = 0;
int lastRightMove = 0;
int matchBestRally = 0;
int rallyHits = 0;
int serveDir = 1;
bool leftHuman = false;
bool rightHuman = false;
bool leftHumanInputArmed = false;
bool rightHumanInputArmed = false;
unsigned long lastLeftInputMs = 0;
unsigned long lastRightInputMs = 0;
unsigned long lastLeftBotMs = 0;
unsigned long lastRightBotMs = 0;
unsigned long stateStartedMs = 0;
Particle particles[8];
int previewBallX = 16 * SCALE;
int previewBallY = 8 * SCALE;
int previewLastBallX = 16 * SCALE;
int previewLastBallY = 8 * SCALE;
int previewBallVx = 88;
int previewBallVy = 54;
int previewLeftY = 6;
int previewRightY = 6;
unsigned long previewLastStepMs = 0;
unsigned long previewLastPaddleMs = 0;

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

int readVertical(const GameStickState& stick) {
    int x = stick.x;
    if (x > INPUT_DEADZONE) return -1;
    if (x < -INPUT_DEADZONE) return 1;
    return 0;
}

bool pongStickNeutral(const GameStickState& stick) {
    return abs(stick.x) <= INPUT_DEADZONE && abs(stick.y) <= INPUT_DEADZONE;
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
    ballVxAbs = clampInt(nextServeVxAbs, INITIAL_VX, INITIAL_VX + 48);
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
    pointPause = false;
    nextServeVxAbs = INITIAL_VX;
    resetPaddles();
    serveBall(random(0, 2) == 0 ? -1 : 1);
}

void beginCountdown() {
    state = GAME_COUNTDOWN;
    stateStartedMs = millis();
    resetPaddles();
    serveBall(serveDir);
    playSound(SND_SELECT);
}

void applyInput(unsigned long now, const GameControlEvents& controls) {
    int leftMove = 0;
    int rightMove = 0;
    if (leftHuman) {
        if (!leftHumanInputArmed) {
            if (pongStickNeutral(controls.secondary)) leftHumanInputArmed = true;
        } else {
            leftMove = readVertical(controls.secondary);
        }
    }
    if (rightHuman) {
        if (!rightHumanInputArmed) {
            if (pongStickNeutral(controls.primary)) rightHumanInputArmed = true;
        } else {
            rightMove = readVertical(controls.primary);
        }
    }

    auto predictedImpactY = [&](int targetX) {
        if (ballVx == 0) return HEIGHT / 2;
        long distance = abs(targetX * SCALE - ballX);
        long projected = ballY + (long)ballVy * distance / abs(ballVx);
        long edge = (HEIGHT - 1) * SCALE;
        long period = edge * 2;
        projected %= period;
        if (projected < 0) projected += period;
        if (projected > edge) projected = period - projected;
        return clampInt((projected + SCALE / 2) / SCALE, 0, HEIGHT - 1);
    };

    // Der verteidigende Bot trifft bewusst seitlich, um den Ball in die vom
    // Gegner freie Haelfte zu lenken. Der andere verfolgt schon den Ball und
    // bereitet den naechsten Return vor, statt unbeweglich in der Mitte zu stehen.
    auto botTargetCenter = [&](bool leftSide) {
        bool incoming = leftSide ? ballVx < 0 : ballVx > 0;
        int ownTargetX = leftSide ? LEFT_X + 1 : RIGHT_X - 1;
        int opponentTargetX = leftSide ? RIGHT_X - 1 : LEFT_X + 1;
        int opponentCenter = (leftSide ? rightY : leftY) + PADDLE_H / 2;

        if (incoming) {
            int impact = predictedImpactY(ownTargetX);
            if (pongBotMode == 1) {
                return clampInt(impact, PADDLE_H / 2, HEIGHT - PADDLE_H / 2);
            }
            int aimAway = opponentCenter < HEIGHT / 2 ? 1 : -1;
            int aimError = 0;
            return clampInt(impact - aimAway + aimError,
                            PADDLE_H / 2, HEIGHT - PADDLE_H / 2);
        }

        int nextImpact = predictedImpactY(opponentTargetX);
        int ballPixelY = clampInt((ballY + SCALE / 2) / SCALE, 0, HEIGHT - 1);
        int motionLead = ballVy >= 0 ? 2 : -2;
        return clampInt((nextImpact + ballPixelY) / 2 + motionLead,
                        PADDLE_H / 2, HEIGHT - PADDLE_H / 2);
    };

    const unsigned long botStepMs = pongBotMode == 1 ? 24 : 50;
    if (!leftHuman && now - lastLeftBotMs >= botStepMs) {
        int target = botTargetCenter(true);
        int center = leftY + PADDLE_H / 2;
        if (target < center) leftMove = -1;
        else if (target > center) leftMove = 1;
        lastLeftBotMs = now;
    }

    if (!rightHuman && now - lastRightBotMs >= botStepMs) {
        int target = botTargetCenter(false);
        int center = rightY + PADDLE_H / 2;
        if (target < center) rightMove = -1;
        else if (target > center) rightMove = 1;
        lastRightBotMs = now;
    }

    const unsigned long leftStepMs = !leftHuman && pongBotMode == 1 ? 32 : INPUT_STEP_MS;
    const unsigned long rightStepMs = !rightHuman && pongBotMode == 1 ? 32 : INPUT_STEP_MS;
    if (leftMove != 0 && now - lastLeftInputMs >= leftStepMs) {
        leftY = clampInt(leftY + leftMove, 0, HEIGHT - PADDLE_H);
        lastLeftInputMs = now;
        lastLeftMove = leftMove;
    } else if (leftMove == 0) {
        lastLeftMove = 0;
    }

    if (rightMove != 0 && now - lastRightInputMs >= rightStepMs) {
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

    rallyHits++;
    bool botsOnly = !leftHuman && !rightHuman;
    // In an attract match the paddles keep a fixed physical speed. The ball
    // accelerates on every return, so even perfect prediction cannot create an
    // endless rally. Human matches retain the gentler two-hit progression.
    ballVxAbs = botsOnly
        ? min(ballVxAbs + 16, MAX_VX)
        : steppedCappedSpeed(ballVxAbs, rallyHits,
                             SPEED_UP_EVERY_HITS, SPEED_STEP, MAX_VX);
    ballVx = leftPaddle ? ballVxAbs : -ballVxAbs;
    ballVy = clampInt(targetVy, -MAX_VY, MAX_VY);
    ballX = (leftPaddle ? (LEFT_X + 2) : (RIGHT_X - 2)) * SCALE;

    if (rallyHits > matchBestRally) matchBestRally = rallyHits;
    spawnImpact(leftPaddle ? LEFT_X + 1 : RIGHT_X - 1, hitY, leftPaddle ? 1 : -1);
    playSound(ballVxAbs >= MAX_VX - SPEED_STEP ? SND_SHOOT : SND_HIT);
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
        nextServeVxAbs = clampInt(ballVxAbs - SPEED_STEP * 2, INITIAL_VX, INITIAL_VX + 48);
        rightScore++;
        serveDir = -1;
        pointPause = true;
        stateStartedMs = millis();
        playSound(SND_DIE);
    } else if (ballX > WIDTH * SCALE) {
        nextServeVxAbs = clampInt(ballVxAbs - SPEED_STEP * 2, INITIAL_VX, INITIAL_VX + 48);
        leftScore++;
        serveDir = 1;
        pointPause = true;
        stateStartedMs = millis();
        playSound(SND_DIE);
    }
}

void renderField() {
    FastLED.clear();
    for (int y = 1; y < HEIGHT; y += 3) setPixel(15, y, CRGB(20, 28, 38));

    drawNumber2(leftScore, 4, 0, CRGB(70, 90, 120));
    drawNumber2(rightScore, 20, 0, CRGB(70, 90, 120));

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

void renderMenu() {
    unsigned long now = millis();
    if (previewLastStepMs == 0 || now - previewLastStepMs > 250UL) {
        previewLastStepMs = now;
        previewLastPaddleMs = now;
    }

    auto predictedPreviewImpact = [&](int targetX) {
        long distance = abs(targetX * SCALE - previewBallX);
        long projected = previewBallY +
                         (long)previewBallVy * distance / max(1, abs(previewBallVx));
        long edge = (HEIGHT - 1) * SCALE;
        long period = edge * 2;
        projected %= period;
        if (projected < 0) projected += period;
        if (projected > edge) projected = period - projected;
        return clampInt((projected + SCALE / 2) / SCALE, 0, HEIGHT - 1);
    };

    if (now - previewLastPaddleMs >= 40UL) {
        int leftTarget = previewBallVx < 0 ? predictedPreviewImpact(3) : HEIGHT / 2;
        int rightTarget = previewBallVx > 0 ? predictedPreviewImpact(28) : HEIGHT / 2;
        int leftCenter = previewLeftY + PADDLE_H / 2;
        int rightCenter = previewRightY + PADDLE_H / 2;
        if (leftTarget < leftCenter) previewLeftY--;
        else if (leftTarget > leftCenter) previewLeftY++;
        if (rightTarget < rightCenter) previewRightY--;
        else if (rightTarget > rightCenter) previewRightY++;
        previewLeftY = clampInt(previewLeftY, 0, HEIGHT - PADDLE_H);
        previewRightY = clampInt(previewRightY, 0, HEIGHT - PADDLE_H);
        previewLastPaddleMs = now;
    }

    uint8_t steps = 0;
    while (now - previewLastStepMs >= 16UL && steps++ < 4) {
        previewLastStepMs += 16UL;
        previewLastBallX = previewBallX;
        previewLastBallY = previewBallY;
        previewBallX += previewBallVx;
        previewBallY += previewBallVy;

        if (previewBallY < 0) {
            previewBallY = -previewBallY;
            previewBallVy = -previewBallVy;
        } else if (previewBallY > (HEIGHT - 1) * SCALE) {
            previewBallY = (HEIGHT - 1) * SCALE -
                           (previewBallY - (HEIGHT - 1) * SCALE);
            previewBallVy = -previewBallVy;
        }

        int hitY = clampInt(previewBallY / SCALE, 0, HEIGHT - 1);
        if (previewBallVx < 0 && previewBallX <= 3 * SCALE) {
            previewLeftY = clampInt(hitY - PADDLE_H / 2, 0, HEIGHT - PADDLE_H);
            previewBallX = 3 * SCALE;
            previewBallVx = abs(previewBallVx);
            int offset = hitY - (previewLeftY + PADDLE_H / 2);
            previewBallVy = clampInt(previewBallVy + offset * 12, -96, 96);
        } else if (previewBallVx > 0 && previewBallX >= 28 * SCALE) {
            previewRightY = clampInt(hitY - PADDLE_H / 2, 0, HEIGHT - PADDLE_H);
            previewBallX = 28 * SCALE;
            previewBallVx = -abs(previewBallVx);
            int offset = hitY - (previewRightY + PADDLE_H / 2);
            previewBallVy = clampInt(previewBallVy + offset * 12, -96, 96);
        }
    }

    FastLED.clear();
    for (int y = 1; y < HEIGHT; y += 3) setPixel(15, y, CRGB(18, 28, 42));
    for (int y = 0; y < PADDLE_H; y++) {
        setPixel(2, previewLeftY + y, CRGB::Cyan);
        setPixel(29, previewRightY + y, CRGB::Orange);
    }
    int trailX = clampInt(previewLastBallX / SCALE, 0, WIDTH - 1);
    int trailY = clampInt(previewLastBallY / SCALE, 0, HEIGHT - 1);
    setPixel(trailX, trailY, CRGB(45, 48, 60));
    int bx = clampInt(previewBallX / SCALE, 0, WIDTH - 1);
    int by = clampInt(previewBallY / SCALE, 0, HEIGHT - 1);
    setPixel(bx, by, CRGB::White);
}

void renderCountdown(unsigned long now) {
    renderField();
    int left = 3 - (int)((now - stateStartedMs) / 700);
    if (left < 1) left = 1;
    drawDigitW(14, 5, left, CRGB::Yellow);
}

void renderGameOver() {
    FastLED.clear();
    CRGB winCol = leftScore > rightScore ? CRGB::Cyan : CRGB::Orange;
    int winX = leftScore > rightScore ? 6 : 24;
    for (int y = 4; y < 10; y++) setPixel(winX, y, winCol);
    for (int i = 0; i < 5; i++) setPixel(15 + i % 3, 4 + i, CRGB::White);
    drawNumber2(matchBestRally, 11, 9, CRGB::Yellow);
}

void tickPong() {
    unsigned long now = millis();
    GameControlEvents controls = runtime.poll(joystick1, joystick3);
    if (controls.primaryDoubleClick && !rightHuman) {
        rightHuman = true;
        rightHumanInputArmed = false;
        lastRightMove = 0;
        playSound(SND_POWERUP);
    }
    if (controls.secondaryDoubleClick && !leftHuman) {
        leftHuman = true;
        leftHumanInputArmed = false;
        lastLeftMove = 0;
        playSound(SND_POWERUP);
    }
    joystick1.einfacherKlickZaehler = 0;
    joystick3.einfacherKlickZaehler = 0;
    if (controls.pauseToggle && (state == GAME_PLAYING || state == GAME_PAUSED))
        runtime.applyPause(state);

    switch (state) {
    case GAME_MENU:
        renderMenu();
        break;

    case GAME_COUNTDOWN:
        applyInput(now, controls);
        renderCountdown(now);
        if (now - stateStartedMs >= 2100) {
            state = GAME_PLAYING;
            stateStartedMs = now;
        }
        break;

    case GAME_PLAYING:
        if (pointPause) {
            updateParticles();
            renderField();
            if (leftScore >= WIN_SCORE || rightScore >= WIN_SCORE) {
                if (now - stateStartedMs >= 900) {
                    runtime.animateTransition(CRGB::Red);
                    state = GAME_GAME_OVER;
                    stateStartedMs = now;
                    pointPause = false;
                }
            } else if (now - stateStartedMs >= 900) {
                pointPause = false;
                beginCountdown();
            }
            break;
        }
        applyInput(now, controls);
        updateBall();
        updateParticles();
        renderField();
        break;

    case GAME_PAUSED:
        runtime.drawPauseOverlay();
        break;

    case GAME_GAME_OVER:
        renderGameOver();
        if (now - stateStartedMs >= 1800UL) {
            resetMatch();
            runtime.animateTransition(CRGB::Blue);
            beginCountdown();
        }
        break;
    }

    FastLED.show();
}
}

void renderPongMenuPreview() {
    renderMenu();
}

void taskPongHandler(void *pvParameters) {
    int lastTask = -1;

    for (;;) {
        if (aktiverTask != 7) {
            feedRuntimeWatchdog();
            lastTask = -1;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (lastTask != 7) {
            leftHuman = false;
            rightHuman = false;
            leftHumanInputArmed = false;
            rightHumanInputArmed = false;
            runtime.begin();
            joystick1.reset();
            joystick3.reset();
            resetMatch();
            beginCountdown();
            lastTask = 7;
            Serial.println("Pong gestartet.");
        }

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        if (aktiverTask == 7) tickPong();

        unlockDisplay();
        runtime.waitForNextFrame(16);
    }
}
