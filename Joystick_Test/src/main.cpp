#include <Arduino.h>
#include <WiFi.h>  // WiFi Library für den Härtetest
#include <Wire.h>
#include "Joystick.h"

// ==========================================
// PIN-DEFINITIONEN & RECHNEN-PARAMETER
// ==========================================
// Joystick 1 (Intern)
#define J1_PIN_X 34       // Analog
#define J1_PIN_Y 35       // Analog
#define J1_PIN_TASTER 13  // Digital

// Joystick 2 (Extern 1)
#define J2_PIN_X 36       // Analog (SENSOR_VP)
#define J2_PIN_Y 39       // Analog (SENSOR_VN)
#define J2_PIN_TASTER 14  // Digital

// I2C Pins für Joystick 3 (Befreit ADC-Pins 32 und 33 für I2C!)
#define I2C_SDA 32
#define I2C_SCL 33
#define J3_I2C_ADDRESS 0x08

// ==========================================
// WLAN KONFIGURATION
// ==========================================
const char *ssid = "Nothin";
const char *password = "nothin099";

// ==========================================
// INSTANZEN
// ==========================================
Joystick joystick1(J1_PIN_X, J1_PIN_Y, J1_PIN_TASTER, INPUT_PULLDOWN);
Joystick joystick2(J2_PIN_X, J2_PIN_Y, J2_PIN_TASTER, INPUT_PULLUP);
// Joystick 3 wird standardmäßig (ohne Pins) initialisiert, da Daten via I2C kommen
Joystick joystick3; 

// Globale Variablen für die I2C-Rohdaten von Joystick 3 (zum Debuggen)
int debugRawX = 0;
int debugRawY = 0;
bool debugRawBtn = false;

// Task-Prototyp
void taskI2CJoystickHandler(void *pvParameters);

// Hilfsfunktionen für die Richtungsausgabe
const char *richtungX(int wert) {
    if (wert > 10) return "rechts";
    if (wert < -10) return "links";
    return "mitte";
}

const char *richtungY(int wert) {
    if (wert > 10) return "oben";
    if (wert < -10) return "unten";
    return "mitte";
}

void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("===== Triple-Joystick (inkl. I2C) + WLAN-Härtetest =====");
    
    // I2C Bus für Joystick 3 starten
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("I2C-Bus auf GPIO 32 (SDA) und GPIO 33 (SCL) aktiv.");

    // WLAN aktivieren
    Serial.print("Verbinde mit WLAN...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWLAN erfolgreich verbunden!");

    // Hintergrund-Task für I2C (Joystick 3) starten
    xTaskCreatePinnedToCore(
        taskI2CJoystickHandler,
        "I2CJoystickTask",
        2048,
        NULL,
        4,
        NULL,
        1
    );
    Serial.println("I2C-Hintergrund-Task gestartet.");

    Serial.println("Bitte Hardware-Joysticks (1 & 2) nicht bewegen!");
    Serial.println("Kalibriere Analog-Mittelstellungen...");
    delay(1500);
    
    // Nur die analogen Joysticks kalibrieren (I2C kalibriert sich auf dem Nano selbst)
    joystick1.kalibrieren();
    joystick2.kalibrieren();
    
    Serial.println("Setup abgeschlossen!");
    Serial.println("==========================================================");
}

void loop() {
    // WLAN-Modul im Hintergrund künstlich beschäftigen
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    }

    // Hardware-Instanzen updaten (Joystick 3 wird im Task geupdated!)
    joystick1.update();
    joystick2.update();

    // ==========================================
    // JOYSTICK 1 (INTERN) AUSLESEN
    // ==========================================
    int j1X = joystick1.readXPercent();
    int j1Y = joystick1.readYPercent();
    Serial.printf("J1-> X: %3d%% (%s) Y: %3d%% (%s) T: %s", 
                  j1X, richtungX(j1X), j1Y, richtungY(j1Y), joystick1.isPressed() ? "DN" : "--");
    
    if (joystick1.einfacherKlickZaehler > 0) { Serial.print("[K]"); joystick1.einfacherKlickZaehler = 0; }
    if (joystick1.doppelklickZaehler > 0)   { Serial.print("[D]"); joystick1.doppelklickZaehler = 0; }
    if (joystick1.langKlickZaehler > 0)     { Serial.print("[L]"); joystick1.langKlickZaehler = 0; }

    Serial.print("  ||  ");

    // ==========================================
    // JOYSTICK 2 (EXTERN 1) AUSLESEN
    // ==========================================
    int j2X = joystick2.readXPercent();
    int j2Y = joystick2.readYPercent();
    Serial.printf("J2-> X: %3d%% (%s) Y: %3d%% (%s) T: %s", 
                  j2X, richtungX(j2X), j2Y, richtungY(j2Y), joystick2.isPressed() ? "DN" : "--");
    
    if (joystick2.einfacherKlickZaehler > 0) { Serial.print("[K]"); joystick2.einfacherKlickZaehler = 0; }
    if (joystick2.doppelklickZaehler > 0)   { Serial.print("[D]"); joystick2.doppelklickZaehler = 0; }
    if (joystick2.langKlickZaehler > 0)     { Serial.print("[L]"); joystick2.langKlickZaehler = 0; }

    Serial.print("  ||  ");

    // ==========================================
    // JOYSTICK 3 (EXTERN 2 - I2C VIA NANO) AUSLESEN
    // ==========================================
    int j3X = joystick3.readXPercent();
    int j3Y = joystick3.readYPercent();
    
    // Ausgabe inklusive der Rohwerte im Hintergrund zur Kontrolle
    Serial.printf("J3-> X: %3d%% Y: %3d%% T: %s (RohX: %d)", 
                  j3X, j3Y, (joystick3.einfacherKlickZaehler > 0 || joystick3.isPressed()) ? "DN" : "--", debugRawX);
    
    if (joystick3.einfacherKlickZaehler > 0) { Serial.print("[K]"); joystick3.einfacherKlickZaehler = 0; }
    if (joystick3.doppelklickZaehler > 0)   { Serial.print("[D]"); joystick3.doppelklickZaehler = 0; }
    if (joystick3.langKlickZaehler > 0)     { Serial.print("[L]"); joystick3.langKlickZaehler = 0; }

    Serial.println();
    delay(100); 
}

// ========== I2C HINTERGRUND-TASK (UNVERÄNDERT FÜR STABILE DATEN) ==========
void taskI2CJoystickHandler(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(15);

    for (;;) {
        Wire.requestFrom((uint8_t)J3_I2C_ADDRESS, (uint8_t)5);
        
        if (Wire.available() >= 5) {
            int rawX = (Wire.read() << 8) | Wire.read();
            int rawY = (Wire.read() << 8) | Wire.read();
            bool btnPressed = (Wire.read() == 1);

            debugRawX = rawX;
            debugRawY = rawY;
            debugRawBtn = btnPressed;

            // Daten an Library übergeben
            joystick3.setI2CData(rawX, rawY, btnPressed);
            joystick3.update(); 
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}