#include "TaskSnake.h"
#include <Arduino.h>
#include <FastLED.h>
#include "HardwareUtils.h"
#include "Config.h"
#include "SoundUtils.h" // NEU


extern volatile bool taskWechselAnforderung;

// Global verfügbar machen
extern void drawDigitW(int x, int y, int n, CRGB c);
extern void drawChar3x5(int x, int y, char c, CRGB color);

#define WIDTH 32
#define HEIGHT_TOTAL 16
#define PIN_X 34
#define PIN_Y 35
#define PIN_SW 13 

enum GameState { STATE_MENU, STATE_SHOW_HIGHSCORES, STATE_PLAYING, STATE_GAMEOVER };
GameState currentState = STATE_MENU;

struct Point { int x, y; };

void taskSnakeHandler(void *pvParameters) {
    pinMode(PIN_SW, INPUT_PULLDOWN);
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
        if (taskWechselAnforderung) {
            FastLED.clear();
            FastLED.show();              // LEDs löschen
            taskWechselAnforderung = false; // Flag zurücksetzen
            vTaskSuspend(NULL);          // Task selbst schlafen legen
        }
        
        bool clicked = (digitalRead(PIN_SW) == HIGH);
        static bool lastClicked = false;
        bool btnPress = (clicked && !lastClicked);
        lastClicked = clicked;
        if(clicked) vTaskDelay(50 / portTICK_PERIOD_MS);

        switch (currentState) {
            case STATE_MENU: {
                FastLED.clear();
                // Einfacher Text "PLAY" oder Cursor
                drawChar3x5(2, 6, 'P', CRGB::Green);
                drawChar3x5(8, 6, 'L', CRGB::Green);
                drawChar3x5(14, 6, 'A', CRGB::Green);
                drawChar3x5(20, 6, 'Y', CRGB::Green);
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
                    // Zeige Name (max 3 Buchstaben)
                    String name = top[highScoreIdx].name;
                    for(int i=0; i<min((int)name.length(), 3); i++) {
                        drawChar3x5(4 + (i*7), 2, name.charAt(i), CRGB::Cyan);
                    }
                    // Zeige Score
                    drawDigitW(10, 9, top[highScoreIdx].score / 10, CRGB::Gold);
                    drawDigitW(16, 9, top[highScoreIdx].score % 10, CRGB::Gold);
                }
                FastLED.show();
                break;
            }

          case STATE_PLAYING: { // <-- KLAMMERN ZUGEFÜGT
                int rawX = analogRead(PIN_X); int rawY = analogRead(PIN_Y);
                if (rawY > 2800 && dir.x == 0) { dir.x = 1; dir.y = 0; }      
                else if (rawY < 400 && dir.x == 0) { dir.x = -1; dir.y = 0; } 
                else if (rawX < 400 && dir.y == 0) { dir.y = 1; dir.x = 0; }  
                else if (rawX > 2800 && dir.y == 0) { dir.y = -1; dir.x = 0; }

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
                    setPixel(food.x, food.y, CRGB::Red);
                    for (int i = 0; i < snakeLength; i++) setPixel(snake[i].x, snake[i].y, (i == 0) ? CRGB::White : CRGB::Green);
                    FastLED.show();
                }
                break;
            } // <-- KLAMMER ZU

            case STATE_GAMEOVER: {
                FastLED.clear();
                // Zeige "END" und Score
                drawDigitW(12, 8, currentScore / 10, CRGB::White);
                drawDigitW(18, 8, currentScore % 10, CRGB::White);
                FastLED.show();
                if (btnPress) { resetGame(); currentState = STATE_MENU; }
                break;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}