#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include "HardwareUtils.h" // <--- Hier sind AnzeigeOben/AnzeigeUnten definiert

#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>
#include <DHT.h>
#include <ESP_Google_Sheet_Client.h>
#include "Joystick.h"
#include "TaskUhr.h"
#include "TaskWetter.h"
#include "TaskDHT.h"
#include "Config.h"
#include "TaskSnake.h"
// ==========================================
// 1. HARDWARE & KONFIGURATION
// ==========================================
#define LED_PIN_OBEN   26
#define LED_PIN_UNTEN  25
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define MATRIX_WIDTH   32
#define MATRIX_HEIGHT  8
#define MATRIX_TYPE    VERTICAL_ZIGZAG_MATRIX
#define WIDTH          32
#define HEIGHT_TOTAL   16

// --- DHT22 Setup ---
#define DHTPIN         21
#define DHTTYPE        DHT22

// --- Joystick 1 (Für Snake - INPUT_PULLDOWN) ---
#define J1_PIN_X       34
#define J1_PIN_Y       35
#define J1_PIN_TASTER  13
// Mappings, damit der unveränderte Task C funktioniert:
#define PIN_X          J1_PIN_X
#define PIN_Y          J1_PIN_Y
#define PIN_BTN        J1_PIN_TASTER

// --- Joystick 2 (Für Menü & Navigation - INPUT_PULLUP) ---
#define J2_PIN_X       36
#define J2_PIN_Y       39
#define J2_PIN_TASTER  14






// ==========================================
// 3. INSTANZEN & GLOBALE VARIABLEN
// ==========================================
Joystick joystick1(J1_PIN_X, J1_PIN_Y, J1_PIN_TASTER, INPUT_PULLDOWN); // Snake
Joystick joystick2(J2_PIN_X, J2_PIN_Y, J2_PIN_TASTER, INPUT_PULLUP);   // Menü


TaskHandle_t handleA = NULL, handleB = NULL, handleC = NULL, handleData = NULL, handleE = NULL; 

char datumBuffer[40], zeitBuffer[40];
char bufferOben[64], bufferUnten[64];
int fokusModus = 0;    // Navigation im Menü
int aktiverTask = -1;  // -1 = Menü, 0-4 = Task läuft
bool navigationsSperre = false;
unsigned long navigationSperreZeit = 0; 
int lastXPerc = 0;     
int lastYPerc = 0;     
unsigned long eventSperreBis = 0;

SemaphoreHandle_t clickCounterMutex = NULL;

const uint32_t menuIcons[5] = {
  0x24924, 0x24924, 0x24924, 0x24924, 0x24924 
};

struct Point { int x, y; };



void drawIcon(int xOffset, int yOffset, uint16_t icon, CRGB color) {
  for (int i = 0; i < 15; i++) {
    if (icon & (1 << (14 - i))) {
      setPixel(xOffset + (i % 3), yOffset + (i / 3), color);
    }
  }
}

void printMenu() {
  FastLED.clear();
  for (int i = 0; i < 5; i++) {
    CRGB farbe = (fokusModus == i) ? CRGB::Cyan : CRGB::DarkSlateGray;
    drawIcon(2 + (i * 6), 5, menuIcons[i], farbe); 
  }
  FastLED.show();
}

void tokenStatusCallback(TokenInfo info) {
    if (info.status == token_status_error) Serial.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
}

struct ClickEvent {
    int einfacherKlick;
    int doppelklick;
    int langKlick;
};

// Nutzt jetzt joystick2 (Master-Menü-Controller)
struct ClickEvent readAndClearClicks() {
    struct ClickEvent result = {0, 0, 0};
    if (xSemaphoreTake(clickCounterMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        result.einfacherKlick = joystick2.einfacherKlickZaehler;
        result.doppelklick = joystick2.doppelklickZaehler;
        result.langKlick = joystick2.langKlickZaehler;
        
        joystick2.einfacherKlickZaehler = 0;
        joystick2.doppelklickZaehler = 0;
        joystick2.langKlickZaehler = 0;
        xSemaphoreGive(clickCounterMutex);
    }
    return result;
}

bool eventSperreAktiv() {
    return (long)(millis() - eventSperreBis) < 0;
}

void setEventSperre(unsigned long dauerMs) {
    eventSperreBis = millis() + dauerMs;
}

TaskHandle_t getTaskHandle(int nummer) {
    switch (nummer) {
        case 0: return handleA;
        case 1: return handleB;
        case 2: return handleC;      // Snake
        case 3: return handleData;   // DHT & Sheets
        case 4: return handleE;
        default: return NULL;
    }
}

void wechsleZuTask(int zielTask) {
    if (zielTask < 0 || zielTask > 4) return;
    TaskHandle_t aktuellerHandle = getTaskHandle(aktiverTask);
    TaskHandle_t zielHandle = getTaskHandle(zielTask);

    FastLED.clear(true);
    
    // DHT-Task (Index 3) wird NIEMALS schlafen gelegt
    if (aktiverTask >= 0 && aktiverTask != 3 && aktuellerHandle != NULL && aktuellerHandle != zielHandle) {
        vTaskSuspend(aktuellerHandle);
    }

    aktiverTask = zielTask;
    navigationsSperre = false;
    lastXPerc = 0;  
    lastYPerc = 0;
    
    joystick2.einfacherKlickZaehler = 0;
    joystick2.doppelklickZaehler = 0;
    joystick2.langKlickZaehler = 0;

    // DHT-Task (Index 3) braucht kein vTaskResume, da er schon läuft
    if (zielTask != 3 && zielHandle != NULL) {
        vTaskResume(zielHandle);
    }
    
    setEventSperre(750);
    Serial.printf("Task %d aktiv\n", zielTask);
}

void zurueckZumMenue() {
    // DHT-Task (Index 3) wird hier ignoriert und läuft im Hintergrund weiter
    if (aktiverTask >= 0 && aktiverTask != 3) {
        TaskHandle_t aktuellerHandle = getTaskHandle(aktiverTask);
        if (aktuellerHandle != NULL) {
            vTaskSuspend(aktuellerHandle);
        }
    }

    aktiverTask = -1; // Schaltet die Display-Ausgabe im DHT-Task automatisch stumm!
    navigationsSperre = false;
    lastXPerc = 0;  
    lastYPerc = 0;
    FastLED.clear(true);
    
    joystick2.einfacherKlickZaehler = 0;
    joystick2.doppelklickZaehler = 0;
    joystick2.langKlickZaehler = 0;

    vTaskDelay(30 / portTICK_PERIOD_MS);
    printMenu();
    setEventSperre(750);
    Serial.println("Zurück zum Menü");
}


void starteTask(int nummer) { wechsleZuTask(nummer); }
void stopAlleTasks() { zurueckZumMenue(); }

// ==========================================
// 5. FREE-RTOS TASKS
// ==========================================


void taskE(void * pv) { for(;;) { Serial.println("E..."); vTaskDelay(1000/portTICK_PERIOD_MS); } }


// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
  // 1. Speichersystem mounten & Config laden
    initLittleFS();
// Lade Default-Daten beim Start, z.B. für einen leeren User
loadConfigForUser("");
    // WiFi & NTP
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
  Serial.print("\nWLAN verbunden. IP: ");
Serial.println(WiFi.localIP());

    // 2. Webserver initialisieren und als Task starten
    setupWebServer();
    TaskHandle_t handleWeb = NULL;
    xTaskCreate(taskWebServerHandler, "TaskWeb", 4096, NULL, 1, &handleWeb);
    configTime(3600, 3600, "pool.ntp.org");
    Serial.print("Warte auf Zeit-Sync...");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) { delay(500); Serial.print("."); }
    Serial.println(" Zeit synchronisiert!");

    time_t now;
    time(&now);
    GSheet.setSystemTime(now); 

    clickCounterMutex = xSemaphoreCreateMutex();

    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(15);
    
    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 1, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 1, 0);
    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff);

    // Alle Tasks in FreeRTOS einklinken
    xTaskCreate(taskUhr, "TaskA", 4096, NULL, 1, &handleA);
    xTaskCreate(taskWetter, "TaskB", 4096, NULL, 1, &handleB);
    xTaskCreate(taskSnakeHandler, "TaskC", 4096, NULL, 1, &handleC);          // Snake Task (Index 2)
    xTaskCreate(taskDataHandler, "TaskDHT", 8192, NULL, 1, &handleData); // DHT Task (Index 3)
    xTaskCreate(taskE, "TaskE", 2048, NULL, 1, &handleE);

    // Alle beim Boot suspendieren
   
    vTaskSuspend(handleA); vTaskSuspend(handleB); vTaskSuspend(handleC);
   
   // vTaskSuspend(handleData); 
    vTaskSuspend(handleE);
joystick2.setInverted(true, true);

    printMenu();
}

void loop() {
    // 1. Hardware abfragen (Master Controller = Joystick 2)
    joystick1.klickenErkennen(); // Läuft im Hintergrund mit
    joystick2.klickenErkennen(); // Ist für das Menü zuständig
    struct ClickEvent clicks = readAndClearClicks();

    // 2. Sperrzeit abwarten (Verhindert doppeltes Feuern im Umschaltmoment)
    if (eventSperreAktiv()) {
        joystick2.einfacherKlickZaehler = 0;
        joystick2.doppelklickZaehler = 0;
        joystick2.langKlickZaehler = 0;
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
        return;
    }

    // ========== IM MENÜ (aktiverTask == -1) ==========
    if (aktiverTask == -1) {
        if (clicks.langKlick > 0 || clicks.einfacherKlick > 0) {
            starteTask(fokusModus);
            return;
        }

        // Navigation im Menü über Joystick 2
        int xPerc = joystick2.readXPercent();
        int yPerc = joystick2.readYPercent();

        const int TRIG_POS = 80;    
        const int TRIG_NEG = -80;   
        const int RELEASE_ABS = 60; 
        const int NAV_TIMEOUT_MS = 200; 

      if (!navigationsSperre) {
            // Wir tauschen einfach die Logik von xPerc >= TRIG_POS zu <= TRIG_NEG
            if (xPerc <= TRIG_NEG) { // War >= TRIG_POS
                fokusModus = (fokusModus + 1) % 5;
                navigationsSperre = true;
                navigationSperreZeit = millis();
                printMenu();
            } else if (xPerc >= TRIG_POS) { // War <= TRIG_NEG
                fokusModus = (fokusModus - 1 + 5) % 5;
                navigationsSperre = true;
                navigationSperreZeit = millis();
                printMenu();
            }
        } else {
            if (abs(xPerc) < RELEASE_ABS) {
                navigationsSperre = false;
            } else if ((millis() - navigationSperreZeit) > NAV_TIMEOUT_MS) {
                navigationsSperre = false;
            }
        }
    } 
    // ========== IN LAUFENDER TASK ==========
    else {
        // EVENT 1: Double-Click in Task (Joystick 2) -> Schaltet ab und kehrt ins Menü zurück
        if (clicks.doppelklick > 0) {
            zurueckZumMenue();
            return;
        }

        // EVENT 2: Long-Click in Task (Joystick 2) -> Wechselt direkt zum nächsten Task weiter
        if (clicks.langKlick > 0) {
            int nextTask = (aktiverTask + 1) % 5;
            wechsleZuTask(nextTask);
            return;
        }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
}