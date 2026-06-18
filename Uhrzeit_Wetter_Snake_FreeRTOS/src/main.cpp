#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h> 
#include <ArduinoJson.h> 
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

// --- WLAN & Wetter Daten ---
const char *ssid = "Nothin";
const char *password = "nothin099";
const char *weatherApiKey = "343df2364dc5541a3efd274bf2f845df";
const char *weatherStadt = "Innsbruck";

// --- Instanzen & Handles ---
Joystick meinJoystick(PIN_X, PIN_Y, PIN_BTN);
TaskHandle_t handleA = NULL;
TaskHandle_t handleB = NULL;
TaskHandle_t handleC = NULL;

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;
cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;
cLEDText AnzeigeOben;
cLEDText AnzeigeUnten;

// --- Gemeinsame Variablen ---
int aktuellerModus = 0; // 0 = Uhrzeit, 1 = Wetter, 2 = Snake

// Buffers für Task A (Uhrzeit)
char datumBuffer[40];
char zeitBuffer[40];

// Variablen für Task B (Wetter)
int weatherID = 800; 
float aktuelleTemp = 0.0;

// Struktur für Task C
struct Point { int x, y; };

// --- Gemeinsame Mapping-Funktion ---
void setPixel(int x, int y, CRGB color) {
  if (y < 0 || y >= 16 || x < 0 || x >= 32) return;
  if (y < 8) { 
    int index = (x % 2 == 0) ? (x * 8 + y) : (x * 8 + (7 - y));
    ledsOben[0][index] = color;
  } 
  else { 
    int vX = 31 - x;
    int vY = 7 - (y - 8);
    int index = (vX % 2 == 0) ? (vX * 8 + vY) : (vX * 8 + (7 - vY));
    ledsUnten[0][index] = color;
  }
}

// Hilfsfunktion für Wetter-Zahlen (Task B)
void drawDigit(int x, int y, int n, CRGB c) {
    if(n == 0) { for(int i=0; i<5; i++) { setPixel(x,y+i,c); setPixel(x+2,y+i,c); } setPixel(x+1,y,c); setPixel(x+1,y+4,c); }
    else if(n == 1) { for(int i=0; i<5; i++) setPixel(x+2, y+i, c); }
    else if(n == 2) { for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x+2,y+1,c); setPixel(x,y+3,c); }
    else if(n == 3) { for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x+2,y+1,c); setPixel(x+2,y+3,c); }
    else if(n == 4) { for(int i=0; i<3; i++) setPixel(x,y+i,c); for(int i=0; i<5; i++) setPixel(x+2,y+i,c); setPixel(x+1,y+2,c); }
    else if(n == 5) { for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x,y+1,c); setPixel(x+2,y+3,c); }
    else if(n == 6) { for(int i=0; i<5; i++) setPixel(x,y+i,c); for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x+2,y+3,c); }
    else if(n == 7) { for(int i=0; i<5; i++) setPixel(x+2,y+i,c); setPixel(x,y,c); setPixel(x+1,y,c); }
    else if(n == 8) { for(int i=0; i<5; i++) { setPixel(x,y+i,c); setPixel(x+2,y+i,c); } for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } }
    else if(n == 9) { for(int i=0; i<5; i++) setPixel(x+2,y+i,c); for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x,y+1,c); }
}

// --- TASK A: UHRZEIT ---
void taskA(void * pvParameters) {
    struct tm timeinfo;
    for(;;) {
        if (getLocalTime(&timeinfo)) {
            sprintf(datumBuffer, "\x02       %02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            sprintf(zeitBuffer, "\x02       %02d:%02d:%02d  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
        if (AnzeigeOben.UpdateText() == -1) AnzeigeOben.SetText((unsigned char *)datumBuffer, strlen(datumBuffer));
        if (AnzeigeUnten.UpdateText() == -1) AnzeigeUnten.SetText((unsigned char *)zeitBuffer, strlen(zeitBuffer));
        FastLED.show();
        vTaskDelay(30 / portTICK_PERIOD_MS); 
    }
}

// --- TASK B: WETTER ---
void taskB(void * pvParameters) {
    auto fetchWetter = [&]() {
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(weatherStadt) + "&appid=" + String(weatherApiKey) + "&units=metric";
            http.begin(url);
            if (http.GET() == 200) {
                JsonDocument doc;
                deserializeJson(doc, http.getString());
                aktuelleTemp = doc["main"]["temp"];
                weatherID = doc["weather"][0]["id"];
            }
            http.end();
        }
    };
    fetchWetter();
    unsigned long lastAPIUpdate = millis();
    for(;;) {
        if (millis() - lastAPIUpdate > 900000) { fetchWetter(); lastAPIUpdate = millis(); }
        FastLED.clear();
        CRGB txtCol = CRGB::Cyan;
        for(int y=1; y<6; y++) setPixel(16, y, txtCol); 
        for(int y=1; y<6; y++) { setPixel(18, y, txtCol); setPixel(21, y, txtCol); }
        setPixel(19, 2, txtCol); setPixel(20, 3, txtCol);
        for(int y=1; y<6; y++) { setPixel(23, y, txtCol); setPixel(26, y, txtCol); }
        setPixel(24, 2, txtCol); setPixel(25, 3, txtCol);
        for(int x=28; x<31; x++) { setPixel(x, 1, txtCol); setPixel(x, 3, txtCol); setPixel(x, 5, txtCol); }
        setPixel(28, 2, txtCol); setPixel(30, 4, txtCol);
        int t = abs((int)aktuelleTemp);
        drawDigit(16, 10, t / 10, CRGB::White);
        drawDigit(20, 10, t % 10, CRGB::White);
        setPixel(24, 10, CRGB::White);
        for(int y=10; y<15; y++) setPixel(26, y, CRGB::White);
        for(int x=27; x<30; x++) { setPixel(x, 10, CRGB::White); setPixel(x, 14, CRGB::White); } 
        setPixel(30, 11, CRGB::White); setPixel(30, 13, CRGB::White);
        if (weatherID == 800) { for(int x=4; x<10; x++) for(int y=3; y<9; y++) setPixel(x, y, CRGB::Yellow); }
        else if (weatherID >= 500 && weatherID <= 531) { 
            for(int x=2; x<12; x++) for(int y=4; y<7; y++) setPixel(x, y, CRGB::Gray);
            static int rY = 0; rY = (rY + 1) % 5;
            setPixel(5, 8 + rY, CRGB::Blue); setPixel(10, 8 + ((rY+2)%5), CRGB::Blue);
        } else { for(int x=2; x<12; x++) for(int y=4; y<8; y++) setPixel(x, y, CRGB::Gray); }
        FastLED.show();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// --- TASK C: SNAKE ---
void taskC(void * pvParameters) {
    Point snake[100]; int snakeLength = 3; Point dir = {1, 0}; Point food;
    int moveInterval = 150; unsigned long lastMoveTime = 0;
    auto spawnFood = [&]() { food.x = random(1, WIDTH - 1); food.y = random(1, HEIGHT_TOTAL - 1); };
    auto resetGame = [&]() { snakeLength = 3; snake[0] = {15, 8}; snake[1] = {14, 8}; snake[2] = {13, 8}; dir = {1, 0}; moveInterval = 150; spawnFood(); };
    resetGame();
    for(;;) {
        int xVal = analogRead(PIN_X); int yVal = analogRead(PIN_Y);
        if (xVal < 400 && dir.x == 0) { dir.x = -1; dir.y = 0; }
        else if (xVal > 3700 && dir.x == 0) { dir.x = 1; dir.y = 0; }
        else if (yVal < 400 && dir.y == 0) { dir.y = -1; dir.x = 0; }
        else if (yVal > 3700 && dir.y == 0) { dir.y = 1; dir.x = 0; }
        if (millis() - lastMoveTime > moveInterval) {
            lastMoveTime = millis();
            int nextX = snake[0].x + dir.x; int nextY = snake[0].y + dir.y;
            bool dead = (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL);
            for (int i = 0; i < snakeLength; i++) if (nextX == snake[i].x && nextY == snake[i].y) dead = true;
            if (dead) { resetGame(); vTaskDelay(1000 / portTICK_PERIOD_MS); }
            else {
                if (nextX == food.x && nextY == food.y) { if (snakeLength < 100) snakeLength++; spawnFood(); if (moveInterval > 70) moveInterval -= 2; }
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
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWLAN verbunden!");
    configTime(3600, 3600, "pool.ntp.org");

    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 0, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 0, 0);

    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff);

    // Tasks erstellen
    xTaskCreate(taskA, "TaskUhr", 4096, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskWetter", 8192, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskSnake", 4096, NULL, 1, &handleC);

    // --- WICHTIG: Start-Konfiguration ---
    vTaskSuspend(handleB); // Wetter pausieren
    vTaskSuspend(handleC); // Snake pausieren
    // handleA (Uhrzeit) läuft!
    
    Serial.println("System bereit. Startmodus: UHRZEIT");
}

void loop() {
    meinJoystick.klickenErkennen();
    static unsigned long drueckStartZeit = 0;
    static bool umschaltungSperre = false;

    if (meinJoystick.isPressed()) {
        if (drueckStartZeit == 0) drueckStartZeit = millis();
        if (!umschaltungSperre && (millis() - drueckStartZeit >= 1000)) {
            aktuellerModus = (aktuellerModus + 1) % 3;
            umschaltungSperre = true;

            // Alle Tasks pausieren, um Kollision zu vermeiden
            vTaskSuspend(handleA);
            vTaskSuspend(handleB);
            vTaskSuspend(handleC);

            FastLED.clear();
            FastLED.show();
            delay(50); // Kurze Pause für Hardware-Stabilität

            Serial.print("--- MODUS WECHSEL: ");
            if (aktuellerModus == 0) { vTaskResume(handleA); Serial.println("UHRZEIT ---"); }
            else if (aktuellerModus == 1) { vTaskResume(handleB); Serial.println("WETTER ---"); }
            else if (aktuellerModus == 2) { vTaskResume(handleC); Serial.println("SNAKE ---"); }
        }
    } else {
        drueckStartZeit = 0;
        umschaltungSperre = false;
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
}