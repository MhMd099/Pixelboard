#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>
#include "Joystick.h"

// --- Hardware-Konfiguration LED Paneele ---
#define LED_PIN_OBEN   26
#define LED_PIN_UNTEN  25
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define MATRIX_WIDTH   32
#define MATRIX_HEIGHT  8
#define MATRIX_TYPE    VERTICAL_ZIGZAG_MATRIX
#define WIDTH          32
#define HEIGHT_TOTAL   16

// --- Joystick Pins ---
const int PIN_X = 34;
const int PIN_Y = 35;
const int PIN_BTN = 32;

// --- WLAN Daten ---
const char *ssid = "Nothin";
const char *password = "nothin099";

// --- Instanzen & Handles ---
Joystick meinJoystick(PIN_X, PIN_Y, PIN_BTN);
TaskHandle_t handleA = NULL;
TaskHandle_t handleB = NULL;
TaskHandle_t handleC = NULL;

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;
cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;
cLEDText AnzeigeOben;
cLEDText AnzeigeUnten;

// Buffers für Task A (Deine exakte Lösung)
char datumBuffer[40];
char zeitBuffer[40];
int aktuellerModus = 0;

// --- Hilfsstrukturen für Snake ---
struct Point { int x, y; };

// --- Gemeinsame Mapping-Funktion ---
void setPixel(int x, int y, CRGB color) {
  if (y < 0 || y >= 16 || x < 0 || x >= 32) return;
  if (y < 8) { 
    int index;
    if (x % 2 == 0) index = x * 8 + y;
    else index = x * 8 + (7 - y);
    if (index >= 0 && index < 256) ledsOben[0][index] = color;
  } 
  else { 
    int vX = 31 - x;
    int vY = 7 - (y - 8);
    int index;
    if (vX % 2 == 0) index = vX * 8 + vY;
    else index = vX * 8 + (7 - vY);
    if (index >= 0 && index < 256) ledsUnten[0][index] = color;
  }
}

// --- Task A: Uhrzeit & Datum (DEINE LÖSUNG) ---
void taskA(void * pvParameters) {
    struct tm timeinfo;

    for(;;) {
        // Update gibt -1 zurück, wenn die Nachricht zu Ende gescrollt ist
        if (AnzeigeOben.UpdateText() == -1) {
            if (getLocalTime(&timeinfo)) {
                // Deine exakten Blanks für das Datum
                sprintf(datumBuffer, "\x02      %02d.%02d.%04d", 
                        timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
                AnzeigeOben.SetText((unsigned char *)datumBuffer, strlen(datumBuffer));
            }
        }

        if (AnzeigeUnten.UpdateText() == -1) {
            if (getLocalTime(&timeinfo)) {
                // Deine exakten Blanks für die Zeit
                sprintf(zeitBuffer, "\x02       %02d:%02d:%02d  ", 
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                AnzeigeUnten.SetText((unsigned char *)zeitBuffer, strlen(zeitBuffer));
            }
        }

        FastLED.show();
        vTaskDelay(30 / portTICK_PERIOD_MS); // Deine originale Geschwindigkeit
    }
}

// --- Task B: Wetter (Platzhalter) ---
void taskB(void * pvParameters) {
    for(;;) {
        Serial.println("Task B (Wetter) aktiv...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// --- Task C: Snake Spiel ---
void taskC(void * pvParameters) {
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
        snake[0] = {15, 8}; snake[1] = {14, 8}; snake[2] = {13, 8};
        dir = {1, 0}; moveInterval = 150;
        spawnFood();
    };

    resetGame();

    for(;;) {
        int xVal = analogRead(PIN_X);
        int yVal = analogRead(PIN_Y);

        if (xVal < 400 && dir.x == 0) { dir.x = -1; dir.y = 0; }
        else if (xVal > 3700 && dir.x == 0) { dir.x = 1; dir.y = 0; }
        else if (yVal < 400 && dir.y == 0) { dir.y = -1; dir.x = 0; }
        else if (yVal > 3700 && dir.y == 0) { dir.y = 1; dir.x = 0; }

        if (millis() - lastMoveTime > moveInterval) {
            lastMoveTime = millis();
            int nextX = snake[0].x + dir.x;
            int nextY = snake[0].y + dir.y;

            bool dead = (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL);
            for (int i = 0; i < snakeLength; i++) if (nextX == snake[i].x && nextY == snake[i].y) dead = true;

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
            for(int x=0; x<WIDTH; x++) { setPixel(x, 0, CRGB(2, 2, 10)); setPixel(x, 15, CRGB(2, 2, 10)); }
            setPixel(food.x, food.y, CRGB::Red);
            for (int i = 0; i < snakeLength; i++) setPixel(snake[i].x, snake[i].y, (i == 0) ? CRGB::White : CRGB::Green);
            FastLED.show();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BTN, INPUT_PULLUP);

    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(15);
    FastLED.clear(true);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    configTime(3600, 3600, "pool.ntp.org");

    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 0, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 0, 0);

    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff);

    randomSeed(analogRead(0));

    xTaskCreate(taskA, "TaskUhr", 4096, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskWetter", 2048, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskSnake", 4096, NULL, 1, &handleC);

    vTaskSuspend(handleB);
    vTaskSuspend(handleC);
}

void loop() {
    meinJoystick.klickenErkennen();
    static unsigned long drueckStartZeit = 0;
    static bool wurdeUmschaltungAusgeloest = false;

    if (meinJoystick.isPressed()) {
        if (drueckStartZeit == 0) drueckStartZeit = millis();
        if (!wurdeUmschaltungAusgeloest && (millis() - drueckStartZeit >= 1000)) {
            aktuellerModus = (aktuellerModus + 1) % 3;
            wurdeUmschaltungAusgeloest = true;

            vTaskSuspend(handleA); vTaskSuspend(handleB); vTaskSuspend(handleC);

            if (aktuellerModus == 0) vTaskResume(handleA);
            else if (aktuellerModus == 1) vTaskResume(handleB);
            else if (aktuellerModus == 2) vTaskResume(handleC);
            
            FastLED.clear();
            FastLED.show();
        }
    } else {
        drueckStartZeit = 0;
        wurdeUmschaltungAusgeloest = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}