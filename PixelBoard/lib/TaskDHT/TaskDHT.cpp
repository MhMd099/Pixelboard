#include "TaskDHT.h"
#include <DHT.h>
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include "HardwareUtils.h" // Für AnzeigeOben, AnzeigeUnten, lockDisplay, themeCol etc.

#define DHTPIN 21
#define DHTTYPE DHT22

static DHT dht(DHTPIN, DHTTYPE);

// Zugriff auf die globalen Steuerungs-Variablen aus der main.cpp
extern volatile int aktiverTask;
extern char bufferOben[64];
extern char bufferUnten[64];

void taskDHT(void *pvParameters) {
    dht.begin();
    
    unsigned long lastReadTime = 0;
    float temp = 0.0;
    float hum = 0.0;
    bool firstRead = true;

    for (;;) {
        // Wenn dieser Task (ID 5) nicht aktiv ist, kurz schlafen und loopen
        if (aktiverTask != 5) {
            vTaskDelay(200 / portTICK_PERIOD_MS);
            continue;
        }

        // Sensor alle 2 Sekunden non-blocking auslesen
        if (firstRead || (millis() - lastReadTime >= 2000)) {
            lastReadTime = millis();
            float t = dht.readTemperature();
            float h = dht.readHumidity();

            if (!isnan(t) && !isnan(h)) {
                temp = t;
                hum = h;
                snprintf(bufferOben, sizeof(bufferOben), "\x02 %.1f C", temp);
                snprintf(bufferUnten, sizeof(bufferUnten), "\x02 %.0f %%", hum);
            } else if (firstRead) {
                snprintf(bufferOben, sizeof(bufferOben), "\x02 FEHLER");
                snprintf(bufferUnten, sizeof(bufferUnten), "\x02 SENSOR");
            }
            firstRead = false;
        }

        // Display für das Rendern sperren
        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        // Sicherheitsprüfung vor dem Zeichnen
        if (aktiverTask != 5) {
            unlockDisplay();
            continue;
        }

        FastLED.clear();

        // Text setzen und für eventuelles Scrollen/Rendering updaten
        AnzeigeOben.SetText((unsigned char *)bufferOben, strlen(bufferOben));
        AnzeigeUnten.SetText((unsigned char *)bufferUnten, strlen(bufferUnten));
        
        AnzeigeOben.UpdateText();
        AnzeigeUnten.UpdateText();

        FastLED.show();
        unlockDisplay();

        vTaskDelay(50 / portTICK_PERIOD_MS); // Taktung für flüssige Display-Ausgabe
    }
}