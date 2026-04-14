#include <Arduino.h>

#include <FastLED.h>

#include <LEDMatrix.h>

#include <LEDText.h>

#include <FontMatrise.h>

#include <DHT.h>

#include "Joystick.h"
 
// --- Hardware-Konfiguration (Beibehaltung deiner Spezifikationen) ---

#define LED_PIN_OBEN   26

#define LED_PIN_UNTEN  25

#define COLOR_ORDER    GRB

#define CHIPSET        WS2812B

#define MATRIX_WIDTH   32

#define MATRIX_HEIGHT  8

#define MATRIX_TYPE    VERTICAL_ZIGZAG_MATRIX
 
// --- DHT22 Konfiguration ---

#define DHTPIN         4    

#define DHTTYPE        DHT22

DHT dht(DHTPIN, DHTTYPE);
 
// --- Joystick Pins ---

const int PIN_X = 34;

const int PIN_Y = 35;

const int PIN_BTN = 32;
 
// --- Instanzen & Handles ---

Joystick meinJoystick(PIN_X, PIN_Y, PIN_BTN);

TaskHandle_t handleA = NULL, handleB = NULL, handleC = NULL, handleD = NULL;
 
// Matrizen & Text-Objekte (Mapping für gedrehtes Paneel inklusive)

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;

cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;

cLEDText AnzeigeOben;

cLEDText AnzeigeUnten;
 
char bufferOben[64];

char bufferUnten[64];

int aktuellerModus = 0;
 
// --- Platzhalter für die anderen Teammitglieder ---

void taskA(void * p) { for(;;) { vTaskDelay(1000 / portTICK_PERIOD_MS); } }

void taskB(void * p) { for(;;) { vTaskDelay(1000 / portTICK_PERIOD_MS); } }

void taskC(void * p) { for(;;) { vTaskDelay(1000 / portTICK_PERIOD_MS); } }
 
// --- OPTIMIERTER Task D (DHT22 Sensor & Display) ---

void taskD(void * pvParameters) {

    // Dem Sensor Zeit geben, sich nach dem Einschalten zu stabilisieren

    vTaskDelay(2000 / portTICK_PERIOD_MS); 

    dht.begin();

    vTaskDelay(500 / portTICK_PERIOD_MS);
 
    for(;;) {

        // Sensordaten lesen

        float h = dht.readHumidity();

        vTaskDelay(100 / portTICK_PERIOD_MS); // Kleine Pause zwischen den Registern

        float t = dht.readTemperature();
 
        if (isnan(h) || isnan(t)) {

            // Fehlermeldung bei ungültigen Daten

            snprintf(bufferOben, 64, "\x02  SENSOR FEHLER");

            snprintf(bufferUnten, 64, "\x02  PRUEFE KABEL");

        } else {

            // Normale Anzeige (\x02 nutzt die Matrise Font)

            snprintf(bufferOben, 64, "\x02    Temp: %.1f C", t);

            snprintf(bufferUnten, 64, "\x02    Humi: %.0f %%", h);

        }
 
        AnzeigeOben.SetText((unsigned char *)bufferOben, strlen(bufferOben));

        AnzeigeUnten.SetText((unsigned char *)bufferUnten, strlen(bufferUnten));
 
        // Scroll-Animation

        bool fertig = false;

        unsigned long timeout = millis();

        while (!fertig) {

            int s1 = AnzeigeOben.UpdateText();

            int s2 = AnzeigeUnten.UpdateText();

            FastLED.show();

            vTaskDelay(40 / portTICK_PERIOD_MS); 

            // Wenn Text durchgelaufen ist oder nach 8 Sekunden (Sicherheit)

            if ((s1 == -1 && s2 == -1) || (millis() - timeout > 8000)) {

                fertig = true;

            }

        }
 
        // Der DHT22 braucht zwingend eine Pause von ca. 2 Sekunden

        vTaskDelay(2000 / portTICK_PERIOD_MS); 

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
 
    // Matrix & Font Setup

    AnzeigeOben.SetFont(MatriseFontData);

    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 0, 0);

    AnzeigeUnten.SetFont(MatriseFontData);

    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 0, 0);
 
    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff); // Weiß

    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff); // Cyan
 
    // FreeRTOS Tasks erstellen

    xTaskCreate(taskA, "TaskA", 2048, NULL, 1, &handleA);

    xTaskCreate(taskB, "TaskB", 2048, NULL, 1, &handleB);

    xTaskCreate(taskC, "TaskC", 4096, NULL, 1, &handleC);

    xTaskCreate(taskD, "TaskD", 4096, NULL, 1, &handleD);
 
    vTaskDelay(500 / portTICK_PERIOD_MS);
 
    // Start-Konfiguration: Nur Task A aktiv

    if(handleB) vTaskSuspend(handleB);

    if(handleC) vTaskSuspend(handleC);

    if(handleD) vTaskSuspend(handleD);

}
 
unsigned long drueckStartZeit = 0;

bool wurdeUmschaltungAusgeloest = false;

const unsigned long LANG_KLICK_SCHWELLE = 1000;
 
void loop() {

    meinJoystick.klickenErkennen();
 
    if (meinJoystick.isPressed()) {

        if (drueckStartZeit == 0) drueckStartZeit = millis();
 
        if (!wurdeUmschaltungAusgeloest && (millis() - drueckStartZeit >= LANG_KLICK_SCHWELLE)) {

            aktuellerModus = (aktuellerModus + 1) % 4; 

            wurdeUmschaltungAusgeloest = true;
 
            Serial.printf("Wechsel zu Modus: %d\n", aktuellerModus);
 
            if(handleA) vTaskSuspend(handleA);

            if(handleB) vTaskSuspend(handleB);

            if(handleC) vTaskSuspend(handleC);

            if(handleD) vTaskSuspend(handleD);
 
            if (aktuellerModus == 0) vTaskResume(handleA);

            else if (aktuellerModus == 1) vTaskResume(handleB);

            else if (aktuellerModus == 2) vTaskResume(handleC);

            else if (aktuellerModus == 3) vTaskResume(handleD);
 
            FastLED.clear();

            FastLED.show();

        }

    } else {

        drueckStartZeit = 0;

        wurdeUmschaltungAusgeloest = false;

    }

    vTaskDelay(10 / portTICK_PERIOD_MS);

}
 