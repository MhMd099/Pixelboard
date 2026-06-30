#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "time.h"
#include "HardwareUtils.h" 
#include "SoundUtils.h"

#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>

#include "Joystick.h"
#include "Config.h"

// --- TASK INCLUDES ---
#include "TaskUhr.h"
#include "TaskWetter.h"
#include "TaskSnake.h"
#include "TaskAnim.h"
#include "TaskDHT.h" // <--- NEUER DHT TASK

// ==========================================
// 1. HARDWARE & KONFIGURATION
// ==========================================
#define LED_PIN_OBEN 26
#define LED_PIN_UNTEN 25
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 8
#define MATRIX_TYPE VERTICAL_ZIGZAG_MATRIX
#define WIDTH 32
#define HEIGHT_TOTAL 16

// --- Joystick 1 (Für Snake) ---
#define J1_PIN_X 34
#define J1_PIN_Y 35
#define J1_PIN_TASTER 13
#define PIN_X J1_PIN_X
#define PIN_Y J1_PIN_Y
#define PIN_BTN J1_PIN_TASTER

// --- Joystick 2 (Für Menü & Navigation) ---
#define J2_PIN_X 36
#define J2_PIN_Y 39
#define J2_PIN_TASTER 14

// ==========================================
// 2. GLOBALE VARIABLEN & HANDLES
// ==========================================
Joystick joystick1(J1_PIN_X, J1_PIN_Y, J1_PIN_TASTER, INPUT_PULLDOWN);
Joystick joystick2(J2_PIN_X, J2_PIN_Y, J2_PIN_TASTER, INPUT_PULLUP);

// KLAR BENANNTE TASKS
TaskHandle_t handleUhr    = NULL; // Task 0
TaskHandle_t handleWetter = NULL; // Task 1
TaskHandle_t handleSnake  = NULL; // Task 2
TaskHandle_t handleMusik  = NULL; // Task 3 (War vorher handleData)
TaskHandle_t handleAnim   = NULL; // Task 4 (War vorher handleE)
TaskHandle_t handleDHT    = NULL; // Task 5 (NEU)

SemaphoreHandle_t clickCounterMutex = NULL;
SemaphoreHandle_t displayMutex = NULL; // MUTEX FÜR DAS DISPLAY

volatile int aktiverTask = -1; // -1 = Menü, 0-5 = Apps
volatile bool taskWechselAnforderung = false;
int fokusModus = 0;            // Navigation im Menü
bool navigationsSperre = false;
unsigned long eventSperreBis = 0;

// ==========================================
// 3. MENÜ-ICONS & DISPLAY-FUNKTIONEN
// ==========================================
// 6 Icons für 6 Apps
const uint32_t menuIcons[6] = {
    0x3A5A7, // 0: Uhr
    0x3EEDC, // 1: Wetter 
    0x74B5E, // 2: Snake 
    0x49249, // 3: Musik
    0x24924, // 4: Animationen
    0x72497  // 5: DHT
};

void drawIcon(int xOffset, int yOffset, uint32_t icon, CRGB color) {
    for (int i = 0; i < 15; i++) {
        if (icon & (1 << (14 - i))) {
            setPixel(xOffset + (i % 3), yOffset + (i / 3), color);
        }
    }
}

void printMenu() {
    if (!lockDisplay(20)) return;
    FastLED.clear();
    unsigned long jetzt = millis();

    // Hintergrund
    for (int x = 0; x < 32; x++)
        for (int y = 0; y < 16; y++)
            setPixel(x, y, themeCol(x * 5 - y * 3 + jetzt / 35, 55));

    // Menü-Vorschauen
    if (fokusModus == 0) {
        // ... (Dein komplexer Pixelboard-Schriftzug bleibt unverändert, ich kürze ihn hier visuell für die Lesbarkeit, du kannst deinen originalen P-I-X-E-L Code hier einsetzen)
        drawIcon(14, 5, 0x7E3F1, themeCol(jetzt / 10, 255)); // Platzhalter für Startseite
    } 
    else {
        switch (fokusModus) {
            case 1: // Uhr
                setPixel(16, 8, themeCol(jetzt / 10, 255));
                setPixel(16, 7, themeCol(jetzt / 10 + 64, 220));
                break;
            case 2: // Wetter
                setPixel(10, 6, CRGB::Yellow); setPixel(16, 9, CRGB::White);
                break;
            case 3: // Snake
                setPixel(16, 8, themeCol(jetzt / 10, 255));
                setPixel(27, 5, themeCol(jetzt / 10 + 128, 255));
                break;
            case 4: // Musik
                setPixel(16, 8, CHSV(jetzt/10, 255, 255));
                break;
            case 5: // Animationen
                setPixel(16, 8, themeCol(jetzt/5, 200));
                break;
            case 6: // DHT
                drawIcon(14, 5, menuIcons[5], themeCol(jetzt/10, 255));
                break;
        }
    }
    FastLED.show();
    unlockDisplay();
}

// ==========================================
// 4. TASK MANAGEMENT
// ==========================================
TaskHandle_t getTaskHandle(int nummer) {
    switch (nummer) {
        case 0: return handleUhr;
        case 1: return handleWetter;
        case 2: return handleSnake;
        case 3: return handleMusik;
        case 4: return handleAnim;
        case 5: return handleDHT;
        default: return NULL;
    }
}

void setEventSperre(unsigned long dauerMs) {
    eventSperreBis = millis() + dauerMs;
}
bool eventSperreAktiv() {
    return (long)(millis() - eventSperreBis) < 0;
}

void wechsleZuTask(int zielTask) {
    if (zielTask < 0 || zielTask > 5) return; // Erweitert auf 5
    
    int vorherigerTask = aktiverTask;
    aktiverTask = -2; 

    if (vorherigerTask == 3) stopMusic(); 

    if (lockDisplay(100)) {
        FastLED.clear(true);
        unlockDisplay();
    }

    aktiverTask = zielTask;
    navigationsSperre = false;
    joystick2.reset(); 

    if (zielTask == 3) startMusic(); 

    setEventSperre(250);
    Serial.printf("Task %d aktiv\n", zielTask);
}

void zurueckZumMenue() {
    int vorherigerTask = aktiverTask;
    aktiverTask = -2;

    if (vorherigerTask == 3) stopMusic(); 

    fokusModus = 0;
    navigationsSperre = false;
    
    if (lockDisplay(100)) {
        FastLED.clear(true);
        unlockDisplay();
    }

    joystick2.reset(); 
    vTaskDelay(30 / portTICK_PERIOD_MS);
    
    aktiverTask = -1;
    printMenu();
    setEventSperre(250);
}

struct ClickEvent { int einfacherKlick; int doppelklick; int langKlick; };

struct ClickEvent readAndClearClicks() {
    struct ClickEvent result = {0, 0, 0};
    if (clickCounterMutex != NULL) {
        if (xSemaphoreTake(clickCounterMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            result.einfacherKlick = joystick2.einfacherKlickZaehler;
            result.doppelklick = joystick2.doppelklickZaehler;
            result.langKlick = joystick2.langKlickZaehler;
            joystick2.einfacherKlickZaehler = 0;
            joystick2.doppelklickZaehler = 0;
            joystick2.langKlickZaehler = 0;
            xSemaphoreGive(clickCounterMutex);
        }
    }
    return result;
}

// ==========================================
// 5. MUSIK TASK (Ehemals taskDataHandler)
// ==========================================
void taskMusik(void *pv) {
    for (;;) {
        if (aktiverTask != 3) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }
        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }
        if (aktiverTask != 3) {
            unlockDisplay();
            continue;
        }

        FastLED.clear();
        unsigned long jetzt = millis();

        for (int bar = 0; bar < 8; bar++) {
            int bx = 1 + bar * 4;
            float phase = jetzt / 130.0 + bar * 0.8;
            int h = 2 + (int)(fabs(sin(phase)) * 9) + (musicBeat % 4);
            if (h > 15) h = 15;
            for (int y = 15; y > 15 - h; y--) {
                CRGB c = themeCol((15 - y) * 16 + jetzt / 20, 220); 
                setPixel(bx, y, c);
                setPixel(bx + 1, y, c);
            }
        }
        FastLED.show();
        unlockDisplay();
        vTaskDelay(45 / portTICK_PERIOD_MS);
    }
}

void taskWifiManager(void *pv) {
    bool timeConfigured = false;

    for (;;) {
        maintainWifiConnection();

        if (WiFi.status() == WL_CONNECTED) {
            if (!timeConfigured) {
                configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
                timeConfigured = true;
                Serial.println("WLAN verbunden, NTP gestartet: " + WiFi.localIP().toString());
            }
        } else {
            timeConfigured = false;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// ==========================================
// 6. SETUP
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(500);

    clickCounterMutex = xSemaphoreCreateMutex();
    displayMutex = xSemaphoreCreateMutex();
    
    initLittleFS();
    loadWifiCredentials();
    loadDeviceSettings();
    loadConfigForUser("default"); 
    startCaptivePortal();
    if (hasWifiCredentials()) beginWifiConnection();

    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(18);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 2400);

    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 1, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 1, 0);

    initAudio();
    setupWebServer();

    // ALL TASKS PINNED TO CORE 1
    xTaskCreatePinnedToCore(taskWebServerHandler, "Web", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(taskWifiManager, "WiFi", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(taskUhr, "Uhr", 4096, NULL, 1, &handleUhr, 1);
    xTaskCreatePinnedToCore(taskWetter, "Wetter", 4096, NULL, 1, &handleWetter, 1);
    xTaskCreatePinnedToCore(taskSnakeHandler, "Snake", 4096, NULL, 1, &handleSnake, 1);
    xTaskCreatePinnedToCore(taskMusik, "Musik", 2560, NULL, 1, &handleMusik, 1); 
    xTaskCreatePinnedToCore(taskAnim, "Anim", 4096, NULL, 1, &handleAnim, 1);      
    xTaskCreatePinnedToCore(taskDHTHandler, "DHT", 4096, NULL, 1, &handleDHT, 1); // <--- NEUER DHT TASK START

    xTaskCreatePinnedToCore(taskWetterFetch, "WetterNet", 8192, NULL, 1, NULL, 0);

    joystick2.setInverted(true, true);
    delay(50);
    joystick1.kalibrieren();
    joystick2.kalibrieren();
    printMenu();
}

// ==========================================
// 7. LOOP
// ==========================================
void loop() {
    joystick1.klickenErkennen();
    joystick2.klickenErkennen();

    if (eventSperreAktiv()) {
        vTaskDelay(5 / portTICK_PERIOD_MS);
        return;
    }

    struct ClickEvent clicks = readAndClearClicks();

    if (aktiverTask == -1) {
        // IM MENÜ
        if (clicks.einfacherKlick > 0 || clicks.langKlick > 0) {
            if (fokusModus > 0) {
                playSound(SND_SELECT);
                wechsleZuTask(fokusModus - 1);
                return;
            }
        }

        int xPerc = joystick2.readXPercent();
        if (!navigationsSperre) {
            if (xPerc <= -80) {
                fokusModus = (fokusModus >= 6) ? 1 : fokusModus + 1; // Erweitert auf 6
                navigationsSperre = true;
                playSound(SND_SWIPE);
            } else if (xPerc >= 80) {
                fokusModus = (fokusModus <= 1) ? 6 : fokusModus - 1; // Erweitert auf 6
                navigationsSperre = true;
                playSound(SND_SWIPE);
            }
        } else if (abs(xPerc) < 60) {
            navigationsSperre = false;
        }
        printMenu();
    } else {
        // IN EINER APP
        if (clicks.doppelklick > 0) {
            playSound(SND_DIE);
            zurueckZumMenue();
        } else if (clicks.langKlick > 0) {
            playSound(SND_SELECT);
            int next = (aktiverTask + 1) % 6; // Erweitert auf 6
            fokusModus = next + 1;
            wechsleZuTask(next);
        }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
