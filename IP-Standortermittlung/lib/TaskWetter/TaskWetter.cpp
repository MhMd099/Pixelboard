#include "TaskWetter.h"
#include <FastLED.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "HardwareUtils.h" // Hier sind ledsOben/Unten und setPixel definiert

// API-Daten als lokale Task-Variablen
int weatherID = 800; 
float aktuelleTemp = 22.5; // Testwert

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

void taskWetter(void * pvParameters) {
    unsigned long lastAPIUpdate = 0;

    for(;;) {
        // API Call alle 5 Sekunden (5000ms)
        if (millis() - lastAPIUpdate > 5000 || lastAPIUpdate == 0) {
            // ... hier später dein HTTPClient Code ...
            Serial.println("Wetter-API Update...");
            lastAPIUpdate = millis();
        }

        FastLED.clear();

        // --- Beispiel: STADTNAME "INNS" ---
        CRGB txtCol = CRGB::Cyan;
        for(int y=1; y<6; y++) { setPixel(16, y, txtCol); setPixel(18, y, txtCol); setPixel(21, y, txtCol); }
        setPixel(19, 2, txtCol); setPixel(20, 3, txtCol);

        // --- TEMPERATUR ---
        int t = abs((int)aktuelleTemp);
        drawDigit(16, 10, t / 10, CRGB::White);
        drawDigit(20, 10, t % 10, CRGB::White);
        
        FastLED.show();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}