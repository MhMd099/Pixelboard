#include <Arduino.h>
#include <FastLED.h>
#include "Joystick.h"

#define LED_PIN_OBEN   26  
#define LED_PIN_UNTEN  25  
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define WIDTH  32
#define HEIGHT_TOTAL 16

const int PIN_X = 34;
const int PIN_Y = 35;
const int PIN_BTN = 32;

CRGB ledsOben[256];
CRGB ledsUnten[256];
Joystick joystick(PIN_X, PIN_Y, PIN_BTN);

struct Point { int x, y; };

// --- Spiel-Variablen ---
Point snake[100];      // Schlange kann bis zu 100 Pixel lang werden
int snakeLength = 3;   // Start-Länge
Point dir = {1, 0};    
Point food;            // Position des Futters
unsigned long lastMoveTime = 0;
int moveInterval = 200; // Geschwindigkeit

// --- Mapping Funktion ---
void setPixel(int x, int y, CRGB color) {
  if (y < 0 || y >= 16 || x < 0 || x >= 32) return;
  if (y < 8) { 
    int index;
    if (x % 2 == 0) index = x * 8 + y;
    else index = x * 8 + (7 - y);
    if (index >= 0 && index < 256) ledsOben[index] = color;
  } 
  else { 
    int vX = 31 - x;
    int vY = 7 - (y - 8);
    int index;
    if (vX % 2 == 0) index = vX * 8 + vY;
    else index = vX * 8 + (7 - vY);
    if (index >= 0 && index < 256) ledsUnten[index] = color;
  }
}

// Funktion um Futter an einer freien Stelle zu platzieren
void spawnFood() {
  food.x = random(1, WIDTH - 1);
  food.y = random(1, HEIGHT_TOTAL - 1);
}

void resetGame() {
  snakeLength = 3;
  snake[0] = {15, 8};
  snake[1] = {14, 8};
  snake[2] = {13, 8};
  dir = {1, 0};
  moveInterval = 200;
  spawnFood();
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben, 256);
  FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten, 256);
  FastLED.setBrightness(15);
  
  randomSeed(analogRead(0)); // Zufallsgenerator initialisieren
  resetGame();
}

void loop() {
  // 1. STEUERUNG
  int xVal = analogRead(PIN_X);
  int yVal = analogRead(PIN_Y);

  if (xVal < 500 && dir.x == 0) dir = {-1, 0};
  else if (xVal > 3500 && dir.x == 0) dir = {1, 0};
  else if (yVal < 500 && dir.y == 0) dir = {0, -1};
  else if (yVal > 3500 && dir.y == 0) dir = {0, 1};

  // 2. BEWEGUNG & LOGIK
  if (millis() - lastMoveTime > moveInterval) {
    lastMoveTime = millis();

    int nextX = snake[0].x + dir.x;
    int nextY = snake[0].y + dir.y;

    // A) Kollision mit der Wand
    if (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL) {
      goto gameOver;
    }

    // B) Kollision mit sich selbst
    for (int i = 0; i < snakeLength; i++) {
      if (nextX == snake[i].x && nextY == snake[i].y) goto gameOver;
    }

    // C) Futter fressen
    if (nextX == food.x && nextY == food.y) {
      if (snakeLength < 100) snakeLength++; // Schlange wächst
      spawnFood();                          // Neues Futter
      if (moveInterval > 80) moveInterval -= 2; // Spiel wird langsam schneller
    }

    // D) Körper-Teile nachrücken
    for (int i = snakeLength - 1; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
    snake[0].x = nextX;
    snake[0].y = nextY;

    // 3. ZEICHNEN
    FastLED.clear();
    
    // Rahmen (ganz dunkel)
    for(int x=0; x<WIDTH; x++) { setPixel(x, 0, CRGB(2, 2, 10)); setPixel(x, 15, CRGB(2, 2, 10)); }
    for(int y=0; y<HEIGHT_TOTAL; y++) { setPixel(0, y, CRGB(2, 2, 10)); setPixel(31, y, CRGB(2, 2, 10)); }

    // Futter zeichnen (Rot)
    setPixel(food.x, food.y, CRGB::Red);

    // Schlange zeichnen
    for (int i = 0; i < snakeLength; i++) {
      setPixel(snake[i].x, snake[i].y, (i == 0) ? CRGB::White : CRGB::Green);
    }
    
    FastLED.show();
  }
  return;

  // Game Over Label
  gameOver:
    for(int i=0; i<snakeLength; i++) setPixel(snake[i].x, snake[i].y, CRGB::Red);
    FastLED.show();
    delay(1000);
    resetGame();
}