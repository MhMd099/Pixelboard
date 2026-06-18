#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>
#include "Joystick.h"

// --- Hardware ---
#define LED_PIN_OBEN   26
#define LED_PIN_UNTEN  25
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define MATRIX_WIDTH   32
#define MATRIX_HEIGHT  8
#define MATRIX_TYPE    VERTICAL_ZIGZAG_MATRIX

const int PIN_X = 34;
const int PIN_Y = 35;
const int PIN_BTN = 32;

const char *ssid = "Nothin";
const char *password = "nothin099";
const char *weatherApiKey = "343df2364dc5541a3efd274bf2f845df";
const char *weatherStadt = "Innsbruck";

Joystick meinJoystick(PIN_X, PIN_Y, PIN_BTN);
TaskHandle_t handleA = NULL;
TaskHandle_t handleB = NULL;
TaskHandle_t handleC = NULL;

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;
cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;

int weatherID = 800; 
float aktuelleTemp = 0.0;
int aktuellerModus = 1; 

// --- Mapping ---
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

// Schmale 3x5 Zahlen
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

// --- Task A: LEER ---
void taskA(void * pvParameters) { for(;;) { vTaskDelay(1000 / portTICK_PERIOD_MS); } }

// --- Task B: WETTER ---
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
        
        // --- STADTNAME "INNS" (ab Spalte 16, schmaler) ---
        CRGB txtCol = CRGB::Cyan;
        // I
        for(int y=1; y<6; y++) setPixel(16, y, txtCol);
        // N
        for(int y=1; y<6; y++) { setPixel(18, y, txtCol); setPixel(21, y, txtCol); }
        setPixel(19, 2, txtCol); setPixel(20, 3, txtCol);
        // N
        for(int y=1; y<6; y++) { setPixel(23, y, txtCol); setPixel(26, y, txtCol); }
        setPixel(24, 2, txtCol); setPixel(25, 3, txtCol);
        // S
        for(int x=28; x<31; x++) { setPixel(x, 1, txtCol); setPixel(x, 3, txtCol); setPixel(x, 5, txtCol); }
        setPixel(28, 2, txtCol); setPixel(30, 4, txtCol);

        // --- TEMPERATUR (unten) ---
        int t = abs((int)aktuelleTemp);
        drawDigit(16, 10, t / 10, CRGB::White);
        drawDigit(20, 10, t % 10, CRGB::White);
        
        // Grad-Symbol (°)
        setPixel(24, 10, CRGB::White);
        
        // Celsius (C) - Deutlicher
        for(int y=10; y<15; y++) setPixel(26, y, CRGB::White); // Links
        for(int x=27; x<30; x++) { setPixel(x, 10, CRGB::White); setPixel(x, 14, CRGB::White); } // Oben/Unten
        setPixel(30, 11, CRGB::White); setPixel(30, 13, CRGB::White); // Abschluss rechts

        // --- SYMBOL (links) ---
        if (weatherID == 800) { // Sonne
             for(int x=4; x<10; x++) for(int y=3; y<9; y++) setPixel(x, y, CRGB::Yellow);
        } else { // Wolke Standard
             for(int x=2; x<12; x++) for(int y=4; y<8; y++) setPixel(x, y, CRGB::Gray);
        }

        FastLED.show();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// --- Task C: LEER ---
void taskC(void * pvParameters) { for(;;) { vTaskDelay(1000 / portTICK_PERIOD_MS); } }

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(15);
    FastLED.clear(true);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWLAN verbunden!");

    xTaskCreate(taskA, "TaskA", 2048, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskB", 8192, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskC", 2048, NULL, 1, &handleC);

    vTaskSuspend(handleA);
    vTaskSuspend(handleC);
    Serial.println("System bereit. Startmodus: WETTER");
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

            vTaskSuspend(handleA); vTaskSuspend(handleB); vTaskSuspend(handleC);

            // SERIAL MONITOR AUSGABE
            Serial.print("--- MODUS WECHSEL: ");
            if (aktuellerModus == 0) { 
                vTaskResume(handleA); Serial.println("UHRZEIT ---"); 
            }
            else if (aktuellerModus == 1) { 
                vTaskResume(handleB); Serial.println("WETTER ---"); 
            }
            else if (aktuellerModus == 2) { 
                vTaskResume(handleC); Serial.println("SNAKE ---"); 
            }
            
            FastLED.clear();
            FastLED.show();
        }
    } else {
        drueckStartZeit = 0;
        umschaltungSperre = false;
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
}