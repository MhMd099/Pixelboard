#ifndef Taster_v2_h
#define Taster_v2_h

#include <Arduino.h>

class Taster_v2 {
  private:
    int tasterPin;
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

    Taster_v2(int pin, unsigned long entprellen = 20, unsigned long doppelklickZ = 400, unsigned long langKlickZ = 1000);
    void klickenErkennen();
};

#endif