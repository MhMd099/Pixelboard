#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>
#include "Taster-v2.h"
#include "CPotentiometer.h"

/**
 * Joystick-Klasse für ESP32
 * - X/Y-Achse (analog)
 * - Taster (digital mit Klickauswertung)
 */
class Joystick : public Taster_v2 {
  private:
    CPotentiometer xAchse;
    CPotentiometer yAchse;
    int tasterPin;

    int xMitte;
    int yMitte;
    int deadZone;

  public:
    Joystick(int pinX, int pinY, int pinTaster);

    // --- Achsen ---
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

    // --- Taster ---
    bool isPressed();
    void update();  // Klick-Erkennung aktualisieren
};

#endif
