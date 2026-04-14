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

char datumBuffer[60];
char zeitBuffer[60];
int aktuellerModus = 0;

// --- Task A: Uhrzeit & Datum ---
void taskA(void * pvParameters) {
    struct tm timeinfo;
    int letzteSekunde = -1;

    for(;;) {
        // Scroll-Status prüfen
        int statusOben = AnzeigeOben.UpdateText();
        int statusUnten = AnzeigeUnten.UpdateText();

        // Synchroner Neustart am Ende des Scrollvorgangs
        if (statusOben == -1 || statusUnten == -1) {
            if (getLocalTime(&timeinfo)) {
                // Eure Lösung für das Hüpfen (Blanks am Anfang)
                sprintf(datumBuffer, "\x02       %02d.%02d.%04d", 
                        timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
                sprintf(zeitBuffer, "\x02        %02d:%02d:%02d", 
                        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

                AnzeigeOben.SetText((unsigned char *)datumBuffer, strlen(datumBuffer));
                AnzeigeUnten.SetText((unsigned char *)zeitBuffer, strlen(zeitBuffer));
            }
        }

        // Live-Update der Sekunden im laufenden Buffer
        if (getLocalTime(&timeinfo)) {
            if (timeinfo.tm_sec != letzteSekunde) {
                letzteSekunde = timeinfo.tm_sec;
                // Direktes Überschreiben der Zahlen im Buffer (Index 9)
                snprintf(zeitBuffer + 9, 9, "%02d:%02d:%02d", 
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            }
        }

        FastLED.show();
        vTaskDelay(50 / portTICK_PERIOD_MS); // Task-Delay für Scroll-Geschwindigkeit
    }
}

// --- Task B & C (Platzhalter für Wetter/Spiel) ---
void taskB(void * pvParameters) {
    for(;;) {
        Serial.println("Task B (Wetter) aktiv...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void taskC(void * pvParameters) {
    for(;;) {
        Serial.println("Task C (Spiel) aktiv...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
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

    // Matrix Init
    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 0, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 0, 0);

    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff);

    // "Warte auf Zeit" ausgeklammert
    // AnzeigeOben.SetText((unsigned char *)"Warte auf Zeit...", 17);

    // FreeRTOS Tasks erstellen (Stack-Größe für TaskA erhöht wegen Matrix/WiFi)
    xTaskCreate(taskA, "TaskUhr", 4096, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskWetter", 2048, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskSpiel", 2048, NULL, 1, &handleC);

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
            
            // Paneele löschen beim Umschalten
            FastLED.clear();
            FastLED.show();
        }
    } else {
        drueckStartZeit = 0;
        wurdeUmschaltungAusgeloest = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}