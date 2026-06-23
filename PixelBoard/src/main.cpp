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

    // Zeitbasis für alle Animationen abrufen
    unsigned long jetzt = millis();

    // ==========================================
    // ZUSTAND 0: DIE STARTSEITE (PIXELBOARD MENÜ)
    // ==========================================
    if (fokusModus == 0) {
        static const uint8_t glyphs[6][7] = {
            {0x1F, 0x11, 0x11, 0x1F, 0x10, 0x10, 0x10}, // P
            {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, // I
            {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
            {0x1F, 0x10, 0x10, 0x1F, 0x10, 0x10, 0x1F}, // E
            {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
            {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}  // M
        };

        struct Coord { int8_t x; int8_t y; };
        static Coord path[150];
        static int pathLength = 0;
        static bool pathInitialized = false;

        if (!pathInitialized) {
            int pIdx = 0;
            int xOffsets[5] = {2, 8, 14, 20, 26}; 
            for (char b = 0; b < 5; b++) {
                int xOff = xOffsets[b];
                bool upward = (b % 2 == 0);
                if (upward) {
                    for (int y = 6; y >= 0; y--) {
                        for (int x = 0; x < 5; x++) if ((glyphs[b][y] >> (4 - x)) & 1) path[pIdx++] = { (int8_t)(xOff + x), (int8_t)y };
                    }
                } else {
                    for (int y = 0; y <= 6; y++) {
                        for (int x = 0; x < 5; x++) if ((glyphs[b][y] >> (4 - x)) & 1) path[pIdx++] = { (int8_t)(xOff + x), (int8_t)y };
                    }
                }
            }
            int xOffsetsUnten[4] = {4, 10, 16, 22};
            for (int y = 6; y >= 0; y--) {
                for (int x = 0; x < 5; x++) if ((glyphs[5][y] >> (4 - x)) & 1) path[pIdx++] = { (int8_t)(xOffsetsUnten[0] + x), (int8_t)(y + 9) };
            }
            for (int y = 0; y <= 6; y++) {
                for (int x = 0; x < 5; x++) if ((glyphs[3][y] >> (4 - x)) & 1) path[pIdx++] = { (int8_t)(xOffsetsUnten[1] + x), (int8_t)(y + 9) };
            }
            for (int y = 6; y >= 0; y--) {
                for (int x = 0; x < 5; x++) if (x==0 || x==4 || x==y-1) path[pIdx++] = { (int8_t)(xOffsetsUnten[2] + x), (int8_t)(y + 9) };
            }
            for (int y = 0; y <= 6; y++) {
                for (int x = 0; x < 5; x++) if (x==0 || x==4 || y==6) path[pIdx++] = { (int8_t)(xOffsetsUnten[3] + x), (int8_t)(y + 9) };
            }
            pathLength = pIdx;
            pathInitialized = true;
        }

        uint8_t startHue = jetzt / 3; 
        uint8_t currentHue = startHue;
        for (int i = 0; i < pathLength; i++) {
            setPixel(path[i].x, path[i].y, CHSV(currentHue, 255, 255));
            currentHue += 5;
        }
        FastLED.show();
        return;
    }

    // ==========================================
    // ZUSTÄNDE 1-5: ANIMIERTE SYMBOLE
    // ==========================================
    switch (fokusModus) {
        
        case 1: { // TASK 0: UHR (TICKT)
            int cx = 16, cy = 8;
            for (int x = 0; x < 32; x++) {
                for (int y = 0; y < 16; y++) {
                    int dist = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                    if (dist >= 40 && dist <= 56) setPixel(x, y, CRGB::White); 
                    else if (dist < 40) setPixel(x, y, CRGB::Navy);            
                }
            }
            setPixel(16, 8, CRGB::Red);
            
            // Jede Sekunde (1000ms) wechselt der Sekundenzeiger die Richtung (Tick-Tack)
            bool tick = (jetzt / 1000) % 2 == 0;
            if (tick) {
                setPixel(16, 7, CRGB::Orange); setPixel(16, 6, CRGB::Orange); // Zeiger oben
                setPixel(17, 8, CRGB::Yellow); setPixel(18, 8, CRGB::Yellow); // Zeiger rechts
            } else {
                setPixel(17, 7, CRGB::Orange); setPixel(18, 6, CRGB::Orange); // Zeiger schräg oben rechts
                setPixel(16, 9, CRGB::Yellow); setPixel(16, 10, CRGB::Yellow); // Zeiger unten
            }
            break;
        }

        case 2: { // TASK 1: WETTER (WOLKE BEWEGT SICH NACH LINKS/RECHTS)
            // Sonne bleibt statisch im Hintergrund
            int sx = 10, sy = 6;
            for (int x = 5; x <= 15; x++) {
                for (int y = 1; y <= 11; y++) {
                    if ((x-sx)*(x-sx) + (y-sy)*(y-sy) < 16) setPixel(x, y, CRGB::Yellow);
                }
            }
            setPixel(10, 1, CRGB::Orange); setPixel(10, 11, CRGB::Orange);
            setPixel(5, 6, CRGB::Orange);  setPixel(15, 6, CRGB::Orange);

            // Dynamischer X-Versatz für die Wolke über eine Sinus-Welle (-2 bis +2 Pixel)
            int wX = (sin(jetzt / 400.0) * 2.0);

            for(int x = 12 + wX; x <= 28 + wX; x++) if(x>=0 && x<32) setPixel(x, 12, CRGB::White);
            auto drawCloudBlobAnim = [wX](int cx, int cy, int r) {
                int dynCx = cx + wX;
                for (int x = dynCx-r; x <= dynCx+r; x++) {
                    for (int y = cy-r; y <= cy+r; y++) {
                        if ((x-dynCx)*(x-dynCx) + (y-cy)*(y-cy) <= r*r && x >= 0 && x < 32 && y >= 0 && y < 16) {
                            setPixel(x, y, CRGB::White);
                        }
                    }
                }
            };
            drawCloudBlobAnim(16, 9, 3);
            drawCloudBlobAnim(21, 7, 4);
            drawCloudBlobAnim(25, 10, 3);
            break;
        }

        case 3: { // TASK 2: SNAKE BEWEGT SICH RICHTUNG APFEL
            CRGB sColor = CRGB::Green;
            // Der X-Versatz lässt die Schlange periodisch 0 bis 3 Pixel nach rechts kriechen
            int sX = (jetzt / 150) % 4; 

            for(int x = 2 + sX; x <= 14 + sX; x++) { if(x<32) { setPixel(x, 3, sColor); setPixel(x, 4, sColor); } }   
            for(int y = 5; y <= 9; y++)  { if(13+sX<32) setPixel(13+sX, y, sColor); if(14+sX<32) setPixel(14+sX, y, sColor); } 
            for(int x = 6 + sX; x <= 14 + sX; x++) { if(x<32) { setPixel(x, 9, sColor); setPixel(x, 10, sColor); } }  
            for(int y = 11; y <= 13; y++) { if(6+sX<32) setPixel(6+sX, y, sColor); if(7+sX<32) setPixel(7+sX, y, sColor); }   
            for(int x = 6 + sX; x <= 18 + sX; x++) { if(x<32) { setPixel(x, 13, sColor); setPixel(x, 14, sColor); } } 
            
            if(2+sX<32) setPixel(2+sX, 3, CRGB::DarkGreen);
            if(3+sX<32) setPixel(3+sX, 2, CRGB::White); 

            // Apfel bleibt fest verankert stehen, Schlange kriecht darauf zu
            int ax = 26, ay = 11;
            setPixel(ax, ay, CRGB::Red);     setPixel(ax+1, ay, CRGB::Red);
            setPixel(ax, ay+1, CRGB::Red);   setPixel(ax+1, ay+1, CRGB::Red);
            setPixel(ax+1, ay-1, CRGB::Lime); 
            break;
        }

        case 4: { // TASK 3: DHT SCHRIFTZUG (BLAU-IN-BLAU VERLAUF)
            auto drawLetterCyanAnim = [jetzt](int xOff, int type) {
                if (type == 0) { // D
                    for(int y=4; y<=12; y++) {
                        // Jedes Pixel berechnet seine Farbe basierend auf Y und Zeit
                        uint8_t hue = 130 + (sin((jetzt / 200.0) + y) * 20); 
                        setPixel(xOff, y, CHSV(hue, 255, 255)); setPixel(xOff+1, y, CHSV(hue, 255, 255));
                    }
                    for(int x=xOff+2; x<=xOff+4; x++) {
                        uint8_t hue = 130 + (sin((jetzt / 200.0) + x) * 20);
                        setPixel(x, 4, CHSV(hue, 255, 255)); setPixel(x, 12, CHSV(hue, 255, 255));
                    }
                    for(int y=5; y<=11; y++) {
                        uint8_t hue = 130 + (sin((jetzt / 200.0) + y) * 20);
                        setPixel(xOff+4, y, CHSV(hue, 255, 255));
                    }
                }
                else if (type == 1) { // H
                    for(int y=4; y<=12; y++) {
                        uint8_t hue = 130 + (sin((jetzt / 200.0) + y) * 20);
                        setPixel(xOff, y, CHSV(hue, 255, 255)); setPixel(xOff+1, y, CHSV(hue, 255, 255));
                        setPixel(xOff+4, y, CHSV(hue, 255, 255)); setPixel(xOff+5, y, CHSV(hue, 255, 255));
                    }
                    for(int x=xOff+2; x<=xOff+3; x++) {
                        uint8_t hue = 130 + (sin((jetzt / 200.0) + x) * 20);
                        setPixel(x, 8, CHSV(hue, 255, 255));
                    }
                }
                else if (type == 2) { // T
                    for(int x=xOff; x<=xOff+6; x++) {
                        uint8_t hue = 130 + (sin((jetzt / 200.0) + x) * 20);
                        setPixel(x, 4, CHSV(hue, 255, 255)); setPixel(x, 5, CHSV(hue, 255, 255));
                    }
                    for(int y=6; y<=12; y++) {
                        uint8_t hue = 130 + (sin((jetzt / 200.0) + y) * 20);
                        setPixel(xOff+2, y, CHSV(hue, 255, 255)); setPixel(xOff+3, y, CHSV(hue, 255, 255));
                    }
                }
            };
            drawLetterCyanAnim(3, 0);  
            drawLetterCyanAnim(12, 1); 
            drawLetterCyanAnim(21, 2); 
            break;
        }

        case 5: { // TASK 4: GEZENTRIERTE SECHZEHNTELNOTE (HÜPFT HOCH UND RUNTER)
            CRGB nColor = CRGB::Magenta;
            // Vertikaler Versatz simuliert das Hüpfen im Rhythmus (0 bis -2 Pixel nach oben)
            int bounceY = (int)(abs(sin(jetzt / 150.0)) * -2.5);

            setPixel(11, 12+bounceY, nColor); setPixel(12, 12+bounceY, nColor);
            setPixel(10, 13+bounceY, nColor); setPixel(11, 13+bounceY, nColor); setPixel(12, 13+bounceY, nColor);
            setPixel(11, 14+bounceY, nColor); setPixel(12, 14+bounceY, nColor);

            setPixel(18, 12+bounceY, nColor); setPixel(19, 12+bounceY, nColor);
            setPixel(17, 13+bounceY, nColor); setPixel(18, 13+bounceY, nColor); setPixel(19, 13+bounceY, nColor);
            setPixel(18, 14+bounceY, nColor); setPixel(19, 14+bounceY, nColor);

            for(int y = 3; y <= 12; y++) {
                setPixel(12, y+bounceY, nColor); 
                setPixel(19, y+bounceY, nColor); 
            }

            for(int x = 12; x <= 19; x++) setPixel(x, 3+bounceY, nColor);
            for(int x = 12; x <= 19; x++) setPixel(x, 5+bounceY, nColor);
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
                int naechsterModus = fokusModus + 1;
                if (naechsterModus > 5) naechsterModus = 1; 
                fokusModus = naechsterModus;
                navigationsSperre = true;
                navigationSperreZeit = millis();
                printMenu();
            } else if (xPerc >= TRIG_POS) { 
                int naechsterModus = fokusModus - 1;
                if (fokusModus == 0) naechsterModus = 5;
                else if (naechsterModus < 1) naechsterModus = 5; 
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

        // KORREKTUR: printMenu() wird jetzt immer aufgerufen, damit alle Symbole animieren
        printMenu(); 
    } 
    // ========== IN LAUFENDER TASK ==========
    else {
        if (clicks.doppelklick > 0) {
            zurueckZumMenue();
            return;
        }

        if (clicks.langKlick > 0) {
            int nextTask = (aktiverTask + 1) % 5;
            fokusModus = nextTask + 1; 
            wechsleZuTask(nextTask);
            return;
        }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
}