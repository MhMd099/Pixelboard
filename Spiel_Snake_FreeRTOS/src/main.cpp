#include <Arduino.h>
#include <FastLED.h>
#include "Joystick.h"

// --- Hardware-Konfiguration LED Paneele ---
#define LED_PIN_OBEN   26  
#define LED_PIN_UNTEN  25  
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define WIDTH          32
#define HEIGHT_TOTAL   16

// --- Joystick Pins ---
const int PIN_X = 34;
const int PIN_Y = 35;
const int PIN_BTN = 32;

// --- Instanzen & Handles ---
Joystick meinJoystick(PIN_X, PIN_Y, PIN_BTN);
TaskHandle_t handleA = NULL;
TaskHandle_t handleB = NULL;
TaskHandle_t handleC = NULL;

CRGB ledsOben[256];
CRGB ledsUnten[256];

int aktuellerModus = 0;

// --- Hilfsstrukturen für Snake ---
struct Point { int x, y; };

// --- Deine funktionierende Mapping-Funktion ---
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

// --- Task A & B (Platzhalter) ---
void taskA(void * pvParameters) {
    for(;;) {
        Serial.println("A");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void taskB(void * pvParameters) {
    for(;;) {
        Serial.println("b");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// --- Task C: Das Snake Spiel ---
void taskC(void * pvParameters) {
    // Variablen müssen innerhalb des Tasks oder statisch sein
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
        dir = {1, 0}; // Startet immer nach RECHTS
        moveInterval = 150;
        spawnFood();
    };

    resetGame();

    for(;;) {
        // 1. Joystick auslesen
        int xVal = analogRead(PIN_X);
        int yVal = analogRead(PIN_Y);

        // STEUERUNG mit großer "Toter Zone" (ignore 400 bis 3700)
        // Wir ändern die Richtung NUR bei extremem Ausschlag
        if (xVal < 400) {
            if (dir.x == 0) { dir.x = -1; dir.y = 0; } // Links
        } 
        else if (xVal > 3700) {
            if (dir.x == 0) { dir.x = 1; dir.y = 0; }  // Rechts
        } 
        else if (yVal < 400) {
            if (dir.y == 0) { dir.y = -1; dir.x = 0; } // Oben
        } 
        else if (yVal > 3700) {
            if (dir.y == 0) { dir.y = 1; dir.x = 0; }  // Unten
        }

        // 2. Bewegung (Zeitgesteuert)
        if (millis() - lastMoveTime > moveInterval) {
            lastMoveTime = millis();

            int nextX = snake[0].x + dir.x;
            int nextY = snake[0].y + dir.y;

            // Wand-Kollision
            if (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL) {
                goto snakeGameOver;
            }

            // Selbst-Kollision
            for (int i = 0; i < snakeLength; i++) {
                if (nextX == snake[i].x && nextY == snake[i].y) goto snakeGameOver;
            }

            // Futter checken
            if (nextX == food.x && nextY == food.y) {
                if (snakeLength < 100) snakeLength++;
                spawnFood();
                if (moveInterval > 70) moveInterval -= 2;
            }

            // Körper bewegen
            for (int i = snakeLength - 1; i > 0; i--) {
                snake[i] = snake[i - 1];
            }
            snake[0] = {nextX, nextY};

            // 3. Zeichnen
            FastLED.clear();
            
            // Rahmen
            for(int x=0; x<WIDTH; x++) { setPixel(x, 0, CRGB(2, 2, 10)); setPixel(x, 15, CRGB(2, 2, 10)); }
            for(int y=0; y<HEIGHT_TOTAL; y++) { setPixel(0, y, CRGB(2, 2, 10)); setPixel(31, y, CRGB(2, 2, 10)); }

            setPixel(food.x, food.y, CRGB::Red); // Futter
            for (int i = 0; i < snakeLength; i++) {
                setPixel(snake[i].x, snake[i].y, (i == 0) ? CRGB::White : CRGB::Green);
            }
            FastLED.show();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;

        snakeGameOver:
            for(int i=0; i<snakeLength; i++) setPixel(snake[i].x, snake[i].y, CRGB::Red);
            FastLED.show();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            resetGame();
    }
}
void setup() {
    Serial.begin(115200);
    pinMode(PIN_BTN, INPUT_PULLUP);

    // FastLED Setup
    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben, 256);
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten, 256);
    FastLED.setBrightness(15);
    FastLED.clear(true);

    randomSeed(analogRead(0));

    // Tasks erstellen
    xTaskCreate(taskA, "TaskA", 2048, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskB", 2048, NULL, 1, &handleB);
    xTaskCreate(taskC, "SnakeTask", 4096, NULL, 1, &handleC);

    delay(1000); 

    // Startzustand: Nur Task A aktiv
    if(handleB) vTaskSuspend(handleB);
    if(handleC) vTaskSuspend(handleC);
}

unsigned long drueckStartZeit = 0;
bool wurdeUmschaltungAusgeloest = false;
const unsigned long LANG_KLICK_SCHWELLE = 1000;

void loop() {
  meinJoystick.klickenErkennen();

  if (meinJoystick.isPressed()) {
    if (drueckStartZeit == 0) drueckStartZeit = millis();

    if (!wurdeUmschaltungAusgeloest && (millis() - drueckStartZeit >= LANG_KLICK_SCHWELLE)) {
      aktuellerModus = (aktuellerModus + 1) % 3;
      wurdeUmschaltungAusgeloest = true;

      Serial.printf("\n>>> WECHSEL ZU MODUS %d <<<\n", aktuellerModus);

      if(handleA) vTaskSuspend(handleA);
      if(handleB) vTaskSuspend(handleB);
      if(handleC) vTaskSuspend(handleC);

      if (aktuellerModus == 0 && handleA) vTaskResume(handleA);
      else if (aktuellerModus == 1 && handleB) vTaskResume(handleB);
      else if (aktuellerModus == 2 && handleC) vTaskResume(handleC);

      // Paneele beim Umschalten kurz leeren
      FastLED.clear(true);
    }
  } else {
    drueckStartZeit = 0;
    wurdeUmschaltungAusgeloest = false;
  }
  vTaskDelay(10 / portTICK_PERIOD_MS);
}