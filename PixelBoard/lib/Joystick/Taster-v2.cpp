#include "Taster-v2.h"

Taster_v2::Taster_v2(int pin, int modus, unsigned long entprellen, unsigned long doppelklickZ, unsigned long langKlickZ) {
  tasterPin = pin;
  tasterModus = modus;
  entprellZeit = entprellen;
  doppelklickZeit = doppelklickZ;
  langKlickZeit = langKlickZ;
  letzteEntprellZeit = 0;
  
  // Startzustand dynamisch je nach Modus setzen
  if (tasterModus == INPUT_PULLUP) {
    letzterTasterZustand = HIGH;
    tasterZustand = HIGH;
  } else {
    letzterTasterZustand = LOW;
    tasterZustand = LOW;
  }
  
  klickStartZeit = 0;
  ersterKlickErkannt = false;
  letzterKlickMillis = 0;
  langKlickErkannt = false;
  ignoreBisLoslassen = false;
  einfacherKlickZaehler = 0;
  doppelklickZaehler = 0;
  langKlickZaehler = 0;

  // Aktiviert genau den Modus, den wir im Hauptprogramm waehlen.
  if (tasterPin >= 0) {
    pinMode(tasterPin, tasterModus);
  }
}

void Taster_v2::klickenErkennen() {
  if (tasterPin < 0) return;

  int aktuellerZustand = digitalRead(tasterPin); 
  unsigned long jetzt = millis();

  if (aktuellerZustand != letzterTasterZustand) {
    letzteEntprellZeit = jetzt;
  }

  if ((jetzt - letzteEntprellZeit) > entprellZeit) {
    if (aktuellerZustand != tasterZustand) {
      tasterZustand = aktuellerZustand;

      // Logik-Umschalter: Bei PULLUP ist LOW aktiv, bei PULLDOWN ist HIGH aktiv
      bool istAktiv = (tasterModus == INPUT_PULLUP) ? (tasterZustand == LOW) : (tasterZustand == HIGH);

      if (istAktiv) {
        klickStartZeit = jetzt;
        langKlickErkannt = false;
      }
      else {
        // Taster wurde losgelassen
        if (ignoreBisLoslassen) {
          // Der zum Task-Wechsel benutzte Druck wird hier sauber abgeschlossen
          ignoreBisLoslassen = false;
        } else {
          unsigned long klickDauer = jetzt - klickStartZeit;
          if (klickDauer >= langKlickZeit) {
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

  // Auch hier das dauerhafte Halten dynamisch prüfen
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
}

void Taster_v2::reset() {
  if (tasterPin < 0) {
    ignoreBisLoslassen = false;
    einfacherKlickZaehler = 0;
    doppelklickZaehler = 0;
    langKlickZaehler = 0;
    return;
  }

  int cur = digitalRead(tasterPin);
  bool aktivJetzt = (tasterModus == INPUT_PULLUP) ? (cur == LOW) : (cur == HIGH);
  ignoreBisLoslassen = aktivJetzt;   // wenn beim Wechsel noch gedrückt: bis Loslassen ignorieren
  letzterTasterZustand = cur;
  tasterZustand = cur;
  klickStartZeit = millis();
  ersterKlickErkannt = false;
  langKlickErkannt = aktivJetzt;
  letzterKlickMillis = 0;
  einfacherKlickZaehler = 0;
  doppelklickZaehler = 0;
  langKlickZaehler = 0;
}

int Taster_v2::isPressed() {
  if (tasterPin < 0) return false;

  // Gibt true (1) zurück, wenn der Taster aktiv gedrückt ist
  return (tasterModus == INPUT_PULLUP) ? (digitalRead(tasterPin) == LOW) : (digitalRead(tasterPin) == HIGH);
}
