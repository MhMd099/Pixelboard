#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "Nothin";
const char* password = "nothin099";
const char* apiKey = "343df2364dc5541a3efd274bf2f845df"; 
const char* stadt = "Innsbruck";

#define LED_PIN_OBEN   26
#define LED_PIN_UNTEN  25
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define NUM_LEDS       256 
#define BRIGHTNESS     25  

CRGB ledsOben[NUM_LEDS];
CRGB ledsUnten[NUM_LEDS];

int weatherID = 800; 
float aktuelleTemp = 0.0; 

void setPixel(int x, int y, CRGB color) {
    if (x < 0 || x >= 32 || y < 0 || y >= 16) return;
    if (y < 8) { 
        int index = (x % 2 == 0) ? (x * 8 + y) : (x * 8 + (7 - y));
        ledsOben[index] = color;
    } else { 
        int vX = 31 - x;
        int vY = 7 - (y - 8);
        int index = (vX % 2 == 0) ? (vX * 8 + vY) : (vX * 8 + (7 - vY));
        ledsUnten[index] = color;
    }
}

void drawDigit(int x, int y, int n, CRGB c) {
    if(n == 0) { for(int i=0; i<5; i++) { setPixel(x, y+i, c); setPixel(x+2, y+i, c); } setPixel(x+1, y, c); setPixel(x+1, y+4, c); }
    if(n == 1) { for(int i=0; i<5; i++) setPixel(x+1, y+i, c); }
    if(n == 2) { for(int i=0; i<3; i++) { setPixel(x+i, y, c); setPixel(x+i, y+2, c); setPixel(x+i, y+4, c); } setPixel(x+2, y+1, c); setPixel(x, y+3, c); }
    if(n == 3) { for(int i=0; i<3; i++) { setPixel(x+i, y, c); setPixel(x+i, y+2, c); setPixel(x+i, y+4, c); } setPixel(x+2, y+1, c); setPixel(x+2, y+3, c); }
    if(n == 4) { for(int i=0; i<3; i++) setPixel(x, y+i, c); for(int i=0; i<5; i++) setPixel(x+2, y+i, c); setPixel(x+1, y+2, c); }
    if(n == 5) { for(int i=0; i<3; i++) { setPixel(x+i, y, c); setPixel(x+i, y+2, c); setPixel(x+i, y+4, c); } setPixel(x, y+1, c); setPixel(x+2, y+3, c); }
    if(n == 6) { for(int i=0; i<5; i++) setPixel(x, y+i, c); for(int i=0; i<3; i++) { setPixel(x+i, y, c); setPixel(x+i, y+2, c); setPixel(x+i, y+4, c); } setPixel(x+2, y+3, c); }
    if(n == 7) { for(int i=0; i<5; i++) setPixel(x+2, y+i, c); setPixel(x, y, c); setPixel(x+1, y, c); }
    if(n == 8) { for(int i=0; i<5; i++) { setPixel(x, y+i, c); setPixel(x+2, y+i, c); } for(int i=0; i<3; i++) { setPixel(x+i, y, c); setPixel(x+i, y+2, c); setPixel(x+i, y+4, c); } }
    if(n == 9) { for(int i=0; i<5; i++) setPixel(x+2, y+i, c); for(int i=0; i<3; i++) { setPixel(x+i, y, c); setPixel(x+i, y+2, c); setPixel(x+i, y+4, c); } setPixel(x, y+1, c); }
}

// --- WETTER SYMBOLE ---

void drawSun() {
    for(int x=5; x<11; x++) for(int y=5; y<11; y++) setPixel(x, y, CRGB::Yellow);
    static int r = 0; EVERY_N_MILLISECONDS(200) { r++; }
    for(int i=0; i<8; i++) {
        float a = (i*45+r*10)*3.14/180;
        setPixel(8+cos(a)*5, 8+sin(a)*5, CRGB::Orange);
    }
}

void drawCloud(CRGB c) {
    for(int x=2; x<14; x++) for(int y=4; y<9; y++) setPixel(x, y, c);
    for(int x=4; x<12; x++) setPixel(x, 3, c);
}

void drawRain() {
    drawCloud(CRGB(40, 40, 60));
    static int aY = 0; EVERY_N_MILLISECONDS(80) { aY++; }
    for(int i=0; i<4; i++) {
        int y = 9 + ((aY + i*2) % 6);
        setPixel(4 + i*3, y, CRGB::Blue);
    }
}

void drawThunder() {
    drawCloud(CRGB(30, 30, 30));
    static int b = 0; b = random8(50);
    if(b < 2) { for(int y=8; y<15; y++) setPixel(7 + (y%2), y, CRGB::White); }
}

void drawSnow() {
    drawCloud(CRGB::Gray);
    static int sY = 0; EVERY_N_MILLISECONDS(300) { sY++; }
    for(int i=0; i<3; i++) setPixel(5 + i*3, 9 + ((sY + i) % 5), CRGB::White);
}

void drawMist() {
    static int mX = 0; EVERY_N_MILLISECONDS(400) { mX++; }
    for(int y=5; y<12; y+=2) {
        for(int x=2; x<14; x++) {
            if((x + mX + y) % 4 != 0) setPixel(x, y, CRGB(50, 50, 50));
        }
    }
}

void fetchWeather() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(stadt) + "&appid=" + String(apiKey) + "&units=metric";
        http.begin(url);
        int httpCode = http.GET();
        if (httpCode == 200) {
            JsonDocument doc;
            deserializeJson(doc, http.getString());
            aktuelleTemp = doc["main"]["temp"];
            weatherID = doc["weather"][0]["id"];
        }
        http.end();
    }
}

void drawStaticText() {
    // INNS
    for(int y=1; y<6; y++) { setPixel(18, y, CRGB::Cyan); setPixel(20, y, CRGB::Cyan); setPixel(23, y, CRGB::Cyan); }
    setPixel(21, 2, CRGB::Cyan); setPixel(22, 3, CRGB::Cyan);
    setPixel(25, 2, CRGB::Cyan); setPixel(26, 3, CRGB::Cyan); // Vereinfachtes N
    for(int x=29; x<32; x++) { setPixel(x, 1, CRGB::Cyan); setPixel(x, 3, CRGB::Cyan); setPixel(x, 5, CRGB::Cyan); }
    
    // Temperatur
    int t = abs((int)aktuelleTemp);
    if(aktuelleTemp < 0) setPixel(15, 12, CRGB::White); // Minuszeichen
    drawDigit(17, 10, t / 10, CRGB::White); 
    drawDigit(21, 10, t % 10, CRGB::White); 

    setPixel(25, 10, CRGB::White);
    for(int y=11; y<14; y++) setPixel(27, y, CRGB::White);
    setPixel(28, 10, CRGB::White); setPixel(28, 14, CRGB::White);
}

void setup() {
    Serial.begin(115200);
    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben, NUM_LEDS);
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    WiFi.begin(ssid, password);
    int counter = 0;
    while (WiFi.status() != WL_CONNECTED && counter < 20) { delay(500); counter++; }
    if(WiFi.status() == WL_CONNECTED) fetchWeather();
}

void loop() {
    FastLED.clear();
    drawStaticText();

    // DYNAMISCHE LOGIK NACH WEATHER ID
    if (weatherID == 800) {
        drawSun();
    } else if (weatherID >= 200 && weatherID <= 232) {
        drawThunder();
    } else if (weatherID >= 300 && weatherID <= 531) {
        drawRain();
    } else if (weatherID >= 600 && weatherID <= 622) {
        drawSnow();
    } else if (weatherID >= 701 && weatherID <= 781) {
        drawMist();
    } else {
        drawCloud(CRGB::Gray); // Bewölkt (801-804)
        
    }

    FastLED.show();
    EVERY_N_MINUTES(15) { fetchWeather(); }
    delay(50);
}