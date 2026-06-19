#include "TaskSnake.h"
#include <Arduino.h>
#include <FastLED.h>
#include "HardwareUtils.h"

#define WIDTH          32
#define HEIGHT_TOTAL   16

// Die Joystick-Pins für Snake (Joystick 1)
#define PIN_X          34
#define PIN_Y          35

struct Point { int x, y; };

void taskSnakeHandler(void *pvParameters) {
    Point snake[100]; 
    int snakeLength = 3; 
    Point dir = {1, 0}; 
    Point food;
    int moveInterval = 150; 
    unsigned long lastMoveTime = 0;
    
    auto spawnFood = [&]() { 
        food.x = random(1, WIDTH - 1); 
        food.y = random(1, HEIGHT_TOTAL - 1); 
    };
    
    auto resetGame = [&]() { 
        snakeLength = 3; 
        snake[0] = {15, 8}; 
        snake[1] = {14, 8}; 
        snake[2] = {13, 8}; 
        dir = {1, 0}; 
        moveInterval = 150; 
        spawnFood(); 
    };
    
    resetGame();
    
    for(;;) {
        // Deine originale Achsen-Mapping-Logik (unverändert!)
        int rawX = analogRead(PIN_X); 
        int rawY = analogRead(PIN_Y);
        
        if (rawY > 2800 && dir.x == 0) { dir.x = 1; dir.y = 0; }      
        else if (rawY < 400 && dir.x == 0) { dir.x = -1; dir.y = 0; } 
        else if (rawX < 400 && dir.y == 0) { dir.y = 1; dir.x = 0; }  
        else if (rawX > 2800 && dir.y == 0) { dir.y = -1; dir.x = 0; }

        if (millis() - lastMoveTime > moveInterval) {
            lastMoveTime = millis();
            int nextX = snake[0].x + dir.x; 
            int nextY = snake[0].y + dir.y;
            
            bool dead = (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL);
            for (int i = 0; i < snakeLength; i++) {
                if (nextX == snake[i].x && nextY == snake[i].y) dead = true;
            }
            
            if (dead) { 
                resetGame(); 
                vTaskDelay(1000 / portTICK_PERIOD_MS); 
            } else {
                if (nextX == food.x && nextY == food.y) { 
                    if (snakeLength < 100) snakeLength++; 
                    spawnFood(); 
                    if (moveInterval > 70) moveInterval -= 2; 
                }
                for (int i = snakeLength - 1; i > 0; i--) snake[i] = snake[i - 1];
                snake[0] = {nextX, nextY};
            }
            
            FastLED.clear();
            // Spielfeldrand zeichnen
            for(int x = 0; x < WIDTH; x++) { 
                setPixel(x, 0, CRGB(2, 2, 10)); 
                setPixel(x, 15, CRGB(2, 2, 10)); 
            }
            // Essen zeichnen
            setPixel(food.x, food.y, CRGB::Red);
            // Schlange zeichnen
            for (int i = 0; i < snakeLength; i++) {
                setPixel(snake[i].x, snake[i].y, (i == 0) ? CRGB::White : CRGB::Green);
            }
            FastLED.show();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}