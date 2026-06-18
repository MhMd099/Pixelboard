#include <Arduino.h>
#include <WiFi.h>  // WiFi Library für den Härtetest
#include "Joystick.h"

// ==========================================
// PIN-DEFINITIONEN (PERFEKT AUFGETEILT)
// ==========================================
// Joystick 1 (Intern) -_>Ex
#define J1_PIN_X 34       // Analog (ADC1)
#define J1_PIN_Y 35       // Analog (ADC1)
#define J1_PIN_TASTER 13  // Digital (UMGESTECKT! Befreit Pin 32)

// Joystick 2 (Extern 1)
#define J2_PIN_X 36       // Analog (ADC1 - SENSOR_VP)
#define J2_PIN_Y 39       // Analog (ADC1 - SENSOR_VN)
#define J2_PIN_TASTER 14  // Digital (Sicherer Boot-Pin)

// Joystick 3 (Extern 2 - NEU)
#define J3_PIN_X 32       // Analog (ADC1 - Jetzt frei für X!)
#define J3_PIN_Y 33       // Analog (ADC1 - Jetzt frei für Y!)
#define J3_PIN_TASTER 27  // Digital (Freier Digital-Pin für Taster)

// ==========================================
// WLAN KONFIGURATION
// ==========================================
const char *ssid = "Nothin";
const char *password = "nothin099";

// ==========================================
// INSTANZEN (3 Joysticks mit passender Logik)
// ==========================================
Joystick joystick1(J1_PIN_X, J1_PIN_Y, J1_PIN_TASTER, INPUT_PULLDOWN);
Joystick joystick2(J2_PIN_X, J2_PIN_Y, J2_PIN_TASTER, INPUT_PULLUP);
Joystick joystick3(J3_PIN_X, J3_PIN_Y, J3_PIN_TASTER, INPUT_PULLUP);

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
  
  Serial.println("===== Triple-Joystick + WLAN-Härtetest =====");
  
  // WLAN aktivieren und Verbindung erzwingen
  Serial.print("Verbinde mit WLAN...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWLAN erfolgreich verbunden! Das Funkmodul läuft im Hintergrund.");
  Serial.println();

  Serial.println("Bitte ALLE DREI Joysticks nicht bewegen!");
  Serial.println("Kalibriere Mittelstellungen...");
  delay(1500);
  
  // Alle drei Joysticks nacheinander kalibrieren
  joystick1.kalibrieren();
  joystick2.kalibrieren();
  joystick3.kalibrieren();
  
  Serial.println("Kalibrierung für ALLE DREI Joysticks abgeschlossen!");
  Serial.println("==========================================================");
}

void loop() {
  // WLAN-Modul im Hintergrund künstlich beschäftigen
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }

  // Alle drei Hardware-Instanzen updaten
  joystick1.update();
  joystick2.update();
  joystick3.update();

  // ==========================================
  // JOYSTICK 1 (INTERN) AUSLESEN
  // ==========================================
  int j1X = joystick1.readXPercent();
  int j1Y = joystick1.readYPercent();
  bool j1Gedrueckt = joystick1.isPressed();

  Serial.print("J1 -> X: "); Serial.print(j1X); Serial.print("% ("); Serial.print(richtungX(j1X));
  Serial.print(") Y: "); Serial.print(j1Y); Serial.print("% ("); Serial.print(richtungY(j1Y));
  Serial.print(") T: "); Serial.print(j1Gedrueckt ? "DN" : "--");
  if (joystick1.einfacherKlickZaehler > 0) { Serial.print("[K]"); joystick1.einfacherKlickZaehler = 0; }
  if (joystick1.doppelklickZaehler > 0)   { Serial.print("[D]"); joystick1.doppelklickZaehler = 0; }
  if (joystick1.langKlickZaehler > 0)     { Serial.print("[L]"); joystick1.langKlickZaehler = 0; }

  Serial.print("  ||  "); // Trenner

  // ==========================================
  // JOYSTICK 2 (EXTERN 1) AUSLESEN
  // ==========================================
  int j2X = joystick2.readXPercent();
  int j2Y = joystick2.readYPercent();
  bool j2Gedrueckt = !joystick2.isPressed(); // Invertiert wegen Pullup

  Serial.print("J2 -> X: "); Serial.print(j2X); Serial.print("% ("); Serial.print(richtungX(j2X));
  Serial.print(") Y: "); Serial.print(j2Y); Serial.print("% ("); Serial.print(richtungY(j2Y));
  Serial.print(") T: "); Serial.print(j2Gedrueckt ? "DN" : "--");
  if (joystick2.einfacherKlickZaehler > 0) { Serial.print("[K]"); joystick2.einfacherKlickZaehler = 0; }
  if (joystick2.doppelklickZaehler > 0)   { Serial.print("[D]"); joystick2.doppelklickZaehler = 0; }
  if (joystick2.langKlickZaehler > 0)     { Serial.print("[L]"); joystick2.langKlickZaehler = 0; }

  Serial.print("  ||  "); // Trenner

  // ==========================================
  // JOYSTICK 3 (EXTERN 2) AUSLESEN
  // ==========================================
  int j3X = joystick3.readXPercent();
  int j3Y = joystick3.readYPercent();
  bool j3Gedrueckt = !joystick3.isPressed(); // Invertiert wegen Pullup

  Serial.print("J3 -> X: "); Serial.print(j3X); Serial.print("% ("); Serial.print(richtungX(j3X));
  Serial.print(") Y: "); Serial.print(j3Y); Serial.print("% ("); Serial.print(richtungY(j3Y));
  Serial.print(") T: "); Serial.print(j3Gedrueckt ? "DN" : "--");
  if (joystick3.einfacherKlickZaehler > 0) { Serial.print("[K]"); joystick3.einfacherKlickZaehler = 0; }
  if (joystick3.doppelklickZaehler > 0)   { Serial.print("[D]"); joystick3.doppelklickZaehler = 0; }
  if (joystick3.langKlickZaehler > 0)     { Serial.print("[L]"); joystick3.langKlickZaehler = 0; }

  Serial.println();
  
  // Auf 100ms erhöht, da die Zeile im Seriellen Monitor durch 3 Joysticks sehr lang wird
  delay(100); 
}