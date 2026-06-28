
#include "HardwareUtils.h"
#include <LEDText.h> // Musst du hier hinzufügen


cLEDMatrix<32, -8, VERTICAL_ZIGZAG_MATRIX> ledsOben;
cLEDMatrix<-32, 8, VERTICAL_ZIGZAG_MATRIX> ledsUnten;
cLEDText AnzeigeOben;
cLEDText AnzeigeUnten;
bool displayAktiv = true;
const char* font3x5[26] = {
  "111101111101101", "110101110101110", "111100100100111", "110101101101110",
  "111100111100111", "111100111100100", "111100101101111", "101101111101101",
  "111010010010111", "001001001101111", "101110100110101", "100100100100111",
  "111111101101101", "111101101101101", "111101101101111", "111101111100100",
  "111101101111001", "111101110101101", "111100111001111", "111010010010010",
  "101101101101111", "101101101101010", "101101101111111", "101101010101101",
  "101101010010010", "111001010100111"
};

void drawChar3x5(int startX, int startY, char c, CRGB color) {
    if (c >= 'a' && c <= 'z') c -= 32; // Kleinbuchstaben zu Groß
    if (c < 'A' || c > 'Z') return;
    int charIndex = c - 'A';
    const char* pattern = font3x5[charIndex];
    for (int i = 0; i < 15; i++) {
        if (pattern[i] == '1') {
            // Hinweis: setPixel muss hier verfügbar sein, ggf. HardwareUtils einbinden
            setPixel(startX + (i % 3), startY + (i / 3), color);
        }
    }
}
// (Deine bestehende drawDigitW Funktion bleibt auch komplett unverändert!)
void drawDigitW(int x, int y, int n, CRGB c) {
    if(n == 0) { for(int i=0; i<5; i++) { setPixel(x,y+i,c); setPixel(x+2,y+i,c); } setPixel(x+1,y,c); setPixel(x+1,y+4,c); }
    else if(n == 1) { for(int i=0; i<5; i++) setPixel(x+2, y+i, c); }
    else if(n == 2) { for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x+2,y+1,c); setPixel(x,y+3,c); }
    else if(n == 3) { for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x+2,y+1,c); setPixel(x+2,y+3,c); }
    else if(n == 4) { for(int i=0; i<3; i++) setPixel(x,y+i,c); for(int i=0; i<5; i++) setPixel(x+2,y+i,c); setPixel(x+1,y+2,c); }
    else if(n == 5) { for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x,y+1,c); setPixel(x+2,y+3,c); }
    else if(n == 6) { for(int i=0; i<5; i++) setPixel(x,y+i,c); for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x+2,y+3,c); }
    else if(n == 7) { for(int i=0; i<5; i++) setPixel(x+2,y+i,c); setPixel(x,y,c); setPixel(x+1,y,c); }
    else if(n == 8) { for(int i=0; i<5; i++) { setPixel(x,y+i,c); setPixel(x+2,y+i,c); } for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } }
    else if(n == 9) { for(int i=0; i<5; i++) setPixel(x+2,y+i,c); for(int i=0; i<3; i++) { setPixel(x+i,y,c); setPixel(x+i,y+2,c); setPixel(x+i,y+4,c); } setPixel(x,y+1,c); }
}
// ==========================================
// THEME / DESIGN
// ==========================================
uint8_t g_themeA = 0;
uint8_t g_themeB = 0;
bool    g_themeRainbow = true;
int     g_themeIndex = 0;

struct ThemeDef { const char* name; uint8_t a; uint8_t b; bool rainbow; };
static const ThemeDef THEMES[] = {
    {"Regenbogen",      0,   0,  true},
    {"Ozean",           140, 120, false},
    {"Feuer",           0,   32,  false},
    {"Violett-Gruen",   192, 96,  false},
    {"Sonnenuntergang", 224, 24,  false},
    {"Matrix",          96,  80,  false},
};
static const int THEME_N = sizeof(THEMES) / sizeof(THEMES[0]);

int themeAnzahl() { return THEME_N; }
const char* themeName(int idx) { return (idx >= 0 && idx < THEME_N) ? THEMES[idx].name : "?"; }

void applyTheme(int idx) {
    if (idx < 0 || idx >= THEME_N) idx = 0;
    g_themeIndex   = idx;
    g_themeA       = THEMES[idx].a;
    g_themeB       = THEMES[idx].b;
    g_themeRainbow = THEMES[idx].rainbow;
}

CRGB themeCol(uint16_t phase, uint8_t val) {
    if (g_themeRainbow) return CHSV((uint8_t)phase, 255, val);
    // Dreieckswelle: Phase pendelt zwischen Haupt- und Zweitfarbe hin und her
    uint8_t p = (uint8_t)phase;
    uint8_t t = (p < 128) ? (p * 2) : ((255 - p) * 2);
    uint8_t hue = g_themeA + (int)((int)g_themeB - (int)g_themeA) * t / 255;
    return CHSV(hue, 255, val);
}

// ---- Uhr-Stil ----
int g_clockStyle = 0;
static const char* CLOCK_STYLES[] = {"Digital", "Binaer", "Wort", "Analog"};
static const int CLOCK_N = sizeof(CLOCK_STYLES) / sizeof(CLOCK_STYLES[0]);
int clockStyleAnzahl() { return CLOCK_N; }
const char* clockStyleName(int idx) { return (idx >= 0 && idx < CLOCK_N) ? CLOCK_STYLES[idx] : "?"; }
void applyClockStyle(int idx) { if (idx < 0 || idx >= CLOCK_N) idx = 0; g_clockStyle = idx; }

void setPixel(int x, int y, CRGB color) {
    if (y < 0 || y >= 16 || x < 0 || x >= 32) return;
    if (y < 8) { 
        int index = (x % 2 == 0) ? (x * 8 + y) : (x * 8 + (7 - y));
        ledsOben[0][index] = color;
    } else { 
        int vX = 31 - x;
        int vY = 7 - (y - 8);
        int index = (vX % 2 == 0) ? (vX * 8 + vY) : (vX * 8 + (7 - vY));
        ledsUnten[0][index] = color;
    }
}