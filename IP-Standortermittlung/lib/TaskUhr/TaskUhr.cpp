#include "TaskUhr.h"
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include "time.h"

// Externe Variablen, die in der main.cpp definiert sind
extern cLEDMatrix<32, -8, VERTICAL_ZIGZAG_MATRIX> ledsOben;
extern cLEDMatrix<-32, 8, VERTICAL_ZIGZAG_MATRIX> ledsUnten;
extern cLEDText AnzeigeOben;
extern cLEDText AnzeigeUnten;


void taskUhr(void * pvParameters) {
    struct tm timeinfo;
    
    // HIER: Die Puffer müssen innerhalb der Funktion definiert werden, 
    // damit der Code darauf zugreifen kann!
    char datumBuffer[40];
    char zeitBuffer[40];
    
    for(;;) {
        if (getLocalTime(&timeinfo)) {
            FastLED.clear();
            
            if (millis() % 6000 < 3000) { 
                sprintf(datumBuffer, "\x02%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                sprintf(zeitBuffer, "\x02   %02d", timeinfo.tm_sec);
            } else {
                sprintf(datumBuffer, "\x02 %04d", timeinfo.tm_year + 1900);
                sprintf(zeitBuffer, "\x02%02d.%02d.", timeinfo.tm_mday, timeinfo.tm_mon + 1);
            }
            
            AnzeigeOben.SetText((unsigned char *)datumBuffer, strlen(datumBuffer));
            AnzeigeUnten.SetText((unsigned char *)zeitBuffer, strlen(zeitBuffer));
            
            AnzeigeOben.UpdateText();
            AnzeigeUnten.UpdateText();
            FastLED.show();
        }
        vTaskDelay(500 / portTICK_PERIOD_MS); 
    }
}