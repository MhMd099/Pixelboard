#include "CPotentiometer.h"

// Standardwerte für ESP32 ADC (12-bit)
#define DEFAULT_MIN_RAW 0
#define DEFAULT_MAX_RAW 4095

CPotentiometer::CPotentiometer(int pin) {
  this->pin = pin;
  pinMode(pin, INPUT);
  
  // Setze Standardbereiche
  minValue = DEFAULT_MIN_RAW;
  maxValue = DEFAULT_MAX_RAW;
  minScale = 0;
  maxScale = 100;
  inverted = false;

  // HINWEIS: Für ESP32 müssen Sie eventuell analogReadResolution(12) 
  // oder andere ADC-Konfigurationen in setup() hinzufügen.
}

int CPotentiometer::readRaw() {
  return analogRead(this->pin);
}

int CPotentiometer::readScaled() {
  int raw = readRaw();
  if (inverted) {
    raw = maxValue - raw;
  }
  return map(raw, minValue, maxValue, minScale, maxScale);
}

int CPotentiometer::readPercent() {
  // Liest skaliert auf 0 bis 100
  int raw = readRaw();
  if (inverted) {
    raw = maxValue - raw;
  }
  return map(raw, minValue, maxValue, 0, 100);
}

byte CPotentiometer::readByte() {
  int raw = readRaw();
  if (inverted) {
    raw = maxValue - raw;
  }
  return map(raw, minValue, maxValue, 0, 255);
}

void CPotentiometer::setScaleRange(int newMin, int newMax) {
  minScale = newMin;
  maxScale = newMax;
}

void CPotentiometer::setInputRange(int newMin, int newMax) {
  minValue = newMin;
  maxValue = newMax;
}

void CPotentiometer::setInverted(bool invert) {
  inverted = invert;
}

bool CPotentiometer::hasChanged(int threshold) {
  static int lastValue = 0;
  int currentValue = readRaw();
  if (abs(currentValue - lastValue) >= threshold) {
    lastValue = currentValue;
    return true;
  }
  return false;
}