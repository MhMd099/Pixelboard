#include "TaskDHT.h"
#include "Config.h"
#include "HardwareUtils.h"
#include <DHT.h>
#include <ESP_Google_Sheet_Client.h>
#include <FontMatrise.h>

#define DHTPIN         21
#define DHTTYPE        DHT22

DHT dht(DHTPIN, DHTTYPE);

// Verweist auf die Variablen und Funktionen aus der main.cpp
extern int aktiverTask; 
extern void tokenStatusCallback(TokenInfo info); // <--- Hier ist der Fix!

void taskDataHandler(void *pvParameters) {
    dht.begin();
    GSheet.setTokenCallback(tokenStatusCallback);
    GSheet.setPrerefreshSeconds(10 * 60);
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    unsigned long lastLogTime = 0;
    char bufferOben[64], bufferUnten[64];

    for (;;) {
        float h = dht.readHumidity();
        float t = dht.readTemperature();

        // 1. DISPLAY AKTUALISIEREN (Nur wenn im Menü-Modus 3)
        if (aktiverTask == 3) {
            if (!isnan(h) && !isnan(t)) {
                snprintf(bufferOben, 64, "\x02 %.1f C", t); 
                snprintf(bufferUnten, 64, "\x02 %.0f %%", h);
            } else {
                snprintf(bufferOben, 64, "\x02 FEHLER");
                snprintf(bufferUnten, 64, "\x02 SENSOR");
            }

            FastLED.clear();
            AnzeigeOben.SetText((unsigned char *)bufferOben, strlen(bufferOben));
            AnzeigeUnten.SetText((unsigned char *)bufferUnten, strlen(bufferUnten));
            AnzeigeOben.UpdateText();
            AnzeigeUnten.UpdateText();
            FastLED.show();
        }

        // 2. GOOGLE SHEETS LOGGING (Läuft IMMER im Hintergrund)
        if (GSheet.ready() && (millis() - lastLogTime > 60000)) {
            lastLogTime = millis();
            FirebaseJson valueRange;
            valueRange.add("majorDimension", "COLUMNS");
            time_t now;
            time(&now);
            valueRange.set("values/[0]/[0]", (uint32_t)now);
            valueRange.set("values/[1]/[0]", t);
            valueRange.set("values/[2]/[0]", h);

            FirebaseJson response;
            GSheet.values.append(&response, spreadsheetId, "Sheet1!A1", &valueRange);
            Serial.println("Cloud-Log gesendet.");
        }

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}