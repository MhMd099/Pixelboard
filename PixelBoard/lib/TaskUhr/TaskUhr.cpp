#include "TaskUhr.h"
#include <FastLED.h>
#include "HardwareUtils.h"

void taskUhr(void * pvParameters) {
    struct tm timeinfo;
    char b1[40], b2[40];
    
    // Debug-Ausgabe beim Start des Tasks
    Serial.println("TaskUhr: Gestartet.");

    for(;;) {
        // Versuche die lokale Zeit vom ESP32-System zu holen
        if (getLocalTime(&timeinfo, 10)) { // 10ms internes Blockieren max.
            FastLED.clear();
            
            // Wechselt alle 3 Sekunden zwischen (Zeit + Sekunden) und (Jahr + Datum)
            if (millis() % 6000 < 3000) {
                sprintf(b1, "\x02%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                sprintf(b2, "\x02   %02d", timeinfo.tm_sec);
            } else {
                sprintf(b1, "\x02 %04d", timeinfo.tm_year + 1900);
                sprintf(b2, "\x02%02d.%02d.", timeinfo.tm_mday, timeinfo.tm_mon + 1);
            }
            
            AnzeigeOben.SetText((unsigned char *)b1, strlen(b1));
            AnzeigeUnten.SetText((unsigned char *)b2, strlen(b2));
            AnzeigeOben.UpdateText(); 
            AnzeigeUnten.UpdateText();
            FastLED.show();
            
            vTaskDelay(500 / portTICK_PERIOD_MS); // Schnellerer Intervall für flüssige Sekunden
        } else {
            // Wenn noch kein Sync vom NTP-Server da ist
            FastLED.clear();
            const char* m = "WAIT NTP";
            AnzeigeOben.SetText((unsigned char *)m, strlen(m));
            AnzeigeOben.UpdateText();
            
            // Unten leeren
            AnzeigeUnten.SetText((unsigned char *)" ", 1);
            AnzeigeUnten.UpdateText();
            
            FastLED.show();
            
            Serial.println("TaskUhr: Warte auf NTP Sync...");
            // Wenn kein Sync da ist, warten wir länger (2 Sekunden), um das Netzwerk nicht zu stressen
            vTaskDelay(2000 / portTICK_PERIOD_MS); 
        }
    }
}