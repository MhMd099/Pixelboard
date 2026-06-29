#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h> // NEU für pixelboard.local

#include "time.h"
#include "HardwareUtils.h" // <--- Hier sind AnzeigeOben/AnzeigeUnten definiert

#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>
#include "SoundUtils.h"

#include "Joystick.h"
#include "TaskUhr.h"
#include "TaskWetter.h"
#include "Config.h"
#include "TaskSnake.h"
#include "TaskAnim.h"
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

// --- Joystick 1 (Für Snake - INPUT_PULLDOWN) ---
#define J1_PIN_X 34
#define J1_PIN_Y 35
#define J1_PIN_TASTER 13
// Mappings, damit der unveränderte Task C funktioniert:
#define PIN_X J1_PIN_X
#define PIN_Y J1_PIN_Y
#define PIN_BTN J1_PIN_TASTER

// --- Joystick 2 (Für Menü & Navigation - INPUT_PULLUP) ---
#define J2_PIN_X 36
#define J2_PIN_Y 39
#define J2_PIN_TASTER 14

// ==========================================
// 3. INSTANZEN & GLOBALE VARIABLEN
// ==========================================
// Vorwärtsdeklarationen für den Compiler-Scope
TaskHandle_t getTaskHandle(int nummer);
void setEventSperre(unsigned long dauerMs);
void wechsleZuTask(int zielTask);
void starteTask(int nummer);
// Globale Variablen aus der Struktur übernehmen
extern SemaphoreHandle_t clickCounterMutex;
extern int fokusModus;
extern volatile int aktiverTask;
extern volatile bool taskWechselAnforderung;

volatile bool taskWechselAnforderung = false;

Joystick joystick1(J1_PIN_X, J1_PIN_Y, J1_PIN_TASTER, INPUT_PULLDOWN); // Snake
Joystick joystick2(J2_PIN_X, J2_PIN_Y, J2_PIN_TASTER, INPUT_PULLUP);   // Menü

TaskHandle_t handleA = NULL, handleB = NULL, handleC = NULL, handleData = NULL, handleE = NULL;

char datumBuffer[40], zeitBuffer[40];
char bufferOben[64], bufferUnten[64];
int fokusModus = 0;   // Navigation im Menü
volatile int aktiverTask = -1; // -1 = Menue, -2 = Wechsel, 0-4 = Task laeuft
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

void drawIcon(int xOffset, int yOffset, uint16_t icon, CRGB color)
{
    for (int i = 0; i < 15; i++)
    {
        if (icon & (1 << (14 - i)))
        {
            setPixel(xOffset + (i % 3), yOffset + (i / 3), color);
        }
    }
}

void printMenu()
{
    if (!lockDisplay(20)) return;
    FastLED.clear();

    // Zeitbasis für alle Animationen abrufen
    unsigned long jetzt = millis();

    // Dezenter, lebendiger Hintergrund (weniger Schwarz) im gewaehlten Design.
    // Niedriges V = wenig Strom; die hellen Symbole/Buchstaben darüber bleiben klar lesbar.
    for (int x = 0; x < 32; x++)
        for (int y = 0; y < 16; y++)
            setPixel(x, y, themeCol(x * 5 - y * 3 + jetzt / 35, 55));

    // ==========================================
    // ZUSTAND 0: DIE STARTSEITE (PIXELBOARD MENÜ)
    // ==========================================
    if (fokusModus == 0)
    {
        static const uint8_t glyphs[6][7] = {
            {0x1F, 0x11, 0x11, 0x1F, 0x10, 0x10, 0x10}, // P
            {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, // I
            {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
            {0x1F, 0x10, 0x10, 0x1F, 0x10, 0x10, 0x1F}, // E
            {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
            {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}  // M
        };

        struct Coord
        {
            int8_t x;
            int8_t y;
        };
        static Coord path[150];
        static int pathLength = 0;
        static bool pathInitialized = false;

        if (!pathInitialized)
        {
            int pIdx = 0;
            int xOffsets[5] = {2, 8, 14, 20, 26};
            for (char b = 0; b < 5; b++)
            {
                int xOff = xOffsets[b];
                bool upward = (b % 2 == 0);
                if (upward)
                {
                    for (int y = 6; y >= 0; y--)
                    {
                        for (int x = 0; x < 5; x++)
                            if ((glyphs[b][y] >> (4 - x)) & 1)
                                path[pIdx++] = {(int8_t)(xOff + x), (int8_t)y};
                    }
                }
                else
                {
                    for (int y = 0; y <= 6; y++)
                    {
                        for (int x = 0; x < 5; x++)
                            if ((glyphs[b][y] >> (4 - x)) & 1)
                                path[pIdx++] = {(int8_t)(xOff + x), (int8_t)y};
                    }
                }
            }
            int xOffsetsUnten[4] = {4, 10, 16, 22};
            for (int y = 6; y >= 0; y--)
            {
                for (int x = 0; x < 5; x++)
                    if ((glyphs[5][y] >> (4 - x)) & 1)
                        path[pIdx++] = {(int8_t)(xOffsetsUnten[0] + x), (int8_t)(y + 9)};
            }
            for (int y = 0; y <= 6; y++)
            {
                for (int x = 0; x < 5; x++)
                    if ((glyphs[3][y] >> (4 - x)) & 1)
                        path[pIdx++] = {(int8_t)(xOffsetsUnten[1] + x), (int8_t)(y + 9)};
            }
            for (int y = 6; y >= 0; y--)
            {
                for (int x = 0; x < 5; x++)
                    if (x == 0 || x == 4 || x == y - 1)
                        path[pIdx++] = {(int8_t)(xOffsetsUnten[2] + x), (int8_t)(y + 9)};
            }
            for (int y = 0; y <= 6; y++)
            {
                for (int x = 0; x < 5; x++)
                    if (x == 0 || x == 4 || y == 6)
                        path[pIdx++] = {(int8_t)(xOffsetsUnten[3] + x), (int8_t)(y + 9)};
            }
            pathLength = pIdx;
            pathInitialized = true;
        }

        uint8_t currentHue = jetzt / 3;
        for (int i = 0; i < pathLength; i++)
        {
            setPixel(path[i].x, path[i].y, themeCol(currentHue, 255));
            currentHue += 5;
        }
        FastLED.show();
        unlockDisplay();
        return;
    }

    // ==========================================
    // ZUSTÄNDE 1-5: ANIMIERTE SYMBOLE
    // ==========================================
    switch (fokusModus)
    {

    case 1:
    { // TASK 0: UHR (TICKT)
        int cx = 16, cy = 8;
        for (int x = 0; x < 32; x++)
        {
            for (int y = 0; y < 16; y++)
            {
                int dist = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if (dist >= 40 && dist <= 56)
                    setPixel(x, y, CRGB::White);
                else if (dist < 40)
                    setPixel(x, y, CRGB::Navy);
            }
        }
        setPixel(16, 8, CRGB::Red);

        bool tick = (jetzt / 1000) % 2 == 0;
        if (tick)
        {
            setPixel(16, 7, CRGB::Orange);
            setPixel(16, 6, CRGB::Orange);
            setPixel(17, 8, CRGB::Yellow);
            setPixel(18, 8, CRGB::Yellow);
        }
        else
        {
            setPixel(17, 7, CRGB::Orange);
            setPixel(18, 6, CRGB::Orange);
            setPixel(16, 9, CRGB::Yellow);
            setPixel(16, 10, CRGB::Yellow);
        }
        break;
    }

    case 2:
    { // TASK 1: WETTER (WOLKE ANCH SINUS-WELLE)
        int sx = 10, sy = 6;
        for (int x = 5; x <= 15; x++)
        {
            for (int y = 1; y <= 11; y++)
            {
                if ((x - sx) * (x - sx) + (y - sy) * (y - sy) < 16)
                    setPixel(x, y, CRGB::Yellow);
            }
        }
        setPixel(10, 1, CRGB::Orange);
        setPixel(10, 11, CRGB::Orange);
        setPixel(5, 6, CRGB::Orange);
        setPixel(15, 6, CRGB::Orange);

        int wX = (sin(jetzt / 400.0) * 2.0);

        for (int x = 12 + wX; x <= 28 + wX; x++)
            if (x >= 0 && x < 32)
                setPixel(x, 12, CRGB::White);
        auto drawCloudBlobAnim = [wX](int cx, int cy, int r)
        {
            int dynCx = cx + wX;
            for (int x = dynCx - r; x <= dynCx + r; x++)
            {
                for (int y = cy - r; y <= cy + r; y++)
                {
                    if ((x - dynCx) * (x - dynCx) + (y - cy) * (y - cy) <= r * r && x >= 0 && x < 32 && y >= 0 && y < 16)
                    {
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

    case 3:
    { // TASK 2: SNAKE-VORSCHAU (glatt schlängelnd, Farbverlauf Kopf -> Schwanz)
        const int len = 16;
        int head = (int)((jetzt / 80) % 44) - 6; // läuft sauber von links durch nach rechts

        for (int i = 0; i < len; i++)
        {
            int x = head - i;
            if (x < 0 || x >= 32)
                continue;
            int y = 8 + (int)(sin(x / 3.2 + jetzt / 280.0) * 4.0);
            if (y < 0 || y >= 16)
                continue;

            if (i == 0)
            {                                       // Kopf
                setPixel(x, y, CRGB::White);        // heller Kopf
                setPixel(x, y - 1, CHSV(96, 255, 200));
                if ((jetzt / 180) % 2)
                    setPixel(x + 1, y, CRGB::Red);  // Zunge flackert
            }
            else
            {
                uint8_t v = (uint8_t)map(i, 1, len, 235, 60); // hell -> dunkel
                setPixel(x, y, CHSV(96, 255, v));             // Grün-Verlauf
            }
        }

        // Pulsierender Apfel mit Blatt
        uint8_t puls = 150 + (uint8_t)(sin(jetzt / 200.0) * 90);
        setPixel(27, 5, CHSV(0, 255, puls));
        setPixel(27, 4, CRGB::Lime);
        break;
    }

    case 4:
    { // TASK 3: MUSIK (mehrere bunte Achtelnoten, die im Takt wippen)
        auto drawNote = [&](int nx, int ny, CRGB col)
        {
            // Notenkopf (2x2)
            setPixel(nx, ny + 2, col);
            setPixel(nx + 1, ny + 2, col);
            setPixel(nx, ny + 3, col);
            setPixel(nx + 1, ny + 3, col);
            // Notenhals
            for (int y = ny - 3; y <= ny + 2; y++)
                setPixel(nx + 1, y, col);
            // Fähnchen
            setPixel(nx + 2, ny - 3, col);
            setPixel(nx + 2, ny - 2, col);
        };

        int b1 = (int)(sin(jetzt / 180.0) * 2.0);
        int b2 = (int)(sin(jetzt / 180.0 + 1.2) * 2.0);
        int b3 = (int)(sin(jetzt / 180.0 + 2.4) * 2.0);
        drawNote(5, 7 + b1, CHSV((uint8_t)(jetzt / 12), 255, 235));
        drawNote(14, 8 + b2, CHSV((uint8_t)(jetzt / 12 + 85), 255, 235));
        drawNote(23, 7 + b3, CHSV((uint8_t)(jetzt / 12 + 170), 255, 235));
        break;
    }

    case 5:
    { // TASK 4: ANIMATIONEN (themed Plasma-Vorschau)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 32; x++)
            {
                int v = (sin8(x * 14 + jetzt / 6) + sin8(y * 16 - jetzt / 8) + sin8((x * 3 + y * 5) + jetzt / 5)) / 3;
                setPixel(x, y, themeCol(v + jetzt / 30, 200));
            }
        break;
    }
    }
    FastLED.show();
    unlockDisplay();
}

void zurueckZumMenue()
{
    int vorherigerTask = aktiverTask;
    aktiverTask = -2;

    if (vorherigerTask == 3)
        stopMusic(); // Musik stoppen, falls wir aus der Musik-App kommen

    fokusModus = 0;
    navigationsSperre = false;
    lastXPerc = 0;
    lastYPerc = 0;
    if (lockDisplay(100)) {
        FastLED.clear(true);
        unlockDisplay();
    }

    joystick2.reset(); // Klick-Zustand sauber zuruecksetzen

    vTaskDelay(30 / portTICK_PERIOD_MS);
    aktiverTask = -1;
    printMenu();
    setEventSperre(250);
    Serial.println("Zurück zur PIXELBOARD MENÜ Startseite");
}

struct ClickEvent
{
    int einfacherKlick;
    int doppelklick;
    int langKlick;
};

// Nutzt jetzt joystick2 (Master-Menü-Controller)
struct ClickEvent readAndClearClicks() {
    struct ClickEvent result = {0, 0, 0};
    
    // SICHERHEIT: Prüfen ob Mutex existiert
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

bool eventSperreAktiv()
{
    return (long)(millis() - eventSperreBis) < 0;
}

void setEventSperre(unsigned long dauerMs)
{
    eventSperreBis = millis() + dauerMs;
}

TaskHandle_t getTaskHandle(int nummer)
{
    switch (nummer)
    {
    case 0:
        return handleA;
    case 1:
        return handleB;
    case 2:
        return handleC; // Snake
    case 3:
        return handleData; // DHT & Sheets
    case 4:
        return handleE;
    default:
        return NULL;
    }
}

void wechsleZuTask(int zielTask)
{
    if (zielTask < 0 || zielTask > 4)
        return;
    int vorherigerTask = aktiverTask;
    aktiverTask = -2; // Wechselzustand: kein Display-Task darf neu zeichnen

    if (vorherigerTask == 3)
        stopMusic(); // Musik beim Verlassen der Musik-App stoppen

    if (lockDisplay(100)) {
        FastLED.clear(true);
        unlockDisplay();
    }

    aktiverTask = zielTask;
    taskWechselAnforderung = false; // Zurücksetzen für den nächsten Aufruf
    navigationsSperre = false;
    lastXPerc = 0;
    lastYPerc = 0;

    joystick2.reset(); // Klick-Zustand sauber zuruecksetzen (kein Nachfeuern)

    if (zielTask == 3)
        startMusic(); // Musik beim Betreten der Musik-App starten

    setEventSperre(250);
    Serial.printf("Task %d aktiv\n", zielTask);
}

void starteTask(int nummer) { wechsleZuTask(nummer); }
void stopAlleTasks() { zurueckZumMenue(); }

// ==========================================
// 5. FREE-RTOS TASKS
// ==========================================

// Musik-App: Equalizer-Visualizer, der zur prozeduralen Melodie tanzt.
// Die eigentliche Melodie spielt der AudioTask (SoundUtils); musicBeat liefert den Takt.
void taskMusik(void *pv)
{
    for (;;)
    {
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

        // 8 Balken über die Breite, unten grün -> oben rot
        for (int bar = 0; bar < 8; bar++)
        {
            int bx = 1 + bar * 4;
            float phase = jetzt / 130.0 + bar * 0.8;
            int h = 2 + (int)(fabs(sin(phase)) * 9) + (musicBeat % 4);
            if (h > 15) h = 15;
            for (int y = 15; y > 15 - h; y--)
            {
                CRGB c = themeCol((15 - y) * 16 + jetzt / 20, 220); // Balkenverlauf im Design
                setPixel(bx, y, c);
                setPixel(bx + 1, y, c);
            }
        }
        FastLED.show();
        unlockDisplay();
        vTaskDelay(45 / portTICK_PERIOD_MS);
    }
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- PIXELBOARD START ---");

    clickCounterMutex = xSemaphoreCreateMutex();
    displayMutex = xSemaphoreCreateMutex();
    initLittleFS();
    loadConfigForUser(""); // Lädt Stadt & User

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);
    Serial.print("Verbinde WiFi");
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 30) { // Erhöht auf 30 (15 Sek.)
        delay(500); 
        Serial.print("."); 
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nVerbunden!");
        Serial.print("IP-Adresse: ");
        Serial.println(WiFi.localIP());

        // Dem ESP32 kurz Zeit geben, die Netzwerk-Routen zu stabilisieren
        delay(1000); 

        if (MDNS.begin("pixelboard")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("DNS: http://pixelboard.local");
        }
        
        // MODERNE METHODE: Setzt die Zeitzone für Europa/Berlin (inkl. automatischer Sommer-/Winterzeit)
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
        Serial.println("NTP-Anfrage gestartet...");
    } else {
        Serial.println("\nWLAN-Verbindung fehlgeschlagen! Uhr wird nicht synchronisieren.");
    }
    startCaptivePortal();

    // Matrix & Audio
    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(18);
    // Hartes Strom-Limit fürs 5V/3A-Netzteil: max ~2400mA für die LEDs (~600mA Reserve
    // für den ESP32). FastLED dimmt automatisch ab, bevor zu viel gezogen wird -> kein Brownout.
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 2400);

    // WICHTIG: Text-Anzeigen initialisieren, sonst stürzt taskUhr beim Auswählen ab!
    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 1, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 1, 0);
    AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);
    AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff);

    initAudio();

    // Webserver & Apps
    setupWebServer();
    // Netzwerk-Tasks auf Kern 0 (stoeren das Rendern nicht)
    xTaskCreatePinnedToCore(taskWebServerHandler, "Web", 4096, NULL, 1, NULL, 0);

    // ALLE Anzeige-Tasks auf Kern 1 pinnen -> nie zwei gleichzeitig auf FastLED
    // (verhindert "durcheinander", NTP-im-Menue und das Einfrieren)
    xTaskCreatePinnedToCore(taskUhr, "Uhr", 4096, NULL, 1, &handleA, 1);
    xTaskCreatePinnedToCore(taskWetter, "Wetter", 4096, NULL, 1, &handleB, 1);
    xTaskCreatePinnedToCore(taskSnakeHandler, "Snake", 4096, NULL, 1, &handleC, 1);
    xTaskCreatePinnedToCore(taskMusik, "Musik", 2560, NULL, 1, &handleData, 1); // Task 3
    xTaskCreatePinnedToCore(taskAnim, "Anim", 4096, NULL, 1, &handleE, 1);      // Task 4

    // Wetterdaten im Hintergrund auf Kern 0 (blockiert nie die Anzeige)
    xTaskCreatePinnedToCore(taskWetterFetch, "WetterNet", 8192, NULL, 1, NULL, 0);

    joystick2.setInverted(true, true);
    // Joysticks im Ruhezustand neu kalibrieren (ADC jetzt bereit) -> verhindert haengende Navigation
    delay(50);
    joystick1.kalibrieren();
    joystick2.kalibrieren();
    printMenu();
}

void loop() {
    joystick1.klickenErkennen();
    joystick2.klickenErkennen();

    if (eventSperreAktiv()) {
        // Klicks NICHT verwerfen - sie werden direkt nach der kurzen Sperre verarbeitet
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
                return; // WICHTIG: nicht danach noch printMenu() ueber die App zeichnen
            }
        }

        int xPerc = joystick2.readXPercent();
        if (!navigationsSperre) {
            if (xPerc <= -80) {
                fokusModus = (fokusModus >= 5) ? 1 : fokusModus + 1;
                navigationsSperre = true;
                playSound(SND_SWIPE);
            } else if (xPerc >= 80) {
                fokusModus = (fokusModus <= 1) ? 5 : fokusModus - 1;
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
            int next = (aktiverTask + 1) % 5; // Uhr, Wetter, Snake, Musik, Animationen
            fokusModus = next + 1;
            wechsleZuTask(next);
        }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
