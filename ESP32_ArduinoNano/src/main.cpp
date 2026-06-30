#include <Arduino.h>
#include <Wire.h>
#include "Joystick.h"

// Wir erstellen den I2C-Joystick für den Test
Joystick joystick3; 

// Variablen, um die reinen I2C-Rohwerte für die Anzeige zu speichern
int debugRawX = 0;
int debugRawY = 0;
bool debugRawBtn = false;

void taskI2CJoystickHandler(void *pvParameters);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== ESP32: I2C JOYSTICK ERWEITERUNGS-TEST ===");

    // I2C-Bus mit deinen Wunsch-Pins initialisieren
    Wire.begin(32, 33);
    Serial.println("I2C-Bus auf GPIO 32 (SDA) und GPIO 33 (SCL) aktiv.");

    // Hintergrund-Task starten
    xTaskCreatePinnedToCore(
        taskI2CJoystickHandler,
        "I2CJoystickTask",
        2048,
        NULL,
        4,
        NULL,
        1
    );
}

void loop() {
    // ========== AUSGABE AM SERIELLEN MONITOR DES ESP32 ==========
    
    Serial.println("--------------------------------------------------");
    // 1. Herkunft: Was hat der Arduino Nano im I2C-Buffer geschickt?
    Serial.printf("NANO ROHWERTE -> X: %d | Y: %d | Button-Physikalisch: %s\n", 
                  debugRawX, 
                  debugRawY, 
                  debugRawBtn ? "GEDRUECKT (HIGH)" : "FREI (LOW)");

    // 2. Verarbeitung: Was macht deine Joystick-Library daraus?
    Serial.printf("ESP32 LIB     -> X: %d%% | Y: %d%% | Klicks: %d | Doppel: %d | Lang: %d\n", 
                  joystick3.readXPercent(), 
                  joystick3.readYPercent(),
                  joystick3.einfacherKlickZaehler,
                  joystick3.doppelklickZaehler,
                  joystick3.langKlickZaehler);
                  
    delay(250); // Alle 250ms eine übersichtliche Ausgabe
}

// ========== HINTERGRUND-TASK FÜR DIE ABFRAGE ==========
void taskI2CJoystickHandler(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(15); // Schnelle 15ms Spiel-Schleife

    for (;;) {
        Wire.requestFrom((uint8_t)0x08, (uint8_t)5);
        
        if (Wire.available() >= 5) {
            // Bytes auslesen und zusammensetzen
            int rawX = (Wire.read() << 8) | Wire.read();
            int rawY = (Wire.read() << 8) | Wire.read();
            bool btnPressed = (Wire.read() == 1);

            // Zwischenspeichern für die serielle Anzeige im Hauptprogramm
            debugRawX = rawX;
            debugRawY = rawY;
            debugRawBtn = btnPressed;

            // Deine Library füttern
            joystick3.setI2CData(rawX, rawY, btnPressed);
            
            // Deine Library berechnet jetzt autonom Klick, Doppel- und Langklick!
            joystick3.update(); 
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}