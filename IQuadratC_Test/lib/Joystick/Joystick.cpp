#include "Joystick.h"

// Dein bestehender Hardware-Konstruktor
Joystick::Joystick(int x, int y, int taster, int modus) : Taster_v2(taster, modus) {
    xPin = x;
    yPin = y;
    isI2C = false;
    i2cXValue = 512;
    i2cYValue = 512;
    i2cButtonPressed = false;
    pinMode(xPin, INPUT);
    pinMode(yPin, INPUT);
}

// NEU: Der I2C-Konstruktor (Übergibt einen fiktiven Pin -1 an Taster_v2)
Joystick::Joystick() : Taster_v2(-1, INPUT) {
    xPin = -1;
    yPin = -1;
    isI2C = true;
    i2cXValue = 512;
    i2cYValue = 512;
    i2cButtonPressed = false;
}

// NEU: Setzt die vom Arduino Nano empfangenen Werte
void Joystick::setI2CData(int rawX, int rawY, bool pressed) {
    i2cXValue = rawX;
    i2cYValue = rawY;
    i2cButtonPressed = pressed;
}

int Joystick::readXPercent() {
    int raw = 0;
    if (isI2C) {
        // Arduino Nano liefert 0-1023 (Mitte ca. 512)
        raw = i2cXValue - 512;
    } else {
        // ESP32 Hardware-Pin (0-4095, Mitte ca. 2048)
        raw = analogRead(xPin) - 2048;
    }

    if (abs(raw) < (isI2C ? 40 : 150)) raw = 0; // Deadzone angepasst je nach Hardware

    if (isI2C) {
        return constrain(map(raw, -512, 511, -100, 100), -100, 100);
    } else {
        return constrain(map(raw, -2048, 2047, -100, 100), -100, 100);
    }
}

int Joystick::readYPercent() {
    int raw = 0;
    if (isI2C) {
        raw = i2cYValue - 512;
    } else {
        raw = analogRead(yPin) - 2048;
    }

    if (abs(raw) < (isI2C ? 40 : 150)) raw = 0;

    if (isI2C) {
        return constrain(map(raw, -512, 511, -100, 100), -100, 100);
    } else {
        return constrain(map(raw, -2048, 2047, -100, 100), -100, 100);
    }
}

// NEU: Überschreibt das Update, damit wir die Taster-Logik füttern können
void Joystick::update() {
    if (isI2C) {
        // Wir simulieren das Drücken für die Taster_v2 Basisklasse.
        // Da der Nano-Taster HIGH-aktiv ist, übergeben wir direkt HIGH (gedrückt) oder LOW.
        int simulierterZustand = i2cButtonPressed ? HIGH : LOW;
        
        // Da wir klickenErkennen() in Taster_v2 nicht direkt mit einem Zustand füttern können (weil es dort fest digitalRead nutzt),
        // rufen wir deine Logik auf, indem wir den Zustand manuell setzen.
        // Um das absolut sauber ohne Änderung an Taster_v2 zu lösen, nutzen wir die Variable 'tasterZustand' 
        // oder rufen deine Erkennung auf. Am einfachsten gibst du den simulierten Zustand an deine Klick-Logik weiter.
        
        // Da du die Klick-Logik in Taster_v2 hast, fügen wir hier die exakt gleiche 
        // Entprell- und Klick-Auswertung basierend auf dem 'simulierterZustand' ein (wie in deiner Taster-v2.cpp).
        unsigned long jetzt = millis();
        if (simulierterZustand != letzterTasterZustand) {
            letzteEntprellZeit = jetzt;
        }

        if ((jetzt - letzteEntprellZeit) > entprellZeit) {
            if (simulierterZustand != tasterZustand) {
                tasterZustand = simulierterZustand;
                if (tasterZustand == HIGH) { // HIGH ist aktiv bei deinem Pull-Down Setup am Nano
                    klickStartZeit = jetzt;
                    langKlickErkannt = false;
                } else {
                    if (!ignoreBisLoslassen) {
                        unsigned long klickDauer = jetzt - klickStartZeit;
                        if (klickDauer >= langKlickZeit) {
                            langKlickZaehler++;
                            ersterKlickErkannt = false;
                        } else {
                            if (ersterKlickErkannt && (jetzt - letzterKlickMillis <= doppelklickZeit)) {
                                doppelklickZaehler++;
                                ersterKlickErkannt = false;
                            } else {
                                ersterKlickErkannt = true;
                                letzterKlickMillis = jetzt;
                            }
                        }
                    } else {
                        ignoreBisLoslassen = false;
                    }
                }
            }
        }

        if ((tasterZustand == HIGH) && !langKlickErkannt && !ignoreBisLoslassen) {
            if (jetzt - klickStartZeit >= langKlickZeit) {
                langKlickZaehler++;
                langKlickErkannt = true;
            }
        }

        if (ersterKlickErkannt && !ignoreBisLoslassen && (jetzt - letzterKlickMillis > doppelklickZeit)) {
            einfacherKlickZaehler++;
            ersterKlickErkannt = false;
        }
        letzterTasterZustand = simulierterZustand;

    } else {
        // Normaler Hardware-Joystick nutzt deine originale Methode aus Taster_v2
        klickenErkennen();
    }
}