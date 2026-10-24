#include "TaskStandby.h"

#include <Arduino.h>
#include <FastLED.h>

#include "Config.h"
#include "HardwareUtils.h"
#include "RuntimeHealth.h"

extern volatile int aktiverTask;

namespace {
constexpr int WIDTH = 32;
constexpr int HEIGHT = 16;
constexpr int TASK_ID = 11;
constexpr unsigned long EFFECT_DURATION_MS = 7000;
constexpr unsigned long TEXT_REFRESH_MS = 500;

void drawStandbyGlyph(int x, int y, char value, CRGB color) {
    char c = value;
    if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
    if (c >= 'A' && c <= 'Z') {
        drawChar3x5(x, y, c, color);
        return;
    }
    if (c >= '0' && c <= '9') {
        drawDigitW(x, y, c - '0', color);
        return;
    }
    if (c == '-') {
        for (int dx = 0; dx < 3; dx++) setPixel(x + dx, y + 2, color);
    } else if (c == '.') {
        setPixel(x + 1, y + 4, color);
    } else if (c == '!') {
        for (int dy = 0; dy < 3; dy++) setPixel(x + 1, y + dy, color);
        setPixel(x + 1, y + 4, color);
    } else if (c == '?') {
        for (int dx = 0; dx < 3; dx++) setPixel(x + dx, y, color);
        setPixel(x + 2, y + 1, color);
        setPixel(x + 1, y + 2, color);
        setPixel(x + 1, y + 4, color);
    }
}

void drawTextLine(const String& text, int startX, int y, unsigned long now,
                  uint8_t brightness, int offsetX = 0, int offsetY = 0) {
    for (size_t i = 0; i < text.length(); i++) {
        if (text[i] == ' ') continue;
        CRGB color = themeCol((uint16_t)(now / 12 + i * 29), brightness);
        drawStandbyGlyph(startX + (int)i * 4 + offsetX, y + offsetY, text[i], color);
    }
}

void drawBackground(unsigned long now, uint8_t effect) {
    if (effect == 0) {
        for (int i = 0; i < 18; i++) {
            int x = (i * 17 + now / 110) % WIDTH;
            int y = (i * 11 + now / (170 + (i % 3) * 30)) % HEIGHT;
            uint8_t glow = 18 + sin8(now / 8 + i * 31) / 7;
            setPixel(x, y, themeCol(i * 23 + now / 24, glow));
        }
    } else if (effect == 1) {
        for (int x = 0; x < WIDTH; x++) {
            int waveY = 8 + ((int)sin8(x * 14 + now / 6) - 128) / 28;
            setPixel(x, waveY, themeCol(x * 8 + now / 16, 65));
            if (waveY + 1 < HEIGHT) setPixel(x, waveY + 1, themeCol(x * 8 + now / 16 + 40, 25));
        }
    } else {
        int scan = (now / 70) % HEIGHT;
        for (int x = 0; x < WIDTH; x++) {
            uint8_t edge = 12 + sin8(x * 11 + now / 9) / 10;
            setPixel(x, 0, themeCol(x * 6 + now / 20, edge));
            setPixel(WIDTH - 1 - x, HEIGHT - 1, themeCol(x * 6 + now / 20 + 80, edge));
        }
        for (int x = 0; x < WIDTH; x += 2)
            setPixel(x, scan, themeCol(x * 7 + now / 10, 38));
    }
}

void renderStandbyFrame(const String& text, unsigned long now) {
    FastLED.clear();
    uint8_t effect = (now / EFFECT_DURATION_MS) % 3;
    drawBackground(now, effect);

    int textWidth = max(3, (int)text.length() * 4 - 1);
    int startX;
    if (textWidth <= WIDTH) {
        startX = (WIDTH - textWidth) / 2;
    } else {
        int travel = textWidth + WIDTH + 8;
        startX = WIDTH + 4 - (int)((now / 85) % travel);
    }

    int y = 5;
    if (effect == 1) y += ((int)sin8(now / 7) - 128) / 96;
    drawTextLine(text, startX, y, now, 55, 1, 1);
    drawTextLine(text, startX, y, now, 255);

    uint8_t pulse = 80 + sin8(now / 5) / 2;
    setPixel(0, 0, themeCol(now / 10, pulse));
    setPixel(WIDTH - 1, 0, themeCol(now / 10 + 64, pulse));
    setPixel(0, HEIGHT - 1, themeCol(now / 10 + 128, pulse));
    setPixel(WIDTH - 1, HEIGHT - 1, themeCol(now / 10 + 192, pulse));
}
}

void renderStandbyMenuPreview() {
    unsigned long now = millis();
    for (int x = 2; x < 30; x += 3)
        setPixel(x, 3 + (sin8(x * 13 + now / 7) / 48), themeCol(x * 9 + now / 14, 85));
    drawStandbyGlyph(8, 6, 'N', themeCol(now / 10, 255));
    drawStandbyGlyph(13, 6, 'B', themeCol(now / 10 + 85, 255));
    drawStandbyGlyph(18, 6, 'L', themeCol(now / 10 + 170, 255));
}

void taskStandbyHandler(void *pvParameters) {
    registerCurrentTaskWithWatchdog();
    String text = "NothinBL";
    text.reserve(33);
    unsigned long lastTextRefreshMs = 0;

    for (;;) {
        feedRuntimeWatchdog();
        if (aktiverTask != TASK_ID) {
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }

        unsigned long now = millis();
        if (lastTextRefreshMs == 0 || now - lastTextRefreshMs >= TEXT_REFRESH_MS) {
            text = standbyTextSnapshot();
            if (text.length() == 0) text = "NothinBL";
            lastTextRefreshMs = now;
        }

        if (lockDisplay(20)) {
            if (aktiverTask == TASK_ID) {
                renderStandbyFrame(text, now);
                FastLED.show();
            }
            unlockDisplay();
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
