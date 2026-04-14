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

// Matrizen für Task A (Text)
cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;
cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;
cLEDText AnzeigeOben;
cLEDText AnzeigeUnten;

// Buffers für Task A
char datumBuffer[60];
char zeitBuffer[60];
int aktuellerModus = 0;

// --- Hilfsstrukturen für Snake ---
struct Point { int x, y; };

// --- Gemeinsame Mapping-Funktion für Snake (Task C) ---
void setPixel(int x, int y, CRGB color) {
  if (y < 0 || y >= 16 || x < 0 || x >= 32) return;
  if (y < 8) { 
    // OBERES PANEEL
    int index;
    if (x % 2 == 0) index = x * 8 + y;
    else index = x * 8 + (7 - y);
    if (index >= 0 && index < 256) ledsOben[0][index] = color;
  } 
  else { 
    // UNTERES PANEEL (Kopfüber montiert)
    int vX = 31 - x;
    int vY = 7 - (y - 8);
    int index;
    if (vX % 2 == 0) index = vX * 8 + vY;
    else index = vX * 8 + (7 - vY);
    if (index >= 0 && index < 256) ledsUnten[0][index] = color;
  }
}

// --- Task A: Uhrzeit & Datum ---
void taskA(void * pvParameters) {
    struct tm timeinfo;
    int letzteSekunde = -1;

    for(;;) {
        int statusOben = AnzeigeOben.UpdateText();
        int statusUnten = AnzeigeUnten.UpdateText();

        if (statusOben == -1 || statusUnten == -1) {
            if (getLocalTime(&timeinfo)) {
                sprintf(datumBuffer, "\x02       %02d.%02d.%04d", 
                        timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
                sprintf(zeitBuffer, "\x02        %02d:%02d:%02d", 
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

                AnzeigeOben.SetText((unsigned char *)datumBuffer, strlen(datumBuffer));
                AnzeigeUnten.SetText((unsigned char *)zeitBuffer, strlen(zeitBuffer));
            }
        }

        if (getLocalTime(&timeinfo)) {
            if (timeinfo.tm_sec != letzteSekunde) {
                letzteSekunde = timeinfo.tm_sec;
                snprintf(zeitBuffer + 9, 9, "%02d:%02d:%02d", 
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            }
        }

        FastLED.show();
        vTaskDelay(50 / portTICK_PERIOD_MS);
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
        snake[0] = {15, 8};
        snake[1] = {14, 8};
        snake[2] = {13, 8};
        dir = {1, 0}; 
        moveInterval = 150;
        spawnFood();
    };

    resetGame();

    for(;;) {
        // 1. Joystick Steuerung mit Hysterese (Tote Zone)
        int xVal = analogRead(PIN_X);
        int yVal = analogRead(PIN_Y);

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

        // 2. Spiel-Logik
        if (millis() - lastMoveTime > moveInterval) {
            lastMoveTime = millis();

            int nextX = snake[0].x + dir.x;
            int nextY = snake[0].y + dir.y;

            // Kollisions-Check
            bool dead = false;
            if (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL) dead = true;
            for (int i = 0; i < snakeLength; i++) {
                if (nextX == snake[i].x && nextY == snake[i].y) dead = true;
            }

            if (dead) {
                for(int i=0; i<snakeLength; i++) setPixel(snake[i].x, snake[i].y, CRGB::Red);
                FastLED.show();
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                resetGame();
            } else {
                if (nextX == food.x && nextY == food.y) {
                    if (snakeLength < 100) snakeLength++;
                    spawnFood();
                    if (moveInterval > 70) moveInterval -= 2;
                }

                for (int i = snakeLength - 1; i > 0; i--) snake[i] = snake[i - 1];
                snake[0] = {nextX, nextY};
            }

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
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BTN, INPUT_PULLUP);

    // LED Setup
    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(15);
    FastLED.clear(true);

    // WiFi & Zeit
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    configTime(3600, 3600, "pool.ntp.org");

    // Matrix Init (für Task A)
    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 0, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 0, 0);

    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff);

    randomSeed(analogRead(0));

    // FreeRTOS Tasks erstellen
    xTaskCreate(taskA, "TaskUhr", 4096, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskWetter", 2048, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskSnake", 4096, NULL, 1, &handleC);

    delay(1000); 

    // Nur Task A starten
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
            
            FastLED.clear();
            FastLED.show();
        }
    } else {
        drueckStartZeit = 0;
        wurdeUmschaltungAusgeloest = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}