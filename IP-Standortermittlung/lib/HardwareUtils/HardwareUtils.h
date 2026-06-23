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

#endif