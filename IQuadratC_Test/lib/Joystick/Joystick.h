#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>
#include "Taster-v2.h"

class Joystick : public Taster_v2 {
private:
    int xPin;
    int yPin;
    bool isI2C; // Merker, ob dieser Joystick über I2C läuft

    // Interne Speicher für die I2C-Werte
    int i2cXValue;
    int i2cYValue;
    bool i2cButtonPressed;

public:
    // 1. Dein bestehender Hardware-Konstruktor
    Joystick(int x, int y, int taster, int modus);

    // 2. NEU: Der einfache I2C-Erweiterungs-Konstruktor (keine echten Pins benötigt)
    Joystick();

    // NEU: Methode um die Daten aus deinem I2C-Task direkt reinzuschreiben
    void setI2CData(int rawX, int rawY, bool pressed);

    // Diese Methoden bleiben exakt gleich im Aufruf
    int readXPercent();
    int readYPercent();
    void update(); // Überschreibt die Taster-Aktualisierung für I2C
};

#endif