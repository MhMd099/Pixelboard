#ifndef Taster_v2_h
#define Taster_v2_h

#include <Arduino.h>

class Taster_v2 {
  protected: // Auf 'protected' geändert, damit die Joystick-Klasse diese Variablen erben und nutzen kann
    int tasterPin;
    int tasterModus; // Speichert, ob der Pin als INPUT_PULLUP oder INPUT_PULLDOWN läuft
    
    unsigned long entprellZeit;
    unsigned long doppelklickZeit;
    unsigned long langKlickZeit;
    unsigned long letzteEntprellZeit;
    int letzterTasterZustand;
    int tasterZustand;
    unsigned long klickStartZeit;
    bool ersterKlickErkannt;
    unsigned long letzterKlickMillis;
    bool langKlickErkannt;

  public:
    int einfacherKlickZaehler;
    int doppelklickZaehler;
    int langKlickZaehler;
    
    int isPressed();

    // Der Konstruktor akzeptiert jetzt den Modus (Standardwert ist INPUT_PULLDOWN, falls nichts übergeben wird)
    Taster_v2(int pin, int modus = INPUT_PULLDOWN, unsigned long entprellen = 20, unsigned long doppelklickZ = 400, unsigned long langKlickZ = 1000);
    
    void klickenErkennen();
};

#endif