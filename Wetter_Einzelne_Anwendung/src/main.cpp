#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>

// --- Hardware (wie gehabt) ---
#define LED_PIN_OBEN   26
#define LED_PIN_UNTEN  25
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define MATRIX_WIDTH   32
#define MATRIX_HEIGHT  8
#define MATRIX_TYPE    VERTICAL_ZIGZAG_MATRIX

// --- WLAN & API ---
const char* ssid = "Nothin";
const char* password = "nothin099";
const char* apiKey = "343df2364dc5541a3efd274bf2f845df"; 

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;
cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;
cLEDText AnzeigeOben;
cLEDText AnzeigeUnten;

char bufferOben[32];
char bufferUnten[32];
String stadt = "Wien"; 

void setup() {
    Serial.begin(115200);

    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(15);
    FastLED.clear(true);

    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 0, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 0, 0);

    // Farben: Cyan für den Ort, Weiß für die Temperatur
    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff); 
    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff); 

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }

    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + stadt + "&appid=" + String(apiKey) + "&units=metric&lang=de";
    
    http.begin(url);
    if (http.GET() == 200) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, http.getString());
        float temp = doc["main"]["temp"];

        // 1. OBERE ZEILE: Statischer Ort (Zentriert durch Leerzeichen)
        String stadtUpper = stadt;
        stadtUpper.toUpperCase();
        // Wir nehmen kein \x02, damit es fest steht
        sprintf(bufferOben, "  %s", stadtUpper.c_str());
        
        // 2. UNTERE ZEILE: Statische Temperatur (z.B. "  19 °C")
        // %.0f rundet auf ganze Zahlen. 
        // Falls das ° nicht angezeigt wird, probier mal sprintf(bufferUnten, "  %.0f *C", temp);
        sprintf(bufferUnten, "   %.0f °C", temp);
        
        AnzeigeOben.SetText((unsigned char *)bufferOben, strlen(bufferOben));
        AnzeigeUnten.SetText((unsigned char *)bufferUnten, strlen(bufferUnten));
    }
    http.end();
}

void loop() {
    FastLED.clear();

    // Ohne \x02 im Buffer zeichnet UpdateText() den Text einfach fest an (0,0)
    AnzeigeOben.UpdateText(); 
    AnzeigeUnten.UpdateText();

    FastLED.show();
    delay(200); // Höheres Delay reicht völlig aus, da nichts scrollt
}