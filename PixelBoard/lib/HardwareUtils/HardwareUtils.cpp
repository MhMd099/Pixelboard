
#include "HardwareUtils.h"
#include <LEDText.h> // Musst du hier hinzufügen


cLEDMatrix<32, -8, VERTICAL_ZIGZAG_MATRIX> ledsOben;
cLEDMatrix<-32, 8, VERTICAL_ZIGZAG_MATRIX> ledsUnten;
cLEDText AnzeigeOben;
cLEDText AnzeigeUnten;
bool displayAktiv = true;

void setPixel(int x, int y, CRGB color) {
    if (y < 0 || y >= 16 || x < 0 || x >= 32) return;
    if (y < 8) { 
        int index = (x % 2 == 0) ? (x * 8 + y) : (x * 8 + (7 - y));
        ledsOben[0][index] = color;
    } else { 
        int vX = 31 - x;
        int vY = 7 - (y - 8);
        int index = (vX % 2 == 0) ? (vX * 8 + vY) : (vX * 8 + (7 - vY));
        ledsUnten[0][index] = color;
    }
}