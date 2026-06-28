#include "TaskUhr.h"
#include <FastLED.h>
#include <math.h>
#include "HardwareUtils.h"

// Zwei Ziffern (mit fuehrender Null) ab x zeichnen
static void zweiZiffern(int x, int y, int wert, CRGB c) {
    drawDigitW(x, y, (wert / 10) % 10, c);
    drawDigitW(x + 4, y, wert % 10, c);
}

// ---------- Stil 0: DIGITAL (Zeit/Datum abwechselnd) ----------
static void uhrDigital(struct tm &ti, CRGB col, unsigned long jetzt) {
    bool zeitPhase = (jetzt % 8000) < 4000;
    if (zeitPhase) {
        zweiZiffern(6, 2, ti.tm_hour, col);
        zweiZiffern(18, 2, ti.tm_min, col);
        if ((jetzt / 500) % 2) { setPixel(15, 3, col); setPixel(15, 5, col); }
        zweiZiffern(12, 9, ti.tm_sec, col);
        int bw = (ti.tm_sec * 32) / 60;
        for (int x = 0; x < bw; x++) setPixel(x, 15, col);
    } else {
        zweiZiffern(6, 2, ti.tm_mday, col);
        setPixel(15, 6, col);
        zweiZiffern(18, 2, ti.tm_mon + 1, col);
        int jahr = ti.tm_year + 1900;
        drawDigitW(8, 9, (jahr / 1000) % 10, col);
        drawDigitW(12, 9, (jahr / 100) % 10, col);
        drawDigitW(16, 9, (jahr / 10) % 10, col);
        drawDigitW(20, 9, jahr % 10, col);
    }
}

// ---------- Stil 1: BINAER (BCD, 6 Spalten HH MM SS) ----------
static void uhrBinaer(struct tm &ti, CRGB col) {
    int vals[6] = { ti.tm_hour / 10, ti.tm_hour % 10,
                    ti.tm_min / 10,  ti.tm_min % 10,
                    ti.tm_sec / 10,  ti.tm_sec % 10 };
    int colX[6] = {3, 8, 14, 19, 25, 30};
    CRGB dim = col; dim.nscale8(35);
    for (int c = 0; c < 6; c++) {
        for (int b = 0; b < 4; b++) {
            bool on = vals[c] & (1 << (3 - b));
            int y = 3 + b * 3;
            CRGB cc = on ? col : dim;
            setPixel(colX[c], y, cc);
            setPixel(colX[c] + 1, y, cc);
        }
    }
}

// ---------- Stil 2: WORT (scrollend, Deutsch) ----------
static const char *stundeWort(int h) {
    static const char *w[12] = {"ZWOELF", "EINS", "ZWEI", "DREI", "VIER", "FUENF",
                                "SECHS", "SIEBEN", "ACHT", "NEUN", "ZEHN", "ELF"};
    return w[h % 12];
}
static void uhrWort(struct tm &ti, CRGB col, unsigned long jetzt) {
    int h12 = ti.tm_hour % 12;
    int hN  = (h12 + 1) % 12;
    int b   = ti.tm_min / 5;
    char phrase[48];
    switch (b) {
        case 0:  snprintf(phrase, sizeof(phrase), "ES IST %s UHR", stundeWort(h12)); break;
        case 1:  snprintf(phrase, sizeof(phrase), "FUENF NACH %s", stundeWort(h12)); break;
        case 2:  snprintf(phrase, sizeof(phrase), "ZEHN NACH %s", stundeWort(h12)); break;
        case 3:  snprintf(phrase, sizeof(phrase), "VIERTEL NACH %s", stundeWort(h12)); break;
        case 4:  snprintf(phrase, sizeof(phrase), "ZWANZIG NACH %s", stundeWort(h12)); break;
        case 5:  snprintf(phrase, sizeof(phrase), "FUENF VOR HALB %s", stundeWort(hN)); break;
        case 6:  snprintf(phrase, sizeof(phrase), "HALB %s", stundeWort(hN)); break;
        case 7:  snprintf(phrase, sizeof(phrase), "FUENF NACH HALB %s", stundeWort(hN)); break;
        case 8:  snprintf(phrase, sizeof(phrase), "ZWANZIG VOR %s", stundeWort(hN)); break;
        case 9:  snprintf(phrase, sizeof(phrase), "VIERTEL VOR %s", stundeWort(hN)); break;
        case 10: snprintf(phrase, sizeof(phrase), "ZEHN VOR %s", stundeWort(hN)); break;
        default: snprintf(phrase, sizeof(phrase), "FUENF VOR %s", stundeWort(hN)); break;
    }
    int len = strlen(phrase);
    int total = len * 4 + 32;
    int scrollX = 32 - (int)((jetzt / 80) % total);
    for (int i = 0; i < len; i++) {
        if (phrase[i] != ' ') drawChar3x5(scrollX + i * 4, 6, phrase[i], col);
    }
}

// ---------- Stil 3: ANALOG ----------
static void zeichneLinie(int x0, int y0, int x1, int y1, CRGB c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        setPixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void uhrAnalog(struct tm &ti, CRGB col) {
    int cx = 16, cy = 8, r = 7;
    CRGB dim = col; dim.nscale8(55);
    for (int h = 0; h < 12; h++) {
        float a = h * 30 * DEG_TO_RAD;
        setPixel(cx + (int)round(sin(a) * r), cy - (int)round(cos(a) * r), dim);
    }
    float secA  = ti.tm_sec * 6 * DEG_TO_RAD;
    float minA  = (ti.tm_min * 6 + ti.tm_sec * 0.1) * DEG_TO_RAD;
    float hourA = ((ti.tm_hour % 12) * 30 + ti.tm_min * 0.5) * DEG_TO_RAD;
    zeichneLinie(cx, cy, cx + (int)round(sin(hourA) * 4), cy - (int)round(cos(hourA) * 4), col);
    zeichneLinie(cx, cy, cx + (int)round(sin(minA) * 6),  cy - (int)round(cos(minA) * 6),  col);
    zeichneLinie(cx, cy, cx + (int)round(sin(secA) * 7),  cy - (int)round(cos(secA) * 7),  CRGB::Red);
    setPixel(cx, cy, CRGB::White);
}

void taskUhr(void *pvParameters) {
    struct tm timeinfo;
    Serial.println("TaskUhr: Gestartet.");

    for (;;) {
        FastLED.clear();
        unsigned long jetzt = millis();
        CRGB col = themeCol(jetzt / 40); // Theme-Farbe, sanft wandernd

        if (getLocalTime(&timeinfo, 10)) {
            switch (g_clockStyle) {
                case 1: uhrBinaer(timeinfo, col); break;
                case 2: uhrWort(timeinfo, col, jetzt); break;
                case 3: uhrAnalog(timeinfo, col); break;
                default: uhrDigital(timeinfo, col, jetzt); break;
            }
        } else {
            // Noch kein NTP-Sync
            drawChar3x5(7, 6, 'N', col);
            drawChar3x5(13, 6, 'T', col);
            drawChar3x5(19, 6, 'P', col);
            if ((jetzt / 400) % 2) setPixel(15, 13, col);
        }

        FastLED.show();
        vTaskDelay(60 / portTICK_PERIOD_MS);
    }
}
