#ifndef CPOTENTIOMETER_H
#define CPOTENTIOMETER_H

#include <Arduino.h>

class CPotentiometer {
  private:
    int pin;
    int minValue;    // Minimum raw value
    int maxValue;    // Maximum raw value
    int minScale;    // Minimum scaled value
    int maxScale;    // Maximum scaled value
    bool inverted;   // Whether to invert the readings

  public:
    CPotentiometer(int pin);
    
    // Basic reading methods
    int readRaw();
    int readScaled();
    int readPercent();
    byte readByte();
    
    // Configuration methods
    void setScaleRange(int newMin, int newMax);
    void setInputRange(int newMin, int newMax);
    void setInverted(bool invert);
    
    // Utility methods
    bool hasChanged(int threshold = 5);
};

#endif