#include "TaskUhr.h"
#include <FastLED.h>
#include "HardwareUtils.h"

// Zwei Ziffern (mit fuehrender Null) ab x zeichnen
static void zweiZiffern(int x, int y, int wert, CRGB c) {
    drawDigitW(x, y, (wert / 10) % 10, c);
    drawDigitW(x + 4, y, wert % 10, c);
}

void taskUhr(void *pvParameters) {
    struct tm timeinfo;
    Serial.println("TaskUhr: Gestartet.");

    for (;;) {
        FastLED.clear();
        unsigned long jetzt = millis();
        CRGB col = themeCol(jetzt / 40); // Theme-Farbe, sanft wandernd -> einheitliches Design

        if (getLocalTime(&timeinfo, 10)) {
            bool zeitPhase = (jetzt % 8000) < 4000; // 4s Uhrzeit, dann 4s Datum

            if (zeitPhase) {
                // ---- UHRZEIT: HH:MM oben ----
                zweiZiffern(6, 2, timeinfo.tm_hour, col);
                zweiZiffern(18, 2, timeinfo.tm_min, col);
                if ((jetzt / 500) % 2) { // Doppelpunkt blinkt im Sekundentakt
                    setPixel(15, 3, col);
                    setPixel(15, 5, col);
                }
                // ---- Sekunden: Ziffern + Fortschrittsbalken unten ----
                zweiZiffern(12, 9, timeinfo.tm_sec, col);
                int bw = (timeinfo.tm_sec * 32) / 60;
                for (int x = 0; x < bw; x++) setPixel(x, 15, col);
            } else {
                // ---- DATUM: TT.MM oben, Jahr unten ----
                zweiZiffern(6, 2, timeinfo.tm_mday, col);
                setPixel(15, 6, col); // Trenn-Punkt
                zweiZiffern(18, 2, timeinfo.tm_mon + 1, col);

                int jahr = timeinfo.tm_year + 1900;
                drawDigitW(8, 9, (jahr / 1000) % 10, col);
                drawDigitW(12, 9, (jahr / 100) % 10, col);
                drawDigitW(16, 9, (jahr / 10) % 10, col);
                drawDigitW(20, 9, jahr % 10, col);
            }
        } else {
            // Noch kein NTP-Sync: "NTP" anzeigen (themed)
            drawChar3x5(7, 6, 'N', col);
            drawChar3x5(13, 6, 'T', col);
            drawChar3x5(19, 6, 'P', col);
            if ((jetzt / 400) % 2) { // kleiner Lebenszeichen-Punkt
                setPixel(15, 13, col);
            }
        }

        FastLED.show();
        vTaskDelay(150 / portTICK_PERIOD_MS);
    }
}
