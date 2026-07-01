#include "TaskDHT.h"
#include "HardwareUtils.h"
#include "Config.h"
#include <FastLED.h>
#include <DHT.h>
#include <math.h>

extern volatile int aktiverTask;

static DHT* dht = nullptr;
static int activePin = -1;
static int activeType = -1;
static float lastTemp = 0.0f;
static float lastHumidity = 0.0f;
static bool lastReadingOk = false;
static unsigned long lastReadMs = 0;
static unsigned long lastErrorLogMs = 0;

static uint8_t dhtLibraryType(int type) {
    return (type == 11) ? DHT11 : DHT22;
}

static void ensureSensor() {
    if (dht != nullptr && activePin == currentDhtPin && activeType == currentDhtType) return;

    if (dht != nullptr) {
        delete dht;
        dht = nullptr;
    }

    activePin = currentDhtPin;
    activeType = currentDhtType;
    pinMode(activePin, INPUT_PULLUP);
    dht = new DHT((uint8_t)activePin, dhtLibraryType(activeType));
    dht->begin();
    lastReadingOk = false;
    lastReadMs = 0;
    Serial.print("TaskDHT: Sensor aktiv auf GPIO ");
    Serial.print(activePin);
    Serial.print(" als DHT");
    Serial.println(activeType);
}

static bool validReading(float t, float h) {
    return !isnan(t) && !isnan(h) && h >= 0.0f && h <= 100.0f && t > -45.0f && t < 90.0f;
}

static void readSensorIfDue() {
    ensureSensor();
    if (dht == nullptr) return;

    unsigned long now = millis();
    if (lastReadMs != 0 && now - lastReadMs < 2300UL) return;
    lastReadMs = now;

    bool ok = false;
    float h = NAN;
    float t = NAN;

    for (int attempt = 0; attempt < 3; attempt++) {
        h = dht->readHumidity();
        t = dht->readTemperature();
        if (validReading(t, h)) {
            ok = true;
            break;
        }
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    if (ok) {
        lastTemp = t;
        lastHumidity = h;
        lastReadingOk = true;
    } else {
        lastReadingOk = false;
        if (now - lastErrorLogMs > 10000UL) {
            Serial.print("TaskDHT: Keine gueltige Messung auf GPIO ");
            Serial.print(activePin);
            Serial.print(" als DHT");
            Serial.println(activeType);
            lastErrorLogMs = now;
        }
    }
}

static void drawMinus(int x, int y, CRGB c) {
    setPixel(x, y + 2, c);
    setPixel(x + 1, y + 2, c);
    setPixel(x + 2, y + 2, c);
}

static void drawDot(int x, int y, CRGB c) {
    setPixel(x, y + 4, c);
}

static void drawDegree(int x, int y, CRGB c) {
    setPixel(x, y, c);
    setPixel(x + 1, y, c);
    setPixel(x, y + 1, c);
    setPixel(x + 1, y + 1, c);
}

static void drawPercent(int x, int y, CRGB c) {
    setPixel(x, y, c);
    setPixel(x + 2, y, c);
    setPixel(x + 2, y + 1, c);
    setPixel(x + 1, y + 2, c);
    setPixel(x, y + 3, c);
    setPixel(x, y + 4, c);
    setPixel(x + 2, y + 4, c);
}

static int drawInteger(int x, int y, int value, CRGB c) {
    if (value >= 100) {
        drawDigitW(x, y, (value / 100) % 10, c);
        x += 4;
    }
    if (value >= 10) {
        drawDigitW(x, y, (value / 10) % 10, c);
        x += 4;
    }
    drawDigitW(x, y, value % 10, c);
    return x + 4;
}

static void drawTempLine(int y, float temp, CRGB labelCol, CRGB valueCol) {
    drawChar3x5(1, y, 'T', labelCol);

    int x = 7;
    int temp10 = (int)roundf(fabsf(temp) * 10.0f);
    int whole = temp10 / 10;
    int frac = temp10 % 10;

    if (temp < 0.0f) {
        drawMinus(x, y, valueCol);
        x += 4;
    }

    x = drawInteger(x, y, whole, valueCol);
    if (whole < 100) {
        drawDot(x, y, valueCol);
        drawDigitW(x + 1, y, frac, valueCol);
        x += 5;
    }

    drawDegree(x, y, valueCol);
    drawChar3x5(x + 4, y, 'C', valueCol);
}

static void drawHumidityLine(int y, float humidity, CRGB labelCol, CRGB valueCol) {
    drawChar3x5(1, y, 'H', labelCol);

    int h = constrain((int)roundf(humidity), 0, 100);
    int x = drawInteger(9, y, h, valueCol);
    drawPercent(x, y, valueCol);
}

static void drawError(CRGB col) {
    drawChar3x5(4, 2, 'E', col);
    drawChar3x5(10, 2, 'R', col);
    drawChar3x5(16, 2, 'R', col);
    drawChar3x5(4, 10, 'D', col);
    drawChar3x5(10, 10, 'H', col);
    drawChar3x5(16, 10, 'T', col);
}

void taskDHTHandler(void *pvParameters) {
    ensureSensor();

    for (;;) {
        if (aktiverTask != 5) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        readSensorIfDue();

        if (lockDisplay(20)) {
            if (aktiverTask == 5) {
                unsigned long now = millis();
                CRGB labelCol = themeCol(now / 35, 255);
                CRGB valueCol = themeCol(now / 35 + 96, 230);

                FastLED.clear();
                if (lastReadingOk) {
                    drawTempLine(1, lastTemp, labelCol, valueCol);
                    drawHumidityLine(10, lastHumidity, labelCol, valueCol);
                } else {
                    drawError(labelCol);
                }
                FastLED.show();
            }
            unlockDisplay();
        }

        vTaskDelay(80 / portTICK_PERIOD_MS);
    }
}
