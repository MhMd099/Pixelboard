#include "TaskWetter.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "HardwareUtils.h"
#include "Config.h"

int weatherID = 800; 
float aktuelleTemp = 0.0;

// (Dein bestehendes font3x5 Array bleibt hier genau so, wie es war!)
const char* font3x5[26] = {
  "111101111101101", "110101110101110", "111100100100111", "110101101101110",
  "111100111100111", "111100111100100", "111100101101111", "101101111101101",
  "111010010010111", "001001001101111", "101110100110101", "100100100100111",
  "111111101101101", "111101101101101", "111101101101111", "111101111100100",
  "111101101111001", "111101110101101", "111100111001111", "111010010010010",
  "101101101101111", "101101101101010", "101101101111111", "101101010101101",
  "101101010010010", "111001010100111"
};

void drawChar3x5(int startX, int startY, char c, CRGB color) {
    if (c < 'A' || c > 'Z') return;
    int charIndex = c - 'A';
    const char* pattern = font3x5[charIndex];
    for (int i = 0; i < 15; i++) {
        if (pattern[i] == '1') {
            setPixel(startX + (i % 3), startY + (i / 3), color);
        }
    }
}

// (Deine bestehende drawDigitW Funktion bleibt auch komplett unverändert!)
void drawDigitW(int x, int y, int n, CRGB c) {
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

void taskWetter(void * pvParameters) {
    auto fetchWetter = [&]() {
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            
            // WICHTIG: Leerzeichen für URLs anpassen ("New York" -> "New%20York")
            String safeCity = currentCity;
            safeCity.replace(" ", "%20");

            String url = "http://api.openweathermap.org/data/2.5/weather?q=" + safeCity + "&appid=" + String(weatherApiKey) + "&units=metric";
            Serial.printf("[WETTER] Rufe Daten ab für: '%s'\n", currentCity.c_str());
            
            http.begin(url);
            int httpCode = http.GET();
            if (httpCode == 200) {
                JsonDocument doc;
                deserializeJson(doc, http.getString());
                aktuelleTemp = doc["main"]["temp"];
                weatherID = doc["weather"][0]["id"];
                Serial.printf("[WETTER] Erfolgreich! Temp: %.1f\n", aktuelleTemp);
            } else {
                Serial.printf("[WETTER] Fehler! HTTP Code: %d\n", httpCode);
            }
            http.end();
        }
    };

    // Erster Aufruf beim Start
    fetchWetter();
    unsigned long lastAPIUpdate = millis();

    for(;;) {
        // HIER IST DER MAGISCHE TRIGGER: Entweder 15 Min sind um, ODER jemand hat im Web was geändert!
        if (forceWeatherUpdate || (millis() - lastAPIUpdate > 900000)) { 
            fetchWetter(); 
            lastAPIUpdate = millis(); 
            forceWeatherUpdate = false; // Trigger wieder ausschalten
        }
        
        FastLED.clear();
        
        // --- DYNAMISCHER STADTNAME ---
        String abbr = currentCity.substring(0, 3);
        abbr.toUpperCase();
        
        CRGB txtCol = CRGB::Cyan;
        if (abbr.length() > 0) drawChar3x5(16, 1, abbr.charAt(0), txtCol);
        if (abbr.length() > 1) drawChar3x5(20, 1, abbr.charAt(1), txtCol);
        if (abbr.length() > 2) drawChar3x5(24, 1, abbr.charAt(2), txtCol);

        // --- TEMPERATUR ---
        int t = abs((int)aktuelleTemp);
        drawDigitW(16, 10, t / 10, CRGB::White);
        drawDigitW(20, 10, t % 10, CRGB::White);
        
        setPixel(24, 10, CRGB::White); // Grad-Symbol
        for(int y=10; y<15; y++) setPixel(26, y, CRGB::White); 
        for(int x=27; x<30; x++) { setPixel(x, 10, CRGB::White); setPixel(x, 14, CRGB::White); } 
        setPixel(30, 11, CRGB::White); setPixel(30, 13, CRGB::White); 

        // --- SYMBOL ---
        if (weatherID == 800) { 
             for(int x=4; x<10; x++) for(int y=3; y<9; y++) setPixel(x, y, CRGB::Yellow);
        } else { 
             for(int x=2; x<12; x++) for(int y=4; y<8; y++) setPixel(x, y, CRGB::Gray);
        }

        FastLED.show();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}