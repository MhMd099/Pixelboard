#include <Arduino.h>
#include "Joystick.h"

// Task-Handles
TaskHandle_t handleA = NULL;
TaskHandle_t handleB = NULL;
TaskHandle_t handleC = NULL;

// Deine Hardware-Pins
const int PIN_X = 34;
const int PIN_Y = 35;
const int PIN_BTN = 32;

// Joystick-Instanz
Joystick meinJoystick(PIN_X, PIN_Y, PIN_BTN);

int aktuellerModus = 0; 
int letzterZaehlerstand = 0;

// --- Task-Struktur gemäß deiner Mitschrift ---
void taskA(void * pvParameters) {
    delay(500); // Delay am Anfang für Initialisierung
    for(;;) {
        Serial.println("A");
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Sekundentakt
    }
}

void taskB(void * pvParameters) {
    delay(500);
    for(;;) {
        Serial.println("b");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void taskC(void * pvParameters) {
    delay(500);
    for(;;) {
        Serial.println("C");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);

    // WICHTIG: Pin-Modus für Pull-Up setzen
    pinMode(PIN_BTN, INPUT_PULLUP);

    // Tasks erstellen
    xTaskCreate(taskA, "TaskA", 2048, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskB", 2048, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskC", 2048, NULL, 1, &handleC);

    // Zeit für mindestens einmalige Ausführung (Mitschrift)
    delay(1000); 

    // Startzustand: Nur Task A aktiv
    if(handleB) vTaskSuspend(handleB);
    if(handleC) vTaskSuspend(handleC);
}
unsigned long drueckStartZeit = 0;
bool wurdeUmschaltungAusgeloest = false;
const unsigned long LANG_KLICK_SCHWELLE = 1000; // 1 Sekunde

void loop() {
  // Wir rufen klickenErkennen weiterhin auf, falls du die Zähler später brauchst
  meinJoystick.klickenErkennen();

  // Sofort-Check: Ist der Taster gerade gedrückt?
  if (meinJoystick.isPressed()) {
    if (drueckStartZeit == 0) {
      drueckStartZeit = millis(); // Zeitpunkt des ersten Drückens merken
    }

    // Wenn die Zeit überschritten ist UND wir in diesem Druck-Zyklus noch nicht umgeschaltet haben
    if (!wurdeUmschaltungAusgeloest && (millis() - drueckStartZeit >= LANG_KLICK_SCHWELLE)) {
      
      aktuellerModus = (aktuellerModus + 1) % 3;
      wurdeUmschaltungAusgeloest = true; // Verhindert mehrfaches Umschalten während eines Haltevorgangs

      Serial.printf("\n>>> SOFORTIGER WECHSEL ZU MODUS %d <<<\n", aktuellerModus);

      // Tasks steuern
      if(handleA) vTaskSuspend(handleA);
      if(handleB) vTaskSuspend(handleB);
      if(handleC) vTaskSuspend(handleC);

      if (aktuellerModus == 0 && handleA) vTaskResume(handleA);
      else if (aktuellerModus == 1 && handleB) vTaskResume(handleB);
      else if (aktuellerModus == 2 && handleC) vTaskResume(handleC);
    }
  } else {
    // Taster losgelassen: Reset für den nächsten Klick
    drueckStartZeit = 0;
    wurdeUmschaltungAusgeloest = false;
  }

  vTaskDelay(10 / portTICK_PERIOD_MS);
}