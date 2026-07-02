#include "TaskSnake.h"
#include <Arduino.h>
#include <FastLED.h>
#include "HardwareUtils.h"
#include "Config.h"
#include "SoundUtils.h" // NEU
#include "Joystick.h"


extern volatile bool taskWechselAnforderung;
extern volatile int aktiverTask;
extern Joystick joystick1;
extern Joystick joystick3;

// Global verfügbar machen
extern void drawDigitW(int x, int y, int n, CRGB c);

#define WIDTH 32
#define HEIGHT_TOTAL 16
#define MAX_SNAKE_FOOD 8
#define MAX_SNAKE_OBSTACLES 48
#define MAX_SNAKE_POWERUPS 6

enum GameState { STATE_MENU, STATE_SHOW_HIGHSCORES, STATE_PLAYING, STATE_GAMEOVER };
GameState currentState = STATE_MENU;

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

void taskSnakeHandler(void *pvParameters) {
    static Point snake[100];
    int snakeLength = 3; 
    Point dir = {1, 0}; 
    static SnakeItem foods[MAX_SNAKE_FOOD];
    static SnakeItem obstacles[MAX_SNAKE_OBSTACLES];
    static SnakeItem powerups[MAX_SNAKE_POWERUPS];
    int moveInterval = 150; 
    unsigned long lastMoveTime = 0;
    unsigned long stateTimer = 0;
    unsigned long slowUntilMs = 0;
    int currentScore = 0;
    int highScoreIdx = 0;

    auto pointEquals = [](Point a, Point b) { return a.x == b.x && a.y == b.y; };
    auto onSnake = [&](Point p) {
        for (int i = 0; i < snakeLength; i++)
            if (pointEquals(snake[i], p)) return true;
        return false;
    };
    auto onItems = [&](SnakeItem* items, int count, Point p) {
        for (int i = 0; i < count; i++)
            if (items[i].active && pointEquals(items[i].p, p)) return true;
        return false;
    };
    auto freePoint = [&](Point p) {
        if (p.x <= 0 || p.x >= WIDTH - 1 || p.y <= 0 || p.y >= HEIGHT_TOTAL - 1) return false;
        if (onSnake(p)) return false;
        if (onItems(obstacles, MAX_SNAKE_OBSTACLES, p)) return false;
        if (onItems(foods, MAX_SNAKE_FOOD, p)) return false;
        if (onItems(powerups, MAX_SNAKE_POWERUPS, p)) return false;
        return true;
    };
    auto randomFreePoint = [&](Point& out) {
        for (int attempt = 0; attempt < 140; attempt++) {
            Point p = {random(1, WIDTH - 1), random(1, HEIGHT_TOTAL - 1)};
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
        snakeLength = 3;
        snake[0] = {15, 8};
        snake[1] = {14, 8};
        snake[2] = {13, 8};
        dir = {1, 0};
        moveInterval = 150;
        slowUntilMs = 0;
        configureItems();
    };
    resetGame();

    for(;;) {
        if (aktiverTask != 2) {
            taskWechselAnforderung = false;
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }
        taskWechselAnforderung = false;
        
        bool clicked = joystick1.isPressed() || joystick3.isPressed();
        static bool lastClicked = false;
        bool btnPress = (clicked && !lastClicked);
        lastClicked = clicked;
        if(clicked) vTaskDelay(50 / portTICK_PERIOD_MS);

        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }
        if (aktiverTask != 2) {
            unlockDisplay();
            continue;
        }

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
                    CRGB c = (i == 11) ? headCol : bodyCol;
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
                if (btnPress) {
                    resetGame();
                    lastMoveTime = millis();
                    currentState = STATE_PLAYING;
                }
                break;
            }

            case STATE_SHOW_HIGHSCORES: {
                resetGame();
                lastMoveTime = millis();
                currentState = STATE_PLAYING;
                break;
            }

          case STATE_PLAYING: { // <-- KLAMMERN ZUGEFÜGT
                if (!applySnakeInput(joystick1, dir)) {
                    applySnakeInput(joystick3, dir);
                }

                unsigned long now = millis();
                int activeInterval = now < slowUntilMs ? moveInterval + 45 : moveInterval;
                if (now - lastMoveTime > (unsigned long)activeInterval) {
                    lastMoveTime = millis();
                    int nextX = snake[0].x + dir.x; int nextY = snake[0].y + dir.y;
                    bool dead = (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL);
                    for (int i = 0; i < snakeLength; i++) if (nextX == snake[i].x && nextY == snake[i].y) dead = true;
                    for (int i = 0; i < MAX_SNAKE_OBSTACLES; i++)
                        if (obstacles[i].active && nextX == obstacles[i].p.x && nextY == obstacles[i].p.y) dead = true;
                    
                    if (dead) { 
                        playSound(SND_DIE);
                        currentScore = snakeLength;
                        String snakeUser = currentUser;
                        snakeUser.trim();
                        if (snakeUser == "") snakeUser = "Gast";
                        saveSnakeHighScore(snakeUser, currentScore);
                        currentState = STATE_GAMEOVER;
                    } else {
                        int eatenFoodIndex = -1;
                        int eatenPowerIndex = -1;
                        for (int i = 0; i < MAX_SNAKE_FOOD; i++) {
                            if (!foods[i].active || nextX != foods[i].p.x || nextY != foods[i].p.y) continue;
                            playSound(SND_EAT);
                            if (snakeLength < 100) snakeLength++;
                            foods[i].active = false;
                            eatenFoodIndex = i;
                            if (moveInterval > 70) moveInterval -= 2;
                            break;
                        }
                        for (int i = 0; i < MAX_SNAKE_POWERUPS; i++) {
                            if (!powerups[i].active || nextX != powerups[i].p.x || nextY != powerups[i].p.y) continue;
                            playSound(SND_BUFF);
                            if (snakeLength < 98) snakeLength += 2;
                            slowUntilMs = now + 4500UL;
                            powerups[i].active = false;
                            eatenPowerIndex = i;
                            break;
                        }
                        for (int i = snakeLength - 1; i > 0; i--) snake[i] = snake[i - 1];
                        snake[0] = {nextX, nextY};
                        if (eatenFoodIndex >= 0) spawnOne(foods, eatenFoodIndex);
                        if (eatenPowerIndex >= 0) spawnOne(powerups, eatenPowerIndex);
                    }
                    FastLED.clear();
                    CRGB headCol = themeCol(millis() / 20, 255);
                    CRGB bodyCol = themeCol(millis() / 20 + 80, 170);
                    CRGB foodCol = CRGB::Red;
                    CRGB obstacleCol = CRGB(130, 70, 210);
                    CRGB powerCol = CRGB::Cyan;
                    for (int i = 0; i < MAX_SNAKE_OBSTACLES; i++)
                        if (obstacles[i].active) setPixel(obstacles[i].p.x, obstacles[i].p.y, obstacleCol);
                    for (int i = 0; i < MAX_SNAKE_FOOD; i++)
                        if (foods[i].active) setPixel(foods[i].p.x, foods[i].p.y, foodCol);
                    for (int i = 0; i < MAX_SNAKE_POWERUPS; i++)
                        if (powerups[i].active) setPixel(powerups[i].p.x, powerups[i].p.y, powerCol);
                    for (int i = 0; i < snakeLength; i++) setPixel(snake[i].x, snake[i].y, (i == 0) ? headCol : bodyCol);
                    FastLED.show();
                }
                break;
            } // <-- KLAMMER ZU

            case STATE_GAMEOVER: {
                FastLED.clear();
                CRGB scoreCol = themeCol(millis() / 35, 255);
                drawDigitW(12, 8, currentScore / 10, scoreCol);
                drawDigitW(18, 8, currentScore % 10, scoreCol);
                FastLED.show();
                if (btnPress) { resetGame(); currentState = STATE_MENU; }
                break;
            }
        }
        unlockDisplay();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
