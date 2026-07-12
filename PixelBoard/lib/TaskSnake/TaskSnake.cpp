#include "TaskSnake.h"
#include <Arduino.h>
#include <FastLED.h>
#include "HardwareUtils.h"
#include "Config.h"
#include "SoundUtils.h"
#include "Joystick.h"

extern volatile bool taskWechselAnforderung;
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

enum GameState { STATE_MENU, STATE_PLAYING, STATE_GAMEOVER };
static GameState currentState = STATE_MENU;

struct Point { int x, y; };
struct SnakeItem { Point p; bool active; };

static bool applySnakeInput(Joystick& stick, Point& dir) {
    int xPerc = stick.readXPercent();
    int yPerc = stick.readYPercent();

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

static bool snakeJoystickMoved(Joystick& stick) {
    return abs(stick.readXPercent()) > 70 || abs(stick.readYPercent()) > 70;
}

static int wrapCoordinate(int value, int size) {
    if (value < 0) return size - 1;
    if (value >= size) return 0;
    return value;
}

void taskSnakeHandler(void *pvParameters) {
    static Point snakes[2][MAX_SNAKE_LENGTH];
    int snakeLengths[2] = {3, 3};
    Point dirs[2] = {{1, 0}, {-1, 0}};
    bool playerJoined[2] = {true, false};
    bool playerAlive[2] = {true, false};
    bool secondPlayerNeedsInitialMove = false;
    unsigned long playerJoinMs[2] = {0, 0};
    unsigned long playerDeathMs[2] = {0, 0};
    unsigned long allLostSinceMs = 0;

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
        dirs[0] = {1, 0};
        dirs[1] = {-1, 0};
        playerJoined[0] = true;
        playerJoined[1] = false;
        playerAlive[0] = true;
        playerAlive[1] = false;
        secondPlayerNeedsInitialMove = false;
        playerJoinMs[0] = millis();
        playerJoinMs[1] = 0;
        playerDeathMs[0] = 0;
        playerDeathMs[1] = 0;
        allLostSinceMs = 0;
        moveInterval = 150;
        slowUntilMs = 0;
        currentScore = 0;
        configureItems();
    };
    auto activateSecondPlayer = [&]() {
        dirs[1] = {-dirs[0].x, -dirs[0].y};
        snakeLengths[1] = 3;

        Point preferred;
        if (dirs[1].x != 0) {
            preferred.x = dirs[1].x > 0 ? 4 : WIDTH - 5;
            preferred.y = HEIGHT_TOTAL - 1 - snakes[0][0].y;
        } else {
            preferred.x = WIDTH - 1 - snakes[0][0].x;
            preferred.y = dirs[1].y > 0 ? 4 : HEIGHT_TOTAL - 5;
        }

        auto canPlace = [&](Point head) {
            for (int i = 0; i < snakeLengths[1]; i++) {
                Point segment = {head.x - dirs[1].x * i, head.y - dirs[1].y * i};
                if (segment.x < 0 || segment.x >= WIDTH || segment.y < 0 || segment.y >= HEIGHT_TOTAL) {
                    return false;
                }
                if (onSnake(0, segment) || onAnyItem(segment)) return false;
            }

            Point ahead = {
                wrapCoordinate(head.x + dirs[1].x, WIDTH),
                wrapCoordinate(head.y + dirs[1].y, HEIGHT_TOTAL)
            };
            return !onSnake(0, ahead) && !onItems(obstacles, MAX_SNAKE_OBSTACLES, ahead);
        };

        Point start = preferred;
        bool found = canPlace(start);
        for (int radius = 1; !found && radius < WIDTH + HEIGHT_TOTAL; radius++) {
            for (int y = 0; y < HEIGHT_TOTAL && !found; y++) {
                for (int x = 0; x < WIDTH; x++) {
                    if (abs(x - preferred.x) + abs(y - preferred.y) != radius) continue;
                    Point candidate = {x, y};
                    if (!canPlace(candidate)) continue;
                    start = candidate;
                    found = true;
                    break;
                }
            }
        }
        if (!found) return false;

        for (int i = 0; i < snakeLengths[1]; i++) {
            snakes[1][i] = {start.x - dirs[1].x * i, start.y - dirs[1].y * i};
        }
        playerJoined[1] = true;
        playerAlive[1] = true;
        secondPlayerNeedsInitialMove = true;
        playerJoinMs[1] = millis();
        playSound(SND_POWERUP);
        return true;
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
                setPixel(wrapCoordinate(head.x - 1, WIDTH), head.y, pulse);
                setPixel(wrapCoordinate(head.x + 1, WIDTH), head.y, pulse);
                setPixel(head.x, wrapCoordinate(head.y - 1, HEIGHT_TOTAL), pulse);
                setPixel(head.x, wrapCoordinate(head.y + 1, HEIGHT_TOTAL), pulse);
            }
        }
        FastLED.show();
    };
    auto consumeStartDoubleClick = [&]() {
        bool doubleClicked = joystick1.doppelklickZaehler > 0 ||
                             joystick3.doppelklickZaehler > 0;

        // Snake owns both game joysticks while active. Consume every click type
        // each tick so an event from play cannot become a later restart event.
        joystick1.einfacherKlickZaehler = 0;
        joystick1.doppelklickZaehler = 0;
        joystick1.langKlickZaehler = 0;
        joystick3.einfacherKlickZaehler = 0;
        joystick3.doppelklickZaehler = 0;
        joystick3.langKlickZaehler = 0;
        return doubleClicked;
    };
    auto finishGame = [&]() {
        currentScore = snakeLengths[0];
        if (playerJoined[1] && snakeLengths[1] > currentScore) currentScore = snakeLengths[1];

        String snakeUser = currentUser;
        snakeUser.trim();
        if (snakeUser == "") snakeUser = "Gast";
        saveSnakeHighScore(snakeUser, currentScore);
        currentState = STATE_GAMEOVER;
    };

    resetGame();
    bool taskWasActive = false;

    for (;;) {
        if (aktiverTask != 2) {
            taskWasActive = false;
            taskWechselAnforderung = false;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }
        taskWechselAnforderung = false;

        if (!taskWasActive) {
            taskWasActive = true;
            joystick1.reset();
            joystick3.reset();
            resetGame();
            currentState = STATE_MENU;
        }

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }
        if (aktiverTask != 2) {
            unlockDisplay();
            continue;
        }

        bool startDoubleClick = consumeStartDoubleClick();

        switch (currentState) {
            case STATE_MENU: {
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
                if (startDoubleClick) {
                    resetGame();
                    lastMoveTime = millis();
                    currentState = STATE_PLAYING;
                    playSound(SND_SELECT);
                }
                break;
            }

            case STATE_PLAYING: {
                if (playerAlive[0]) applySnakeInput(joystick1, dirs[0]);
                if (!playerJoined[1]) {
                    if (snakeJoystickMoved(joystick3)) activateSecondPlayer();
                } else if (playerAlive[1] && !secondPlayerNeedsInitialMove) {
                    applySnakeInput(joystick3, dirs[1]);
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
                        next[player].x = wrapCoordinate(snakes[player][0].x + dirs[player].x, WIDTH);
                        next[player].y = wrapCoordinate(snakes[player][0].y + dirs[player].y, HEIGHT_TOTAL);

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

                    if (moving[1]) secondPlayerNeedsInitialMove = false;

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

            case STATE_GAMEOVER: {
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
                if (startDoubleClick) {
                    resetGame();
                    lastMoveTime = millis();
                    currentState = STATE_PLAYING;
                    playSound(SND_SELECT);
                }
                break;
            }
        }
        unlockDisplay();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
