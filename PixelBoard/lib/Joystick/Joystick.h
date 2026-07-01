#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>
#include "Taster-v2.h"
#include "CPotentiometer.h" // Bindet deine richtige Poti-Klasse ein

class Joystick : public Taster_v2 {
private:
    CPotentiometer xAchse;
    CPotentiometer yAchse;
    int xMitte;
    int yMitte;
    int deadZone;
    bool isI2C; // Flag für I2C-Modus

    // Interne Speicher für die empfangenen Nano-Rohwerte
    int i2cXValue;
    int i2cYValue;
    bool i2cButtonPressed;
    bool i2cInvertX;
    bool i2cInvertY;

public:
    // Dein originaler Hardware-Konstruktor (1:1 beibehalten)
    Joystick(int pinX, int pinY, int pinTaster, int tasterModus);

    // NEU: Der leere Erweiterungs-Konstruktor für Joystick 3 (I2C)
    Joystick();

    // NEU: Methode um die Daten aus dem I2C-Task hineinzuschreiben
    void setI2CData(int rawX, int rawY, bool pressed);

    // Deine originalen Methoden eins zu eins
    void kalibrieren();
    int readXRaw();
    int readYRaw();
    int readXPercent();
    int readYPercent();
    void setXScaleRange(int minVal, int maxVal);
    void setYScaleRange(int minVal, int maxVal);
    void setXInputRange(int minVal, int maxVal);
    void setYInputRange(int minVal, int maxVal);
    void setInverted(bool invertX, bool invertY);
    bool isPressed();
    void update();
};

#endif