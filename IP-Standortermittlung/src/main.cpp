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

// --- Netzwerk-Konfiguration ---
const char *ssid = "Nothin";
const char *password = "nothin099";
const char *weatherApiKey = "343df2364dc5541a3efd274bf2f845df";

// Dynamische Stadt-Variable mit Default-Wert "Prag"
String weatherStadt = "Prag"; 

// Joystick-Instanz mit dem von dir geforderten 4. Parameter (PULLUP-Modus)
Joystick meinJoystick(PIN_X, PIN_Y, PIN_BTN, INPUT_PULLUP);

TaskHandle_t handleA = NULL;
TaskHandle_t handleB = NULL;
TaskHandle_t handleC = NULL;

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;
cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;

int weatherID = 800; 
float aktuelleTemp = 0.0;
int aktuellerModus = 1; // Direkt auf Modus 1 (Wetter) gesetzt

// --- Custom-Hardware-Mapping ---
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

// Schmale 3x5 Zahlen-Anzeige
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

// --- Task B: DYNAMISCHES WETTER MIT GEO-IP ---
void taskB(void * pvParameters) {
    // 1. Lokale Standortermittlung über IP-Infrastruktur
    auto fetchLocation = [&]() {
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            Serial.println("[GEO-IP] Kontaktiere ip-api.com...");
            http.begin("http://ip-api.com/json/?fields=status,city");
            
            int httpCode = http.GET();
            if (httpCode == 200) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, http.getString());
                if (!error && doc["status"].as<String>() == "success") {
                    weatherStadt = doc["city"].as<String>();
                    Serial.printf("[GEO-IP] Erfolg! Standort überschrieben mit: %s\n", weatherStadt.c_str());
                    http.end();
                    return;
                }
            }
            http.end();
        }
        Serial.printf("[GEO-IP] Fehler oder Timeout. Behalte Default-Wert: %s\n", weatherStadt.c_str());
    };

    // 2. OpenWeatherMap API-Abfrage
    auto fetchWetter = [&]() {
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            String url = "http://api.openweathermap.org/data/2.5/weather?q=" + weatherStadt + "&appid=" + String(weatherApiKey) + "&units=metric";
            Serial.printf("[WEATHER] Rufe Wetter ab für: %s...\n", weatherStadt.c_str());
            http.begin(url);
            
            int httpCode = http.GET();
            if (httpCode == 200) {
                JsonDocument doc;
                deserializeJson(doc, http.getString());
                aktuelleTemp = doc["main"]["temp"];
                weatherID = doc["weather"][0]["id"];
                Serial.printf("[WEATHER] Daten empfangen. Temp: %.1f°C, ID: %d\n", aktuelleTemp, weatherID);
            } else {
                Serial.printf("[WEATHER] HTTP-Fehlercode: %d\n", httpCode);
            }
            http.end();
        }
    };

    // Sequenzieller Ablauf vor Eintritt in die Display-Schleife
    fetchLocation();
    fetchWetter();
    unsigned long lastAPIUpdate = millis();

    for(;;) {
        // Intervall: Alle 15 Minuten Daten aktualisieren
        if (millis() - lastAPIUpdate > 900000) { 
            fetchLocation(); 
            fetchWetter(); 
            lastAPIUpdate = millis(); 
        }
        
        FastLED.clear();
        
        // --- STADTNAME "INNS" (Statisch gerendert aus deinem funktionierenden Code) ---
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

        // --- TEMPERATUR ---
        int t = abs((int)aktuelleTemp);
        drawDigit(16, 10, t / 10, CRGB::White);
        drawDigit(20, 10, t % 10, CRGB::White);
        
        setPixel(24, 10, CRGB::White); // Grad-Zeichen
        
        // Celsius (C)
        for(int y=10; y<15; y++) setPixel(26, y, CRGB::White); 
        for(int x=27; x<30; x++) { setPixel(x, 10, CRGB::White); setPixel(x, 14, CRGB::White); } 
        setPixel(30, 11, CRGB::White); setPixel(30, 13, CRGB::White); 

        // --- SYMBOL (Sonne / Wolke) ---
        if (weatherID == 800) { 
             for(int x=4; x<10; x++) for(int y=3; y<9; y++) setPixel(x, y, CRGB::Yellow);
        } else { 
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
    delay(1000);
    Serial.println("\n--- BOOT VORGANG STARTEN ---");

    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(15);
    FastLED.clear(true);

    Serial.printf("[WLAN] Verbinde mit SSID: %s\n", ssid);
    WiFi.begin(ssid, password);
    
    int wlanTimeout = 0;
    while (WiFi.status() != WL_CONNECTED && wlanTimeout < 20) { 
        delay(500); 
        Serial.print("."); 
        wlanTimeout++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WLAN] Verbindung erfolgreich hergestellt!");
        Serial.print("[WLAN] IP-Adresse: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WLAN] Verbindung fehlgeschlagen! Modus läuft offline weiter.");
    }

    // Erstellung der FreeRTOS Tasks
    xTaskCreate(taskA, "TaskA", 2048, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskB", 8192, NULL, 1, &handleB); // Wetter-Task
    xTaskCreate(taskC, "TaskC", 2048, NULL, 1, &handleC);

    // CRITICAL FIX: Task B wird beim Boot NICHT suspendiert, um direkt zu starten
    vTaskSuspend(handleA);
    vTaskSuspend(handleC);
    
    Serial.println("[SYSTEM] Init abgeschlossen. TaskB (WETTER) laeuft direkt.");
}

void loop() {
    // Gesamte Taster-Logik deaktiviert, um State-Traps zu verhindern.
    // Task B laeuft unabhaengig im FreeRTOS-Scheduler.
    vTaskDelay(500 / portTICK_PERIOD_MS);
}