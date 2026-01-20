#include "Taster-v2.h"

// Achten Sie darauf, dass der Dateiname im include exakt übereinstimmt

Taster_v2::Taster_v2(int pin, unsigned long entprellen, unsigned long doppelklickZ, unsigned long langKlickZ) {
  tasterPin = pin;
  entprellZeit = entprellen;
  doppelklickZeit = doppelklickZ;
  langKlickZeit = langKlickZ;
  letzteEntprellZeit = 0;
  letzterTasterZustand = HIGH;
  tasterZustand = HIGH;
  klickStartZeit = 0;
  ersterKlickErkannt = false;
  letzterKlickMillis = 0;
  langKlickErkannt = false;
  einfacherKlickZaehler = 0;
  doppelklickZaehler = 0;
  langKlickZaehler = 0;

  // Verwendung von INPUT_PULLUP, Taster muss gegen GND schalten
  pinMode(tasterPin, INPUT_PULLUP); 
}

void Taster_v2::klickenErkennen() {
  int aktuellerZustand = digitalRead(tasterPin);
  unsigned long jetzt = millis();

  // Entprellung
  if (aktuellerZustand != letzterTasterZustand) {
    letzteEntprellZeit = jetzt;
  }

  if ((jetzt - letzteEntprellZeit) > entprellZeit) {
    if (aktuellerZustand != tasterZustand) {
      tasterZustand = aktuellerZustand;

      // Taster gedrückt
      if (tasterZustand == LOW) {
        klickStartZeit = jetzt;
        langKlickErkannt = false;
      }

      // Taster losgelassen
      else {
        unsigned long klickDauer = jetzt - klickStartZeit;

        // Langklick erkannt
        if (klickDauer >= langKlickZeit) {
          langKlickZaehler++;
          ersterKlickErkannt = false;
        }

        // Kurzer Klick
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

  // Langklick, während Taster gedrückt bleibt
  if (tasterZustand == LOW && !langKlickErkannt) {
    if (jetzt - klickStartZeit >= langKlickZeit) {
      langKlickZaehler++;
      langKlickErkannt = true;
    }
  }

  // Einfachklick nach Ablauf der Doppelklick-Zeit
  if (ersterKlickErkannt && (jetzt - letzterKlickMillis > doppelklickZeit)) {
    einfacherKlickZaehler++;
    ersterKlickErkannt = false;
  }

  letzterTasterZustand = aktuellerZustand;
}


// Implementierung für die isPressed-Funktion in Joystick
int Taster_v2::isPressed() {
    // Gibt 1 zurück, wenn der Taster gerade gedrückt ist (LOW), sonst 0.
    // Kann von Joystick::isPressed() aufgerufen werden.
    return digitalRead(tasterPin) == LOW; 
}