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
extern void drawChar3x5(int x, int y, char c, CRGB color);

#define WIDTH 32
#define HEIGHT_TOTAL 16

enum GameState { STATE_MENU, STATE_SHOW_HIGHSCORES, STATE_PLAYING, STATE_GAMEOVER };
GameState currentState = STATE_MENU;

struct Point { int x, y; };

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
    Point snake[100]; 
    int snakeLength = 3; 
    Point dir = {1, 0}; 
    Point food;
    int moveInterval = 150; 
    unsigned long lastMoveTime = 0;
    unsigned long stateTimer = 0;
    int currentScore = 0;
    int highScoreIdx = 0;

    auto spawnFood = [&]() { food.x = random(1, WIDTH - 1); food.y = random(1, HEIGHT_TOTAL - 1); };
    auto resetGame = [&]() { snakeLength = 3; snake[0] = {15, 8}; snake[1] = {14, 8}; snake[2] = {13, 8}; dir = {1, 0}; moveInterval = 150; spawnFood(); };
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
                    CRGB textCol = themeCol(now / 35 + 130, 210);
                    drawChar3x5(6, 11, 'P', textCol);
                    drawChar3x5(11, 11, 'L', textCol);
                    drawChar3x5(16, 11, 'A', textCol);
                    drawChar3x5(21, 11, 'Y', textCol);
                }
                FastLED.show();
                if (btnPress) { currentState = STATE_SHOW_HIGHSCORES; stateTimer = millis(); }
                break;
            }

            case STATE_SHOW_HIGHSCORES: {
                PlayerData top[10]; int count;
                getTopScores(top, count);
                
                if (millis() - stateTimer > 1500) { 
                    highScoreIdx++; stateTimer = millis(); 
                    if (highScoreIdx >= min(count, 3)) { currentState = STATE_PLAYING; highScoreIdx = 0; }
                }

                FastLED.clear();
                if(count > 0) {
                    CRGB nameCol = themeCol(millis() / 35, 255);
                    CRGB scoreCol = themeCol(millis() / 35 + 96, 235);
                    // Zeige Name (max 3 Buchstaben)
                    String name = top[highScoreIdx].name;
                    for(int i=0; i<min((int)name.length(), 3); i++) {
                        drawChar3x5(4 + (i*7), 2, name.charAt(i), nameCol);
                    }
                    // Zeige Score
                    drawDigitW(10, 9, top[highScoreIdx].score / 10, scoreCol);
                    drawDigitW(16, 9, top[highScoreIdx].score % 10, scoreCol);
                }
                FastLED.show();
                break;
            }

          case STATE_PLAYING: { // <-- KLAMMERN ZUGEFÜGT
                if (!applySnakeInput(joystick1, dir)) {
                    applySnakeInput(joystick3, dir);
                }

                if (millis() - lastMoveTime > moveInterval) {
                    lastMoveTime = millis();
                    int nextX = snake[0].x + dir.x; int nextY = snake[0].y + dir.y;
                    bool dead = (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL);
                    for (int i = 0; i < snakeLength; i++) if (nextX == snake[i].x && nextY == snake[i].y) dead = true;
                    
                    if (dead) { 
                        playSound(SND_DIE);
                        currentScore = snakeLength;
                        saveHighScore(currentUser, currentScore);
                        currentState = STATE_GAMEOVER;
                    } else {
                        if (nextX == food.x && nextY == food.y) {
                            playSound(SND_EAT);
                            if (snakeLength < 100) snakeLength++;
                            spawnFood();
                            if (moveInterval > 70) moveInterval -= 2;
                        }
                        for (int i = snakeLength - 1; i > 0; i--) snake[i] = snake[i - 1];
                        snake[0] = {nextX, nextY};
                    }
                    FastLED.clear();
                    CRGB headCol = themeCol(millis() / 20, 255);
                    CRGB bodyCol = themeCol(millis() / 20 + 80, 170);
                    CRGB foodCol = themeCol(millis() / 20 + 160, 255);
                    setPixel(food.x, food.y, foodCol);
                    for (int i = 0; i < snakeLength; i++) setPixel(snake[i].x, snake[i].y, (i == 0) ? headCol : bodyCol);
                    FastLED.show();
                }
                break;
            } // <-- KLAMMER ZU

            case STATE_GAMEOVER: {
                FastLED.clear();
                // Zeige "END" und Score
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
