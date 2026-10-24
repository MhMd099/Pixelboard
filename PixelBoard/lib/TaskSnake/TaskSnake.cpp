#include "TaskSnake.h"
#include <Arduino.h>
#include <FastLED.h>
#include "HardwareUtils.h"
#include "Config.h"
#include "SoundUtils.h"
#include "Joystick.h"
#include "GameRuntime.h"
#include "RuntimeHealth.h"

extern volatile int aktiverTask;
extern Joystick joystick1;
extern Joystick joystick3;

extern void drawDigitW(int x, int y, int n, CRGB c);

#define WIDTH 32
#define HEIGHT_TOTAL 16
#define MAX_SNAKE_LENGTH 100
#define MAX_SNAKE_FOOD 8
#define MAX_SNAKE_OBSTACLES 48
#define MAX_SNAKE_POWERUPS 6

static UnifiedGameState currentState = GAME_MENU;

struct Point { int x, y; };
struct SnakeItem { Point p; bool active; };

static bool applySnakeInput(const GameStickState& stick, Point& dir) {
    int xPerc = stick.x;
    int yPerc = stick.y;

    if (yPerc > 70 && dir.x == 0) {
        dir = {1, 0};
        return true;
    }
    if (yPerc < -70 && dir.x == 0) {
        dir = {-1, 0};
        return true;
    }
    if (xPerc < -70 && dir.y == 0) {
        dir = {0, 1};
        return true;
    }
    if (xPerc > 70 && dir.y == 0) {
        dir = {0, -1};
        return true;
    }
    return false;
}

static bool snakeJoystickMoved(const GameStickState& stick) {
    return abs(stick.x) > 70 || abs(stick.y) > 70;
}

static Point snakePreviewRoutePoint(int step) {
    const int left = 4;
    const int right = 27;
    const int top = 3;
    const int bottom = 12;
    const int horizontal = right - left;
    const int vertical = bottom - top;
    const int perimeter = 2 * (horizontal + vertical);
    step %= perimeter;
    if (step < 0) step += perimeter;
    if (step <= horizontal) return {left + step, top};
    step -= horizontal;
    if (step <= vertical) return {right, top + step};
    step -= vertical;
    if (step <= horizontal) return {right - step, bottom};
    step -= horizontal;
    return {left, bottom - step};
}

void renderSnakeMenuPreview() {
    FastLED.clear();
    unsigned long now = millis();
    int headStep = (now / 105UL) % 64;
    Point apple = snakePreviewRoutePoint(20);
    if (headStep != 20 && headStep != 21) {
        setPixel(apple.x, apple.y, CRGB::Red);
        if ((now / 180UL) & 1) setPixel(apple.x, apple.y - 1, CRGB(120, 18, 12));
    }

    for (int i = 11; i >= 0; i--) {
        Point segment = snakePreviewRoutePoint(headStep - i);
        CRGB color = i == 0 ? themeCol(now / 18, 255)
                            : themeCol(now / 18 + i * 7, 190);
        setPixel(segment.x, segment.y, color);
    }
    Point head = snakePreviewRoutePoint(headStep);
    Point ahead = snakePreviewRoutePoint(headStep + 1);
    int eyeX = constrain(head.x + (ahead.x - head.x), 0, WIDTH - 1);
    int eyeY = constrain(head.y + (ahead.y - head.y), 0, HEIGHT_TOTAL - 1);
    setPixel(eyeX, eyeY, CRGB::White);
}

void taskSnakeHandler(void *pvParameters) {
    GameRuntime runtime;
    static Point snakes[2][MAX_SNAKE_LENGTH];
    int snakeLengths[2] = {3, 3};
    Point dirs[2] = {{1, 0}, {-1, 0}};
    bool playerJoined[2] = {true, false};
    bool playerAlive[2] = {true, false};
    bool playerHuman[2] = {false, false};
    bool humanInputArmed[2] = {false, false};
    int8_t humanController[2] = {-1, -1};
    unsigned long playerJoinMs[2] = {0, 0};
    unsigned long playerDeathMs[2] = {0, 0};
    unsigned long allLostSinceMs = 0;
    unsigned long countdownStartedMs = 0;

    static SnakeItem foods[MAX_SNAKE_FOOD];
    static SnakeItem obstacles[MAX_SNAKE_OBSTACLES];
    static SnakeItem powerups[MAX_SNAKE_POWERUPS];

    int moveInterval = 150;
    unsigned long lastMoveTime = 0;
    unsigned long slowUntilMs = 0;
    int currentScore = 0;

    auto pointEquals = [](Point a, Point b) {
        return a.x == b.x && a.y == b.y;
    };
    auto onSnake = [&](int player, Point p) {
        for (int i = 0; i < snakeLengths[player]; i++) {
            if (pointEquals(snakes[player][i], p)) return true;
        }
        return false;
    };
    auto onItems = [&](SnakeItem* items, int count, Point p) {
        for (int i = 0; i < count; i++) {
            if (items[i].active && pointEquals(items[i].p, p)) return true;
        }
        return false;
    };
    auto onAnyItem = [&](Point p) {
        return onItems(obstacles, MAX_SNAKE_OBSTACLES, p) ||
               onItems(foods, MAX_SNAKE_FOOD, p) ||
               onItems(powerups, MAX_SNAKE_POWERUPS, p);
    };
    auto freePoint = [&](Point p) {
        if (p.x < 0 || p.x >= WIDTH || p.y < 0 || p.y >= HEIGHT_TOTAL) return false;
        if (playerAlive[0] && onSnake(0, p)) return false;
        if (playerJoined[1] && playerAlive[1] && onSnake(1, p)) return false;
        return !onAnyItem(p);
    };
    auto randomFreePoint = [&](Point& out) {
        for (int attempt = 0; attempt < 180; attempt++) {
            Point p = {random(0, WIDTH), random(0, HEIGHT_TOTAL)};
            if (freePoint(p)) {
                out = p;
                return true;
            }
        }
        return false;
    };
    auto spawnOne = [&](SnakeItem* items, int index) {
        Point p;
        if (randomFreePoint(p)) {
            items[index].p = p;
            items[index].active = true;
        } else {
            items[index].active = false;
        }
    };
    auto configureItems = [&]() {
        for (int i = 0; i < MAX_SNAKE_FOOD; i++) foods[i].active = false;
        for (int i = 0; i < MAX_SNAKE_OBSTACLES; i++) obstacles[i].active = false;
        for (int i = 0; i < MAX_SNAKE_POWERUPS; i++) powerups[i].active = false;

        int obstacleCount = constrain((int)snakeObstacleTarget, 0, MAX_SNAKE_OBSTACLES);
        int foodCount = constrain((int)snakeFoodTarget, 1, MAX_SNAKE_FOOD);
        int powerCount = constrain((int)snakePowerupTarget, 0, MAX_SNAKE_POWERUPS);
        for (int i = 0; i < obstacleCount; i++) spawnOne(obstacles, i);
        for (int i = 0; i < foodCount; i++) spawnOne(foods, i);
        for (int i = 0; i < powerCount; i++) spawnOne(powerups, i);
    };
    auto resetGame = [&]() {
        snakeLengths[0] = 3;
        snakeLengths[1] = 3;
        snakes[0][0] = {15, 8};
        snakes[0][1] = {14, 8};
        snakes[0][2] = {13, 8};
        snakes[1][0] = {23, 8};
        snakes[1][1] = {24, 8};
        snakes[1][2] = {25, 8};
        dirs[0] = {1, 0};
        dirs[1] = {-1, 0};
        playerJoined[0] = true;
        playerJoined[1] = snakeBotCount >= 2 || playerHuman[1];
        playerAlive[0] = true;
        playerAlive[1] = playerJoined[1];
        playerJoinMs[0] = millis();
        playerJoinMs[1] = playerJoined[1] ? millis() : 0;
        playerDeathMs[0] = 0;
        playerDeathMs[1] = 0;
        allLostSinceMs = 0;
        moveInterval = 150;
        slowUntilMs = 0;
        currentScore = 0;
        configureItems();
    };
    auto controllerInUse = [&](int8_t controller) {
        for (int player = 0; player < 2; player++) {
            if (playerHuman[player] && humanController[player] == controller) return true;
        }
        return false;
    };
    auto claimBot = [&](int8_t controller, int preferredPlayer) {
        if (controllerInUse(controller)) return false;

        int target = -1;
        if (playerJoined[preferredPlayer] && !playerHuman[preferredPlayer]) {
            target = preferredPlayer;
        } else {
            int other = 1 - preferredPlayer;
            if (playerJoined[other] && !playerHuman[other]) target = other;
        }
        if (target < 0) return false;

        playerHuman[target] = true;
        humanInputArmed[target] = false;
        humanController[target] = controller;
        playerJoinMs[target] = millis();
        playSound(SND_POWERUP);
        return true;
    };
    auto joinSecondHuman = [&](int8_t controller) {
        if (playerJoined[1] || controllerInUse(controller)) return false;

        dirs[1] = {-dirs[0].x, -dirs[0].y};
        snakeLengths[1] = 3;
        Point start = {23, 8};
        bool found = false;
        for (int y = 2; y < HEIGHT_TOTAL - 1 && !found; y++) {
            for (int x = 3; x < WIDTH - 2; x++) {
                bool clear = true;
                for (int i = 0; i < snakeLengths[1]; i++) {
                    Point segment = {x - dirs[1].x * i, y - dirs[1].y * i};
                    if (segment.x < 0 || segment.x >= WIDTH || segment.y < 0 ||
                        segment.y >= HEIGHT_TOTAL || onSnake(0, segment) || onAnyItem(segment)) {
                        clear = false;
                        break;
                    }
                }
                if (!clear) continue;
                start = {x, y};
                found = true;
                break;
            }
        }
        if (!found) return false;

        for (int i = 0; i < snakeLengths[1]; i++)
            snakes[1][i] = {start.x - dirs[1].x * i, start.y - dirs[1].y * i};
        playerJoined[1] = true;
        playerAlive[1] = true;
        playerHuman[1] = true;
        humanInputArmed[1] = false;
        humanController[1] = controller;
        playerJoinMs[1] = millis();
        playSound(SND_POWERUP);
        return true;
    };
    auto chooseBotDirection = [&](int player, Point reservedNext, bool hasReservation) {
        static const Point choices[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        Point best = dirs[player];
        int bestScore = 100000;
        for (int i = 0; i < 4; i++) {
            Point candidate = choices[i];
            if (candidate.x == -dirs[player].x && candidate.y == -dirs[player].y) continue;
            Point next = {
                wrapGameCoordinate(snakes[player][0].x + candidate.x, WIDTH),
                wrapGameCoordinate(snakes[player][0].y + candidate.y, HEIGHT_TOTAL)
            };
            int score = 0;
            if (onItems(obstacles, MAX_SNAKE_OBSTACLES, next) || onSnake(player, next)) score += 50000;
            int other = 1 - player;
            if (playerAlive[other] && onSnake(other, next)) score += 50000;
            if (hasReservation && pointEquals(next, reservedNext)) score += 60000;

            int otherDx = abs(next.x - snakes[other][0].x);
            int otherDy = abs(next.y - snakes[other][0].y);
            int otherDistance = min(otherDx, WIDTH - otherDx) +
                                min(otherDy, HEIGHT_TOTAL - otherDy);
            if (playerAlive[other] && otherDistance <= 2) score += (3 - otherDistance) * 900;

            int safeExits = 0;
            for (int look = 0; look < 4; look++) {
                Point follow = choices[look];
                if (follow.x == -candidate.x && follow.y == -candidate.y) continue;
                Point after = {
                    wrapGameCoordinate(next.x + follow.x, WIDTH),
                    wrapGameCoordinate(next.y + follow.y, HEIGHT_TOTAL)
                };
                bool blocked = onItems(obstacles, MAX_SNAKE_OBSTACLES, after) ||
                               onSnake(player, after) ||
                               (playerAlive[other] && onSnake(other, after)) ||
                               (hasReservation && pointEquals(after, reservedNext));
                if (!blocked) safeExits++;
            }
            if (safeExits == 0) score += 20000;
            else score += (3 - min(3, safeExits)) * 450;

            int nearestReward = 100;
            for (int f = 0; f < MAX_SNAKE_FOOD; f++) {
                if (!foods[f].active) continue;
                int dx = abs(next.x - foods[f].p.x);
                int dy = abs(next.y - foods[f].p.y);
                nearestReward = min(nearestReward, min(dx, WIDTH - dx) + min(dy, HEIGHT_TOTAL - dy));
            }
            for (int p = 0; p < MAX_SNAKE_POWERUPS; p++) {
                if (!powerups[p].active) continue;
                int dx = abs(next.x - powerups[p].p.x);
                int dy = abs(next.y - powerups[p].p.y);
                nearestReward = min(nearestReward, min(dx, WIDTH - dx) + min(dy, HEIGHT_TOTAL - dy) - 2);
            }
            score += nearestReward * 10;
            if (onItems(foods, MAX_SNAKE_FOOD, next)) score -= 300;
            if (onItems(powerups, MAX_SNAKE_POWERUPS, next)) score -= 500;
            if (score < bestScore) {
                bestScore = score;
                best = candidate;
            }
        }
        return best;
    };
    auto renderGame = [&]() {
        FastLED.clear();
        unsigned long now = millis();
        CRGB headCols[2] = {
            themeCol(now / 20, 255),
            themeCol(now / 20 + 128, 255)
        };
        CRGB bodyCols[2] = {
            themeCol(now / 20 + 80, 170),
            themeCol(now / 20 + 208, 170)
        };
        CRGB foodCol = CRGB::Red;
        CRGB obstacleCol = CRGB(130, 70, 210);
        CRGB powerCol = CRGB::Cyan;

        for (int i = 0; i < MAX_SNAKE_OBSTACLES; i++) {
            if (obstacles[i].active) setPixel(obstacles[i].p.x, obstacles[i].p.y, obstacleCol);
        }
        for (int i = 0; i < MAX_SNAKE_FOOD; i++) {
            if (foods[i].active) setPixel(foods[i].p.x, foods[i].p.y, foodCol);
        }
        for (int i = 0; i < MAX_SNAKE_POWERUPS; i++) {
            if (powerups[i].active) setPixel(powerups[i].p.x, powerups[i].p.y, powerCol);
        }
        for (int player = 0; player < 2; player++) {
            if (!playerJoined[player]) continue;

            uint8_t visibility = 255;
            if (!playerAlive[player]) {
                unsigned long elapsed = now - playerDeathMs[player];
                if (elapsed >= 900) continue;
                visibility = 255 - (elapsed * 255UL / 900UL);
            }
            for (int i = 0; i < snakeLengths[player]; i++) {
                CRGB color = i == 0 ? headCols[player] : bodyCols[player];
                color.nscale8_video(visibility);
                setPixel(snakes[player][i].x, snakes[player][i].y, color);
            }

            // A short pulse makes it obvious when player two joins mid-game.
            if (playerAlive[player] && now - playerJoinMs[player] < 750 &&
                ((now / 120) % 2 == 0)) {
                Point head = snakes[player][0];
                CRGB pulse = headCols[player];
                pulse.nscale8_video(145);
                setPixel(wrapGameCoordinate(head.x - 1, WIDTH), head.y, pulse);
                setPixel(wrapGameCoordinate(head.x + 1, WIDTH), head.y, pulse);
                setPixel(head.x, wrapGameCoordinate(head.y - 1, HEIGHT_TOTAL), pulse);
                setPixel(head.x, wrapGameCoordinate(head.y + 1, HEIGHT_TOTAL), pulse);
            }
        }
        FastLED.show();
    };
    auto finishGame = [&]() {
        currentScore = snakeLengths[0];
        if (playerJoined[1] && snakeLengths[1] > currentScore) currentScore = snakeLengths[1];

        runtime.animateTransition(CRGB::Red);
        currentState = GAME_GAME_OVER;
    };

    resetGame();
    bool taskWasActive = false;

    for (;;) {
        if (aktiverTask != 2) {
            feedRuntimeWatchdog();
            taskWasActive = false;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (!taskWasActive) {
            taskWasActive = true;
            joystick1.reset();
            joystick3.reset();
            playerHuman[0] = false;
            playerHuman[1] = false;
            humanInputArmed[0] = false;
            humanInputArmed[1] = false;
            humanController[0] = -1;
            humanController[1] = -1;
            resetGame();
            runtime.begin();
            countdownStartedMs = millis();
            currentState = GAME_COUNTDOWN;
        }

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }
        if (aktiverTask != 2) {
            unlockDisplay();
            continue;
        }

        GameControlEvents controls = runtime.poll(joystick1, joystick3);
        if (controls.primaryDoubleClick && !claimBot(0, 0)) joinSecondHuman(0);
        if (controls.secondaryDoubleClick && !claimBot(1, 1)) joinSecondHuman(1);
        joystick1.einfacherKlickZaehler = 0;
        joystick3.einfacherKlickZaehler = 0;
        if (controls.pauseToggle &&
            (currentState == GAME_PLAYING || currentState == GAME_PAUSED)) {
            runtime.applyPause(currentState);
        }

        switch (currentState) {
            case GAME_MENU: {
                FastLED.clear();
                unsigned long now = millis();
                CRGB headCol = themeCol(now / 18, 255);
                CRGB bodyCol = themeCol(now / 18 + 80, 175);
                CRGB foodCol = CRGB::Red;
                int wave = (now / 130) % 16;
                Point preview[12];
                for (int i = 0; i < 12; i++) {
                    int x = 4 + i * 2;
                    int y = 7 + (int)sin8((uint8_t)(wave * 16 + i * 22)) / 55 - 2;
                    preview[i] = {x, y};
                }
                for (int i = 11; i >= 0; i--) {
                    CRGB c = i == 11 ? headCol : bodyCol;
                    if (i < 11) c.nscale8_video(90 + i * 12);
                    setPixel(preview[i].x, preview[i].y, c);
                    if (i == 11) {
                        setPixel(preview[i].x + 1, preview[i].y, c);
                        setPixel(preview[i].x + 1, preview[i].y - 1, CRGB::White);
                    }
                }
                setPixel(29, 6, foodCol);
                setPixel(30, 6, CRGB(120, 20, 20));
                if ((now / 500) % 2 == 0) {
                    CRGB cueCol = themeCol(now / 35 + 130, 210);
                    for (int x = 8; x <= 23; x += 3) setPixel(x, 13, cueCol);
                    setPixel(24, 12, cueCol);
                    setPixel(25, 11, cueCol);
                }
                FastLED.show();
                break;
            }

            case GAME_COUNTDOWN: {
                renderGame();
                unsigned long elapsed = millis() - countdownStartedMs;
                uint8_t number = 3 - min(2UL, elapsed / 700UL);
                runtime.drawCountdownOverlay(number);
                FastLED.show();
                if (elapsed >= 2100UL) {
                    lastMoveTime = millis();
                    currentState = GAME_PLAYING;
                }
                break;
            }

            case GAME_PLAYING: {
                bool movingNow[2] = {playerAlive[0], playerJoined[1] && playerAlive[1]};
                for (int player = 0; player < 2; player++) {
                    if (!movingNow[player] || !playerHuman[player]) continue;
                    const GameStickState& stick = humanController[player] == 0
                        ? controls.primary : controls.secondary;
                    if (!humanInputArmed[player]) {
                        if (!snakeJoystickMoved(stick)) humanInputArmed[player] = true;
                        continue;
                    }
                    applySnakeInput(stick, dirs[player]);
                }

                Point plannedNext[2] = {snakes[0][0], snakes[1][0]};
                for (int player = 0; player < 2; player++) {
                    if (!movingNow[player]) continue;
                    plannedNext[player].x = wrapGameCoordinate(snakes[player][0].x + dirs[player].x, WIDTH);
                    plannedNext[player].y = wrapGameCoordinate(snakes[player][0].y + dirs[player].y, HEIGHT_TOTAL);
                }

                if (movingNow[0] && !playerHuman[0]) {
                    dirs[0] = chooseBotDirection(0, plannedNext[1], movingNow[1]);
                    plannedNext[0].x = wrapGameCoordinate(snakes[0][0].x + dirs[0].x, WIDTH);
                    plannedNext[0].y = wrapGameCoordinate(snakes[0][0].y + dirs[0].y, HEIGHT_TOTAL);
                }
                if (movingNow[1] && !playerHuman[1]) {
                    dirs[1] = chooseBotDirection(1, plannedNext[0], movingNow[0]);
                }

                unsigned long now = millis();
                int activeInterval = now < slowUntilMs ? moveInterval + 45 : moveInterval;
                if (now - lastMoveTime >= (unsigned long)activeInterval) {
                    lastMoveTime = now;
                    bool moving[2] = {playerAlive[0], playerJoined[1] && playerAlive[1]};
                    bool dead[2] = {false, false};
                    Point next[2] = {snakes[0][0], snakes[1][0]};

                    for (int player = 0; player < 2; player++) {
                        if (!moving[player]) continue;
                        next[player].x = wrapGameCoordinate(snakes[player][0].x + dirs[player].x, WIDTH);
                        next[player].y = wrapGameCoordinate(snakes[player][0].y + dirs[player].y, HEIGHT_TOTAL);

                        if (onItems(obstacles, MAX_SNAKE_OBSTACLES, next[player])) dead[player] = true;
                        if (onSnake(player, next[player])) dead[player] = true;

                        int other = 1 - player;
                        if (moving[other] && onSnake(other, next[player])) dead[player] = true;
                    }

                    if (moving[0] && moving[1] && pointEquals(next[0], next[1])) {
                        dead[0] = true;
                        dead[1] = true;
                    }

                    bool somebodyDied = false;
                    for (int player = 0; player < 2; player++) {
                        if (!dead[player]) continue;
                        playerAlive[player] = false;
                        playerDeathMs[player] = now;
                        somebodyDied = true;
                    }
                    if (somebodyDied) playSound(SND_DIE);

                    int eatenFood[2] = {-1, -1};
                    int eatenPower[2] = {-1, -1};
                    for (int player = 0; player < 2; player++) {
                        if (!moving[player] || dead[player]) continue;

                        int growth = 0;
                        for (int i = 0; i < MAX_SNAKE_FOOD; i++) {
                            if (!foods[i].active || !pointEquals(next[player], foods[i].p)) continue;
                            foods[i].active = false;
                            eatenFood[player] = i;
                            growth++;
                            moveInterval = max(70, moveInterval - 2);
                            playSound(SND_EAT);
                            break;
                        }
                        for (int i = 0; i < MAX_SNAKE_POWERUPS; i++) {
                            if (!powerups[i].active || !pointEquals(next[player], powerups[i].p)) continue;
                            powerups[i].active = false;
                            eatenPower[player] = i;
                            growth += 2;
                            slowUntilMs = now + 4500UL;
                            playSound(SND_BUFF);
                            break;
                        }

                        int oldLength = snakeLengths[player];
                        int newLength = min(MAX_SNAKE_LENGTH, oldLength + growth);
                        Point oldTail = snakes[player][oldLength - 1];
                        for (int i = oldLength - 1; i > 0; i--) {
                            snakes[player][i] = snakes[player][i - 1];
                        }
                        snakes[player][0] = next[player];
                        for (int i = oldLength; i < newLength; i++) snakes[player][i] = oldTail;
                        snakeLengths[player] = newLength;
                    }

                    for (int player = 0; player < 2; player++) {
                        if (eatenFood[player] >= 0) spawnOne(foods, eatenFood[player]);
                        if (eatenPower[player] >= 0) spawnOne(powerups, eatenPower[player]);
                    }

                    renderGame();

                    bool allPlayersLost = !playerAlive[0] &&
                                          (!playerJoined[1] || !playerAlive[1]);
                    if (allPlayersLost) {
                        if (allLostSinceMs == 0) allLostSinceMs = now;
                        if (now - allLostSinceMs >= 900) finishGame();
                    } else {
                        allLostSinceMs = 0;
                    }
                }
                break;
            }

            case GAME_PAUSED: {
                runtime.drawPauseOverlay();
                break;
            }

            case GAME_GAME_OVER: {
                FastLED.clear();
                unsigned long now = millis();
                CRGB playerCols[2] = {
                    themeCol(now / 35, 255),
                    themeCol(now / 35 + 128, playerJoined[1] ? 255 : 55)
                };
                int scores[2] = {
                    min(snakeLengths[0], 99),
                    playerJoined[1] ? min(snakeLengths[1], 99) : 0
                };
                drawDigitW(2, 8, scores[0] / 10, playerCols[0]);
                drawDigitW(8, 8, scores[0] % 10, playerCols[0]);
                drawDigitW(20, 8, scores[1] / 10, playerCols[1]);
                drawDigitW(26, 8, scores[1] % 10, playerCols[1]);
                FastLED.show();
                if (now - allLostSinceMs >= 1800UL) {
                    resetGame();
                    countdownStartedMs = millis();
                    currentState = GAME_COUNTDOWN;
                    runtime.animateTransition(CRGB::Green);
                    playSound(SND_SELECT);
                }
                break;
            }
        }
        unlockDisplay();
        runtime.waitForNextFrame(16);
    }
}
