#ifndef HARDWARE_UTILS_H
#define HARDWARE_UTILS_H

// SCHARFE REIHENFOLGE - Zuerst die Grundlagen, dann die zickige Text-Lib:
#include <Arduino.h>   // Definiert uint8_t, uint16_t etc.
#include <FastLED.h>   // Definiert CRGB
#include <LEDMatrix.h> // Definiert cLEDMatrix und cLEDMatrixBase
#include <LEDText.h>   // Erst JETZT darf sie geladen werden!

// Globale Matrix-Instanzen
extern cLEDMatrix<32, -8, VERTICAL_ZIGZAG_MATRIX> ledsOben;
extern cLEDMatrix<-32, 8, VERTICAL_ZIGZAG_MATRIX> ledsUnten;

// Globale Text-Instanzen
extern cLEDText AnzeigeOben;
extern cLEDText AnzeigeUnten;

void setPixel(int x, int y, CRGB color);
void drawChar3x5(int startX, int startY, char c, CRGB color);

void drawDigitW(int x, int y, int n, CRGB c);

// ==========================================
// THEME / DESIGN (pro User über Web wählbar)
// ==========================================
extern uint8_t g_themeA;       // Haupt-Farbton (Hue 0-255)
extern uint8_t g_themeB;       // Zweit-Farbton
extern bool    g_themeRainbow; // true = voller Regenbogen
extern int     g_themeIndex;   // aktiver Preset-Index

void  applyTheme(int idx);            // Preset aktivieren
int   themeAnzahl();                  // Anzahl Presets
const char* themeName(int idx);       // Name eines Presets
CRGB  themeCol(uint16_t phase, uint8_t val = 255); // themed Farbe für eine "Phase"

// Uhr-Stil (pro User waehlbar): 0=Digital 1=Binaer 2=Wort 3=Analog
extern int g_clockStyle;
void  applyClockStyle(int idx);
int   clockStyleAnzahl();
const char* clockStyleName(int idx);
#endif