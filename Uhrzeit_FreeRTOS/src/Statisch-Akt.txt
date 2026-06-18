#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>
#include "Joystick.h"

// --- Hardware-Konfiguration ---
#define LED_PIN_OBEN   26
#define LED_PIN_UNTEN  25
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define MATRIX_WIDTH   32
#define MATRIX_HEIGHT  8
#define MATRIX_TYPE    VERTICAL_ZIGZAG_MATRIX

#define DHTPIN 21
#define DHTTYPE DHT22

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
TaskHandle_t handleB = NULL; // Platzhalter
TaskHandle_t handleC = NULL; // Snake
TaskHandle_t handleData = NULL; // DHT & Cloud

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;
cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;
cLEDText AnzeigeOben;
cLEDText AnzeigeUnten;

char datumBuffer[40];
char zeitBuffer[40];
int aktuellerModus = 0;
// --- Task A: Wechselnde Anzeige (Uhrzeit / Datum) über beide Zeilen ---
void taskA(void * pvParameters) {
    struct tm timeinfo;

    for(;;) {
        if (getLocalTime(&timeinfo)) {
            FastLED.clear();

            // Wechsel alle 3 Sekunden (6000ms Zyklus)
            if (millis() % 6000 < 3000) { 
                // --- PHASE 1: UHRZEIT ---
                // Oben: Stunden:Minuten (Zentriert sich bei 5 Zeichen fast von selbst)
                sprintf(datumBuffer, "\x02%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                // Unten: Sekunden (leicht eingerückt, damit sie unter den Minuten stehen)
                sprintf(zeitBuffer, "\x02   %02d", timeinfo.tm_sec);
            } else {
                // --- PHASE 2: DATUM ---
                // Oben: Das Jahr (wie gewünscht über dem Datum)
                sprintf(datumBuffer, "\x02 %04d", timeinfo.tm_year + 1900);
                // Unten: Tag und Monat
                sprintf(zeitBuffer, "\x02%02d.%02d.", timeinfo.tm_mday, timeinfo.tm_mon + 1);
            }
        }

        // Text an die Matrix-Objekte übergeben
        AnzeigeOben.SetText((unsigned char *)datumBuffer, strlen(datumBuffer));
        AnzeigeUnten.SetText((unsigned char *)zeitBuffer, strlen(zeitBuffer));
        
        // Anzeigen aktualisieren
        AnzeigeOben.UpdateText();
        AnzeigeUnten.UpdateText();
        FastLED.show();

        // Kurze Pause für die Stabilität
        vTaskDelay(500 / portTICK_PERIOD_MS); 
    }
}
// --- Platzhalter Tasks ---
void taskB(void * pvParameters) { for(;;) vTaskDelay(1000 / portTICK_PERIOD_MS); }
void taskC(void * pvParameters) { for(;;) vTaskDelay(1000 / portTICK_PERIOD_MS); }

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

    // Warten auf Zeit-Sync für Google Auth
    struct tm timeinfo;
    while(!getLocalTime(&timeinfo)){ delay(500); }

    AnzeigeOben.SetFont(MatriseFontData);
    // x-Offset auf 1 gesetzt, um Text minimal einzurücken
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 1, 0);
    
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 1, 0);

    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff);

    xTaskCreate(taskA, "TaskUhr", 4096, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskB", 2048, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskC", 4096, NULL, 1, &handleC);

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