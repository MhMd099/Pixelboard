#include "TaskAnim.h"
#include <FastLED.h>
#include "HardwareUtils.h"

// Eigener Framebuffer (32x16). Statisch -> belegt keinen Stack.
static CRGB buf[16 * 32];
#define B(x, y) buf[(y) * 32 + (x)]

static void animFade(uint8_t keep) {
    for (int i = 0; i < 16 * 32; i++) buf[i].nscale8(keep);
}

void taskAnim(void *pvParameters) {
    static uint8_t heat[16 * 32];
    static int     dropY[32];
    static bool    life[16 * 32];
    static bool    life2[16 * 32];

    uint8_t mode = 0;          // 0=Plasma 1=Feuer 2=Matrix 3=Sterne 4=Game of Life 5=Konfetti
    bool modeInit = true;
    unsigned long modeStart = millis();
    uint32_t frame = 0;
    int lifeGen = 0;

    for (int i = 0; i < 16 * 32; i++) buf[i] = CRGB::Black;

    auto idx = [](int x, int y) { x = (x + 32) % 32; y = (y + 16) % 16; return y * 32 + x; };

    for (;;) {
        unsigned long now = millis();
        if (now - modeStart > 9000) { // alle 9s naechste Animation
            mode = (mode + 1) % 6;
            modeStart = now;
            modeInit = true;
            for (int i = 0; i < 16 * 32; i++) buf[i] = CRGB::Black;
        }
        frame++;

        switch (mode) {
            case 0: { // Plasma
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 32; x++) {
                        int v = (sin8(x * 12 + frame * 2) + sin8(y * 14 - frame * 3) + sin8((x * 3 + y * 5) + frame)) / 3;
                        B(x, y) = themeCol(v + frame / 2, 255);
                    }
                break;
            }
            case 1: { // Feuer (klassische Heat-Palette)
                if (modeInit) for (int i = 0; i < 16 * 32; i++) heat[i] = 0;
                for (int x = 0; x < 32; x++) {
                    for (int y = 0; y < 16; y++) heat[y * 32 + x] = qsub8(heat[y * 32 + x], random8(0, 12));
                    for (int y = 0; y < 15; y++) {
                        int b2 = (y + 2 < 16) ? (y + 2) : 15;
                        heat[y * 32 + x] = (heat[(y + 1) * 32 + x] * 2 + heat[b2 * 32 + x]) / 3;
                    }
                    if (random8() < 110) heat[15 * 32 + x] = qadd8(heat[15 * 32 + x], random8(160, 255));
                    for (int y = 0; y < 16; y++) B(x, y) = HeatColor(heat[y * 32 + x]);
                }
                break;
            }
            case 2: { // Matrix-Regen
                if (modeInit) for (int x = 0; x < 32; x++) dropY[x] = random8(0, 16);
                animFade(150);
                for (int x = 0; x < 32; x++) {
                    if (frame % (1 + (x % 3)) == 0) dropY[x]++;
                    if (dropY[x] >= 16) { if (random8() < 30) dropY[x] = 0; else continue; }
                    if (dropY[x] >= 0 && dropY[x] < 16) {
                        B(x, dropY[x]) = CRGB::White;
                        if (dropY[x] > 0) B(x, dropY[x] - 1) = themeCol(96, 200);
                    }
                }
                break;
            }
            case 3: { // Sternenhimmel (Funkeln)
                animFade(235);
                for (int k = 0; k < 3; k++)
                    if (random8() < 180) B(random8(32), random8(16)) = themeCol(random8(), 255);
                break;
            }
            case 4: { // Game of Life (Conway)
                if (modeInit) { for (int i = 0; i < 16 * 32; i++) life[i] = (random8() < 80); lifeGen = 0; }
                if (frame % 4 == 0) {
                    int pop = 0;
                    for (int y = 0; y < 16; y++)
                        for (int x = 0; x < 32; x++) {
                            int n = 0;
                            for (int dy = -1; dy <= 1; dy++)
                                for (int dx = -1; dx <= 1; dx++)
                                    if (!(dx == 0 && dy == 0) && life[idx(x + dx, y + dy)]) n++;
                            bool alive = life[y * 32 + x];
                            life2[y * 32 + x] = alive ? (n == 2 || n == 3) : (n == 3);
                            if (life2[y * 32 + x]) pop++;
                        }
                    for (int i = 0; i < 16 * 32; i++) life[i] = life2[i];
                    lifeGen++;
                    if (pop < 6 || lifeGen > 220) { for (int i = 0; i < 16 * 32; i++) life[i] = (random8() < 80); lifeGen = 0; }
                }
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 32; x++)
                        B(x, y) = life[y * 32 + x] ? themeCol(x * 4 + y * 4, 255) : CRGB::Black;
                break;
            }
            case 5: { // Konfetti
                animFade(235);
                buf[random16(16 * 32)] = themeCol(random8(), 255);
                break;
            }
        }

        modeInit = false;

        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 32; x++)
                setPixel(x, y, B(x, y));
        FastLED.show();
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}
