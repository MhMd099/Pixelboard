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
// Vorwärtsdeklarationen für den Compiler-Scope
TaskHandle_t getTaskHandle(int nummer);
void setEventSperre(unsigned long dauerMs);
void wechsleZuTask(int zielTask);
void starteTask(int nummer);

volatile bool taskWechselAnforderung = false;

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

// ==========================================
// NEUE SYMBOLE (3x5 Pixel im 15-Bit-Format)
// ==========================================
// Task 0: Uhrzeit/Uhr     -> 
// Task 1: Wetter/Wolke    -> 
// Task 2: Snake/Schlange  -> 
// Task 3: DHT (Text 'D')  -> 
// Task 4: Leer/Strich     -> Vertikaler Strich
const uint32_t menuIcons[5] = {
  0x3A5A7, // Task 0: Uhr (Kreis-Ansatz mit angedeuteten Zeigern)
  0x3EEDC, // Task 1: Wetter (Wolke mit kompakter Basis)
  0x74B5E, // Task 2: Snake (S-Form für Schlange + separater Pixel für Apfel)
  0x72497, // Task 3: DHT (Kompakter Buchstabe 'D', da 3x5 für "DHT" zu schmal ist)
  0x24924  // Task 4: Leer (Dein originaler vertikaler Strich)
};

void drawIcon(int xOffset, int yOffset, uint16_t icon, CRGB color) {
  for (int i = 0; i < 15; i++) {
    if (icon & (1 << (14 - i))) {
      setPixel(xOffset + (i % 3), yOffset + (i / 3), color);
    }
  }
}

void printMenu() {
    FastLED.clear();

    // ==========================================
    // ZUSTAND 0: DIE STARTSEITE (PIXELBOARD MENÜ)
    // ==========================================
    if (fokusModus == 0) {
        AnzeigeOben.SetText((unsigned char*)"PIXEL", 5);
        AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x9B, 0x30, 0xFF); 
        AnzeigeOben.UpdateText();

        AnzeigeUnten.SetText((unsigned char*)"MENU", 4);
        AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xFF, 0xFF); 
        AnzeigeUnten.UpdateText();
        
        FastLED.show();
        return;
    }

    // ==========================================
    // ZUSTÄNDE 1-5: SYMBOLE AUF GANZE MATRIX (32x16)
    // ==========================================
    switch (fokusModus) {
        
        case 1: { // TASK 0: KLASSISCHE UHR
            int cx = 16, cy = 8;
            for (int x = 0; x < 32; x++) {
                for (int y = 0; y < 16; y++) {
                    int dist = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                    if (dist >= 40 && dist <= 56) setPixel(x, y, CRGB::White); 
                    else if (dist < 40) setPixel(x, y, CRGB::Navy);            
                }
            }
            setPixel(16, 8, CRGB::Red);
            setPixel(16, 7, CRGB::Orange); setPixel(16, 6, CRGB::Orange); 
            setPixel(17, 8, CRGB::Yellow); setPixel(18, 8, CRGB::Yellow); 
            break;
        }

        case 2: { // TASK 1: WETTER (SONNE & WOLKE)
            int sx = 10, sy = 6;
            for (int x = 5; x <= 15; x++) {
                for (int y = 1; y <= 11; y++) {
                    if ((x-sx)*(x-sx) + (y-sy)*(y-sy) < 16) setPixel(x, y, CRGB::Yellow);
                }
            }
            setPixel(10, 1, CRGB::Orange); setPixel(10, 11, CRGB::Orange);
            setPixel(5, 6, CRGB::Orange);  setPixel(15, 6, CRGB::Orange);

            for(int x = 12; x <= 28; x++) setPixel(x, 12, CRGB::White);
            auto drawCloudBlob = [](int cx, int cy, int r) {
                for (int x = cx-r; x <= cx+r; x++) {
                    for (int y = cy-r; y <= cy+r; y++) {
                        if ((x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r && x >= 0 && x < 32 && y >= 0 && y < 16) {
                            setPixel(x, y, CRGB::White);
                        }
                    }
                }
            };
            drawCloudBlob(16, 9, 3);
            drawCloudBlob(21, 7, 4);
            drawCloudBlob(25, 10, 3);
            break;
        }

        case 3: { // TASK 2: 2-PIXEL BREITE SNAKE & APFEL (BEREINIGT)
            CRGB sColor = CRGB::Green;
            
            // Schlange (2 Pixel breit gezeichnet über exakte Schleifen)
            for(int x = 2; x <= 14; x++) { setPixel(x, 3, sColor); setPixel(x, 4, sColor); }   // Oberer horizontaler Strang
            for(int y = 5; y <= 9; y++)  { setPixel(13, y, sColor); setPixel(14, y, sColor); } // Rechter vertikaler Abstieg
            for(int x = 6; x <= 14; x++) { setPixel(x, 9, sColor); setPixel(x, 10, sColor); }  // Mittlerer horizontaler Strang
            for(int y = 11; y <= 13; y++) { setPixel(6, y, sColor); setPixel(7, y, sColor); }   // Linker vertikaler Abstieg
            for(int x = 6; x <= 18; x++) { setPixel(x, 13, sColor); setPixel(x, 14, sColor); } // Unterer horizontaler Auslauf
            
            // Schlangenkopf-Details (Auge)
            setPixel(2, 3, CRGB::DarkGreen);
            setPixel(3, 2, CRGB::White); // Auge sitzt sauber oben auf dem Kopf

            // Roter Apfel (Rechte Seite, 4 Pixel Abstand zum Auslauf)
            int ax = 24, ay = 11;
            setPixel(ax, ay, CRGB::Red);     setPixel(ax+1, ay, CRGB::Red);
            setPixel(ax, ay+1, CRGB::Red);   setPixel(ax+1, ay+1, CRGB::Red);
            setPixel(ax+1, ay-1, CRGB::Lime); // Sauberes, einzelnes Blatt direkt auf dem Apfel
            break;
        }

        case 4: { // TASK 3: DHT SCHRIFTZUG (NUR CYAN, OHNE KONTUR)
            auto drawLetterCyan = [](int xOff, int type) {
                CRGB c = CRGB::Cyan;
                if (type == 0) { // D
                    for(int y=4; y<=12; y++) { setPixel(xOff, y, c); setPixel(xOff+1, y, c); }
                    for(int x=xOff+2; x<=xOff+4; x++) { setPixel(x, 4, c); setPixel(x, 12, c); }
                    for(int y=5; y<=11; y++) { setPixel(xOff+4, y, c); }
                }
                else if (type == 1) { // H
                    for(int y=4; y<=12; y++) { setPixel(xOff, y, c); setPixel(xOff+1, y, c); setPixel(xOff+4, y, c); setPixel(xOff+5, y, c); }
                    for(int x=xOff+2; x<=xOff+3; x++) { setPixel(x, 8, c); }
                }
                else if (type == 2) { // T
                    for(int x=xOff; x<=xOff+6; x++) { setPixel(x, 4, c); setPixel(x, 5, c); }
                    for(int y=6; y<=12; y++) { setPixel(xOff+2, y, c); setPixel(xOff+3, y, c); }
                }
            };
            drawLetterCyan(3, 0);  // D
            drawLetterCyan(12, 1); // H
            drawLetterCyan(21, 2); // T
            break;
        }

        case 5: { // TASK 4: GEZENTRIERTE DOPPEL-NOTE (SECHZEHNTELNOTE)
            CRGB nColor = CRGB::Magenta;
            
            // Linker Notenkopf (Zentrierter Bereich ab X=11)
            setPixel(11, 12, nColor); setPixel(12, 12, nColor);
            setPixel(10, 13, nColor); setPixel(11, 13, nColor); setPixel(12, 13, nColor);
            setPixel(11, 14, nColor); setPixel(12, 14, nColor);

            // Rechter Notenkopf (Abstand von 5 Pixeln nach rechts)
            setPixel(18, 12, nColor); setPixel(19, 12, nColor);
            setPixel(17, 13, nColor); setPixel(18, 13, nColor); setPixel(19, 13, nColor);
            setPixel(18, 14, nColor); setPixel(19, 14, nColor);

            // Zwei vertikale Notenhälse (jeweils an der rechten Flanke des Kopfes)
            for(int y = 3; y <= 12; y++) {
                setPixel(12, y, nColor); // Linker Hals
                setPixel(19, y, nColor); // Rechter Hals
            }

            // Doppelbalken oben (Verbindung der Hälse von X=12 bis X=19)
            // Oberer Balken
            for(int x = 12; x <= 19; x++) setPixel(x, 3, nColor);
            // Unterer Parallelbalken (Typisch für Sechzehntelnote)
            for(int x = 12; x <= 19; x++) setPixel(x, 5, nColor);
            break;
        }
    }
    FastLED.show();
}

void zurueckZumMenue() {
    if (aktiverTask >= 0 && aktiverTask != 3) {
        TaskHandle_t aktuellerHandle = getTaskHandle(aktiverTask);
        if (aktuellerHandle != NULL) {
            vTaskSuspend(aktuellerHandle);
        }
    }

    aktiverTask = -1; 
    fokusModus = 0; 
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
    Serial.println("Zurück zur PIXELBOARD MENÜ Startseite");
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

    // Wenn wir aus Snake (Index 2) wechseln, setzen wir das Abbruch-Signal
    if (aktiverTask == 2) {
        taskWechselAnforderung = true;
        // Dem Snake-Task kurz Zeit geben, die Schleife sauber zu verlassen
        vTaskDelay(50 / portTICK_PERIOD_MS); 
    }

    FastLED.clear(true);
    
    if (aktiverTask >= 0 && aktiverTask != 3 && aktuellerHandle != NULL && aktuellerHandle != zielHandle) {
        vTaskSuspend(aktuellerHandle);
    }

    aktiverTask = zielTask;
    taskWechselAnforderung = false; // Zurücksetzen für den nächsten Aufruf
    navigationsSperre = false;
    lastXPerc = 0;  
    lastYPerc = 0;
    
    joystick2.einfacherKlickZaehler = 0;
    joystick2.doppelklickZaehler = 0;
    joystick2.langKlickZaehler = 0;

    if (zielTask != 3 && zielHandle != NULL) {
        vTaskResume(zielHandle);
    }
    
    setEventSperre(750);
    Serial.printf("Task %d aktiv\n", zielTask);
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
    joystick1.klickenErkennen(); 
    joystick2.klickenErkennen(); 
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
            if (fokusModus > 0) {
                starteTask(fokusModus - 1); 
                return;
            }
        }

        int xPerc = joystick2.readXPercent();
        const int TRIG_POS = 80;    
        const int TRIG_NEG = -80;   
        const int RELEASE_ABS = 60; 
        const int NAV_TIMEOUT_MS = 200; 

        if (!navigationsSperre) {
            if (xPerc <= TRIG_NEG) { 
                // Nach rechts schieben -> Seite weiter
                int naechsterModus = fokusModus + 1;
                
                // Wenn wir von den Tasks (1-5) kommen und über 5 hinausgehen, 
                // springen wir direkt zu Task 1 (Uhr) zurück und überspringen die 0
                if (naechsterModus > 5) {
                    naechsterModus = 1; 
                }
                
                fokusModus = naechsterModus;
                navigationsSperre = true;
                navigationSperreZeit = millis();
                printMenu();
            } else if (xPerc >= TRIG_POS) { 
                // Nach links schieben -> Seite zurück
                int naechsterModus = fokusModus - 1;
                
                // Wenn wir von der Startseite (0) nach links gehen, landen wir bei Task 5
                if (fokusModus == 0) {
                    naechsterModus = 5;
                }
                // Wenn wir uns bereits in den Tasks (1-5) befinden und nach links gehen,
                // blockieren wir den Rückweg zur 0 und springen direkt zu Task 5
                else if (naechsterModus < 1) {
                    naechsterModus = 5; 
                }
                
                fokusModus = naechsterModus;
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
        // EVENT 1: Double-Click in Task (Joystick 2) -> Schaltet ab und kehrt zur Startseite (0) zurück
        if (clicks.doppelklick > 0) {
            zurueckZumMenue();
            return;
        }

        // EVENT 2: Long-Click in Task (Joystick 2) -> Wechselt direkt zum nächsten Task weiter
        if (clicks.langKlick > 0) {
            int nextTask = (aktiverTask + 1) % 5;
            
            // Synchronisation: Setzt das passende Farb-Symbol für den nächsten Task
            fokusModus = nextTask + 1; 
            
            wechsleZuTask(nextTask);
            return;
        }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
}