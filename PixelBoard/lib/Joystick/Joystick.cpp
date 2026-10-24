#include "Joystick.h"

// 1. Dein originaler Konstruktor (Eins zu eins kopiert, erweitert um isI2C-Flags)
Joystick::Joystick(int pinX, int pinY, int pinTaster, int tasterModus)
  : Taster_v2(pinTaster, tasterModus), xAchse(pinX), yAchse(pinY) {

  this->tasterPin = pinTaster;
  this->isI2C = false;
  this->i2cInvertX = false;
  this->i2cInvertY = false;

  xAchse.setInputRange(0, 4095);
  yAchse.setInputRange(0, 4095);
  xAchse.setScaleRange(-100, 100);
  yAchse.setScaleRange(-100, 100);

  kalibrieren();
  setDeadZonePercent(1);
}

// 2. NEU: Der einfache I2C-Erweiterungs-Konstruktor
Joystick::Joystick()
  : Taster_v2(-1, INPUT), xAchse(-1), yAchse(-1) { // Gibt fiktive Pins an die Klassen weiter
  
  this->tasterPin = -1;
  this->isI2C = true;
  this->i2cXValue = 512;
  this->i2cYValue = 512;
  this->i2cButtonPressed = false;
  this->i2cInvertX = false;
  this->i2cInvertY = false;

  // Ein Arduino Nano arbeitet mit 10-Bit ADC (0 - 1023)
  xAchse.setInputRange(0, 1023);
  yAchse.setInputRange(0, 1023);
  xAchse.setScaleRange(-100, 100);
  yAchse.setScaleRange(-100, 100);

  xMitte = 512;
  yMitte = 512;
  setDeadZonePercent(4); // Nano-Joysticks brauchen meist etwas mehr Ruhezone.
}

// NEU: Setzt die im I2C-Task empfangenen Werte direkt in die Klasse
void Joystick::setI2CData(int rawX, int rawY, bool pressed) {
  if (isI2C) {
    i2cXValue = rawX;
    i2cYValue = rawY;
    i2cButtonPressed = pressed;
  }
}

void Joystick::kalibrieren() {
  if (isI2C) {
    xMitte = readXRaw(); // Berücksichtigt direkt eventuelle Invertierungen
    yMitte = readYRaw();
  } else {
    xMitte = xAchse.readRaw();
    yMitte = yAchse.readRaw();
  }
}

int Joystick::readXRaw() {
  if (isI2C) {
    int raw = i2cXValue;
    if (i2cInvertX) {
      raw = 1023 - raw; // Spiegelt den Wert analog zu CPotentiometer (maxValue - raw)
    }
    return raw;
  } else {
    return xAchse.readRaw();
  }
}

int Joystick::readYRaw() {
  if (isI2C) {
    int raw = i2cYValue;
    if (i2cInvertY) {
      raw = 1023 - raw;
    }
    return raw;
  } else {
    return yAchse.readRaw();
  }
}

int Joystick::readXPercent() {
  int rawValue = readXRaw();
  int raw = rawValue - xMitte;
  if (abs(raw) < deadZone) raw = 0;
  
  // Im I2C-Modus mappen wir dynamisch bis max. 1023, sonst deine originalen 4095
  int maxRaw = isI2C ? 1023 : 4095;
  return constrain(map(raw, -xMitte, maxRaw - xMitte, -100, 100), -100, 100);
}

int Joystick::readYPercent() {
  int rawValue = readYRaw();
  int raw = rawValue - yMitte;
  if (abs(raw) < deadZone) raw = 0;
  
  int maxRaw = isI2C ? 1023 : 4095;
  return constrain(map(raw, -yMitte, maxRaw - yMitte, -100, 100), -100, 100);
}

void Joystick::setXScaleRange(int minVal, int maxVal) {
  xAchse.setScaleRange(minVal, maxVal);
}

void Joystick::setYScaleRange(int minVal, int maxVal) {
  yAchse.setScaleRange(minVal, maxVal);
}

void Joystick::setXInputRange(int minVal, int maxVal) {
  xAchse.setInputRange(minVal, maxVal);
}

void Joystick::setYInputRange(int minVal, int maxVal) {
  yAchse.setInputRange(minVal, maxVal);
}

void Joystick::setInverted(bool invertX, bool invertY) {
  xAchse.setInverted(invertX);
  yAchse.setInverted(invertY);
  i2cInvertX = invertX;
  i2cInvertY = invertY;
}

bool Joystick::isPressed() {
  if (isI2C) {
    return i2cButtonPressed;
  } else {
    return (tasterModus == INPUT_PULLUP) ? (digitalRead(tasterPin) == LOW) : (digitalRead(tasterPin) == HIGH);
  }
}

void Joystick::setDeadZonePercent(int percent) {
  deadZonePercent = (uint8_t)constrain(percent, 1, 20);
  int maxRaw = isI2C ? 1023 : 4095;
  deadZone = max(1, maxRaw * (int)deadZonePercent / 100);
}

uint8_t Joystick::getDeadZonePercent() const {
  return deadZonePercent;
}

void Joystick::reset() {
  if (!isI2C) {
    Taster_v2::reset();
    return;
  }

  int currentState = i2cButtonPressed ? HIGH : LOW;
  bool activeNow = i2cButtonPressed;
  ignoreBisLoslassen = activeNow;
  letzterTasterZustand = currentState;
  tasterZustand = currentState;
  klickStartZeit = millis();
  ersterKlickErkannt = false;
  letzterKlickMillis = 0;
  langKlickErkannt = activeNow;
  einfacherKlickZaehler = 0;
  doppelklickZaehler = 0;
  langKlickZaehler = 0;
}

void Joystick::update() {
  if (isI2C) {
    // 1:1 Kopie deiner originalen Taster_v2::klickenErkennen() Logik!
    // Nutzt statt digitalRead() einfach den übertragenen I2C-Buttonstatus.
    int aktuellerZustand = i2cButtonPressed ? HIGH : LOW; 
    unsigned long jetzt = millis();

    if (aktuellerZustand != letzterTasterZustand) {
      letzteEntprellZeit = jetzt;
    }

    if ((jetzt - letzteEntprellZeit) > entprellZeit) {
      if (aktuellerZustand != tasterZustand) {
        tasterZustand = aktuellerZustand;

        // Da tasterModus bei I2C auf INPUT steht, ist (tasterZustand == HIGH) aktiv (Nano liefert HIGH beim Drücken)
        bool istAktiv = (tasterModus == INPUT_PULLUP) ? (tasterZustand == LOW) : (tasterZustand == HIGH);

        if (istAktiv) {
          klickStartZeit = jetzt;
          langKlickErkannt = false;
        }
        else {
          if (ignoreBisLoslassen) {
            ignoreBisLoslassen = false;
          } else {
            unsigned long klickDauer = jetzt - klickStartZeit;
            if (klickDauer >= langKlickZeit && !langKlickErkannt) {
              langKlickZaehler++;
              ersterKlickErkannt = false;
            }
            else {
              if (ersterKlickErkannt && (jetzt - letzterKlickMillis <= doppelklickZeit)) {
                doppelklickZaehler++;
                ersterKlickErkannt = false;
              } else {
                ersterKlickErkannt = true;
                letzterKlickMillis = jetzt;
              }
            }
          }
        }
      }
    }

    bool istAktivDauer = (tasterModus == INPUT_PULLUP) ? (tasterZustand == LOW) : (tasterZustand == HIGH);
    if (istAktivDauer && !langKlickErkannt && !ignoreBisLoslassen) {
      if (jetzt - klickStartZeit >= langKlickZeit) {
        langKlickZaehler++;
        langKlickErkannt = true;
      }
    }

    if (ersterKlickErkannt && !ignoreBisLoslassen && (jetzt - letzterKlickMillis > doppelklickZeit)) {
      einfacherKlickZaehler++;
      ersterKlickErkannt = false;
    }

    letzterTasterZustand = aktuellerZustand;
  } else {
    // Nutzt deine originale Methode aus Taster_v2 für normale Joysticks
    klickenErkennen();
  }
}
