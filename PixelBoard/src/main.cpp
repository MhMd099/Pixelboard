#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>

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
#include "RaumschiffGame.h"
#include "TaskRaumschiff.h"
#include <LittleFS.h>

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

// --- Joystick 3 (Nano via I2C, keine ESP32-pinMode/analogRead-Logik) ---
#define I2C_SDA 32
#define I2C_SCL 33
#define J3_I2C_ADDRESS 0x08

// ==========================================
// 2. GLOBALE VARIABLEN & HANDLES
// ==========================================
Joystick joystick1(J1_PIN_X, J1_PIN_Y, J1_PIN_TASTER, INPUT_PULLDOWN);
Joystick joystick2(J2_PIN_X, J2_PIN_Y, J2_PIN_TASTER, INPUT_PULLUP);
Joystick joystick3;

// KLAR BENANNTE TASKS
TaskHandle_t handleUhr = NULL;    // Task 0
TaskHandle_t handleWetter = NULL; // Task 1
TaskHandle_t handleSnake = NULL;  // Task 2
TaskHandle_t handleMusik = NULL;  // Task 3 (War vorher handleData)
TaskHandle_t handleAnim = NULL;   // Task 4 (War vorher handleE)
TaskHandle_t handleDHT = NULL;    // Task 5 (NEU)
TaskHandle_t handleRaumschiff = NULL;

SemaphoreHandle_t clickCounterMutex = NULL;
SemaphoreHandle_t displayMutex = NULL; // MUTEX FÜR DAS DISPLAY

volatile int aktiverTask = -1; // -1 = Menü, 0-5 = Apps
volatile bool taskWechselAnforderung = false;
int fokusModus = 0; // Navigation im Menü
bool navigationsSperre = false;
unsigned long eventSperreBis = 0;

// ==========================================
// 3. MENÜ-ICONS & DISPLAY-FUNKTIONEN
// ==========================================
// 6 Icons für 6 Apps
const uint32_t menuIcons[7] = {
    0x3A5A7, // 0: Uhr
    0x3EEDC, // 1: Wetter
    0x74B5E, // 2: Snake
    0x49249, // 3: Musik
    0x24924, // 4: Animationen
    0x72497, // 5: DHT
    0x5A5A5  // 6 Raumschiff (einfach placeholder)

};

void drawIcon(int xOffset, int yOffset, uint32_t icon, CRGB color)
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
    if (!lockDisplay(20))
        return;
    FastLED.clear();
    unsigned long jetzt = millis();

    for (int x = 0; x < 32; x++)
        for (int y = 0; y < 16; y++)
            setPixel(x, y, themeCol(x * 5 - y * 3 + jetzt / 35, 55));

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
                        for (int x = 0; x < 5; x++)
                            if ((glyphs[b][y] >> (4 - x)) & 1)
                                path[pIdx++] = {(int8_t)(xOff + x), (int8_t)y};
                }
                else
                {
                    for (int y = 0; y <= 6; y++)
                        for (int x = 0; x < 5; x++)
                            if ((glyphs[b][y] >> (4 - x)) & 1)
                                path[pIdx++] = {(int8_t)(xOff + x), (int8_t)y};
                }
            }

            int xOffsetsUnten[4] = {4, 10, 16, 22};
            for (int y = 6; y >= 0; y--)
                for (int x = 0; x < 5; x++)
                    if ((glyphs[5][y] >> (4 - x)) & 1)
                        path[pIdx++] = {(int8_t)(xOffsetsUnten[0] + x), (int8_t)(y + 9)};
            for (int y = 0; y <= 6; y++)
                for (int x = 0; x < 5; x++)
                    if ((glyphs[3][y] >> (4 - x)) & 1)
                        path[pIdx++] = {(int8_t)(xOffsetsUnten[1] + x), (int8_t)(y + 9)};
            for (int y = 6; y >= 0; y--)
                for (int x = 0; x < 5; x++)
                    if (x == 0 || x == 4 || x == y - 1)
                        path[pIdx++] = {(int8_t)(xOffsetsUnten[2] + x), (int8_t)(y + 9)};
            for (int y = 0; y <= 6; y++)
                for (int x = 0; x < 5; x++)
                    if (x == 0 || x == 4 || y == 6)
                        path[pIdx++] = {(int8_t)(xOffsetsUnten[3] + x), (int8_t)(y + 9)};

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

    switch (fokusModus)
    {
    case 1:
    {
        int cx = 16, cy = 8;
        for (int x = 0; x < 32; x++)
        {
            for (int y = 0; y < 16; y++)
            {
                int dist = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if (dist >= 40 && dist <= 56)
                    setPixel(x, y, themeCol(jetzt / 30 + x * 4, 210));
                else if (dist < 40)
                    setPixel(x, y, themeCol(jetzt / 45 + y * 6, 45));
            }
        }

        setPixel(16, 8, themeCol(jetzt / 20, 255));
        bool tick = (jetzt / 1000) % 2 == 0;
        if (tick)
        {
            setPixel(16, 7, themeCol(jetzt / 20 + 64, 255));
            setPixel(16, 6, themeCol(jetzt / 20 + 64, 255));
            setPixel(17, 8, themeCol(jetzt / 20 + 128, 255));
            setPixel(18, 8, themeCol(jetzt / 20 + 128, 255));
        }
        else
        {
            setPixel(17, 7, themeCol(jetzt / 20 + 64, 255));
            setPixel(18, 6, themeCol(jetzt / 20 + 64, 255));
            setPixel(16, 9, themeCol(jetzt / 20 + 128, 255));
            setPixel(16, 10, themeCol(jetzt / 20 + 128, 255));
        }
        break;
    }

    case 2:
    {
        int sx = 10, sy = 6;
        for (int x = 5; x <= 15; x++)
            for (int y = 1; y <= 11; y++)
                if ((x - sx) * (x - sx) + (y - sy) * (y - sy) < 16)
                    setPixel(x, y, CRGB::Yellow);

        setPixel(10, 1, CRGB::Orange);
        setPixel(10, 11, CRGB::Orange);
        setPixel(5, 6, CRGB::Orange);
        setPixel(15, 6, CRGB::Orange);

        int wX = (int)(sin(jetzt / 400.0) * 2.0);
        for (int x = 12 + wX; x <= 28 + wX; x++)
            if (x >= 0 && x < 32)
                setPixel(x, 12, CRGB::White);

        auto drawCloudBlobAnim = [wX](int cx, int cy, int r)
        {
            int dynCx = cx + wX;
            for (int x = dynCx - r; x <= dynCx + r; x++)
                for (int y = cy - r; y <= cy + r; y++)
                    if ((x - dynCx) * (x - dynCx) + (y - cy) * (y - cy) <= r * r && x >= 0 && x < 32 && y >= 0 && y < 16)
                        setPixel(x, y, CRGB::White);
        };
        drawCloudBlobAnim(16, 9, 3);
        drawCloudBlobAnim(21, 7, 4);
        drawCloudBlobAnim(25, 10, 3);
        break;
    }

    case 3:
    {
        const int len = 16;
        int head = (int)((jetzt / 80) % 44) - 6;
        for (int i = 0; i < len; i++)
        {
            int x = head - i;
            if (x < 0 || x >= 32)
                continue;
            int y = 8 + (int)(sin(x / 3.2 + jetzt / 280.0) * 4.0);
            if (y < 0 || y >= 16)
                continue;

            if (i == 0)
            {
                setPixel(x, y, themeCol(jetzt / 20, 255));
                setPixel(x, y - 1, themeCol(jetzt / 20 + 64, 200));
                if ((jetzt / 180) % 2)
                    setPixel(x + 1, y, themeCol(jetzt / 20 + 128, 255));
            }
            else
            {
                uint8_t v = (uint8_t)map(i, 1, len, 235, 60);
                setPixel(x, y, themeCol(jetzt / 35 + i * 8, v));
            }
        }

        uint8_t puls = 150 + (uint8_t)(sin(jetzt / 200.0) * 90);
        setPixel(27, 5, themeCol(jetzt / 20 + 160, puls));
        setPixel(27, 4, themeCol(jetzt / 20 + 96, 220));
        break;
    }

    case 4:
    {
        auto drawNote = [&](int nx, int ny, CRGB col)
        {
            setPixel(nx, ny + 2, col);
            setPixel(nx + 1, ny + 2, col);
            setPixel(nx, ny + 3, col);
            setPixel(nx + 1, ny + 3, col);
            for (int y = ny - 3; y <= ny + 2; y++)
                setPixel(nx + 1, y, col);
            setPixel(nx + 2, ny - 3, col);
            setPixel(nx + 2, ny - 2, col);
        };

        int b1 = (int)(sin(jetzt / 180.0) * 2.0);
        int b2 = (int)(sin(jetzt / 180.0 + 1.2) * 2.0);
        int b3 = (int)(sin(jetzt / 180.0 + 2.4) * 2.0);
        drawNote(5, 7 + b1, themeCol(jetzt / 12, 235));
        drawNote(14, 8 + b2, themeCol(jetzt / 12 + 85, 235));
        drawNote(23, 7 + b3, themeCol(jetzt / 12 + 170, 235));
        break;
    }

    case 5:
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 32; x++)
            {
                int v = (sin8(x * 14 + jetzt / 6) + sin8(y * 16 - jetzt / 8) + sin8((x * 3 + y * 5) + jetzt / 5)) / 3;
                setPixel(x, y, themeCol(v + jetzt / 30, 200));
            }
        break;

    case 6:
    {
        // DHT

        drawChar3x5(5, 2, 'D',
                    themeCol(jetzt / 20, 255));

        drawChar3x5(13, 2, 'H',
                    themeCol(jetzt / 20 + 70, 255));

        drawChar3x5(21, 2, 'T',
                    themeCol(jetzt / 20 + 140, 255));

        break;
    }

    case 7:
    {
        // Raumschiff

        drawChar3x5(3, 2, 'R',
                    themeCol(jetzt / 20, 255));

        drawChar3x5(11, 2, 'A',
                    themeCol(jetzt / 20 + 40, 255));

        drawChar3x5(19, 2, 'U',
                    themeCol(jetzt / 20 + 80, 255));

        // kleines Schiff

        setPixel(16, 11, themeCol(jetzt / 20 + 120, 255));
        setPixel(15, 12, themeCol(jetzt / 20 + 120, 255));
        setPixel(16, 12, themeCol(jetzt / 20 + 120, 255));
        setPixel(17, 12, themeCol(jetzt / 20 + 120, 255));
        setPixel(14, 13, themeCol(jetzt / 20 + 180, 180));
        setPixel(15, 13, themeCol(jetzt / 20 + 180, 180));
        setPixel(16, 13, themeCol(jetzt / 20 + 180, 180));
        setPixel(17, 13, themeCol(jetzt / 20 + 180, 180));
        setPixel(18, 13, themeCol(jetzt / 20 + 180, 180));

        break;
    }
    }

    FastLED.show();
    unlockDisplay();
}

// ==========================================
// 4. TASK MANAGEMENT
// ==========================================
TaskHandle_t getTaskHandle(int nummer)
{
    switch (nummer)
    {
    case 0:
        return handleUhr;
    case 1:
        return handleWetter;
    case 2:
        return handleSnake;
    case 3:
        return handleMusik;
    case 4:
        return handleAnim;
    case 5:
        return handleDHT;
    case 6:
        return handleRaumschiff;
    default:
        return NULL;
    }
}

void setEventSperre(unsigned long dauerMs)
{
    eventSperreBis = millis() + dauerMs;
}
bool eventSperreAktiv()
{
    return (long)(millis() - eventSperreBis) < 0;
}

void wechsleZuTask(int zielTask)
{
    if (zielTask < 0 || zielTask > 6)
        return;

    int vorherigerTask = aktiverTask;
    aktiverTask = -2;

    if (vorherigerTask == 3)
        stopMusic();

    if (lockDisplay(100))
    {
        FastLED.clear(true);
        unlockDisplay();
    }

    aktiverTask = zielTask;
    navigationsSperre = false;
    joystick2.reset();

    if (zielTask == 3)
        startMusic();

    setEventSperre(250);
    Serial.print("Task ");
    Serial.print(zielTask);
    Serial.println(" aktiv");
}

void zurueckZumMenue()
{
    int vorherigerTask = aktiverTask;
    aktiverTask = -2;

    if (vorherigerTask == 3)
        stopMusic();

    fokusModus = 0;
    navigationsSperre = false;

    if (lockDisplay(100))
    {
        FastLED.clear(true);
        unlockDisplay();
    }

    joystick2.reset();
    vTaskDelay(30 / portTICK_PERIOD_MS);

    aktiverTask = -1;
    printMenu();
    setEventSperre(250);
}

struct ClickEvent
{
    int einfacherKlick;
    int doppelklick;
    int langKlick;
};

struct ClickEvent readAndClearClicks()
{
    struct ClickEvent result = {0, 0, 0};
    if (clickCounterMutex != NULL)
    {
        if (xSemaphoreTake(clickCounterMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
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
void taskMusik(void *pv)
{
    for (;;)
    {
        if (aktiverTask != 3)
        {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }
        if (!lockDisplay(20))
        {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }
        if (aktiverTask != 3)
        {
            unlockDisplay();
            continue;
        }

        FastLED.clear();
        unsigned long jetzt = millis();

        for (int bar = 0; bar < 8; bar++)
        {
            int bx = 1 + bar * 4;
            float phase = jetzt / 130.0 + bar * 0.8;
            int h = 2 + (int)(fabs(sin(phase)) * 9) + (musicBeat % 4);
            if (h > 15)
                h = 15;
            for (int y = 15; y > 15 - h; y--)
            {
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

void taskWifiManager(void *pv)
{
    bool timeConfigured = false;
    bool mdnsStarted = false;

    for (;;)
    {
        maintainWifiConnection();

        if (WiFi.status() == WL_CONNECTED)
        {
            if (!timeConfigured)
            {
                configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
                timeConfigured = true;
                Serial.println("WLAN verbunden, NTP gestartet: " + WiFi.localIP().toString());
            }

            if (!mdnsStarted)
            {
                String host = deviceHostname();
                if (MDNS.begin(host.c_str()))
                {
                    MDNS.addService("http", "tcp", 80);
                    mdnsStarted = true;
                    Serial.println("mDNS aktiv: http://" + host + ".local");
                }
                else
                {
                    Serial.println("mDNS Start fehlgeschlagen.");
                }
            }
        }
        else
        {
            timeConfigured = false;
            if (mdnsStarted)
            {
                MDNS.end();
                mdnsStarted = false;
            }
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void taskI2CJoystickHandler(void *pv)
{
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(15);

    for (;;)
    {
        uint8_t received = Wire.requestFrom((uint8_t)J3_I2C_ADDRESS, (uint8_t)5);

        if (received >= 5 && Wire.available() >= 5)
        {
            int rawX = (Wire.read() << 8) | Wire.read();
            int rawY = (Wire.read() << 8) | Wire.read();
            bool pressed = (Wire.read() == 1);

            joystick3.setI2CData(rawX, rawY, pressed);
            joystick3.update();
        }
        else
        {
            while (Wire.available() > 0)
                Wire.read();
            joystick3.setI2CData(512, 512, false);
            joystick3.update();
        }

        vTaskDelayUntil(&lastWake, interval);
    }
}

// ==========================================
// 6. SETUP
// ==========================================
void setup()
{
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(500));

    clickCounterMutex = xSemaphoreCreateMutex();
    displayMutex = xSemaphoreCreateMutex();

    initLittleFS();
    loadWifiCredentials();
    loadDeviceSettings();
    loadConfigForUser("");
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("I2C-Bus aktiv auf GPIO 32 (SDA) und GPIO 33 (SCL).");
    if (!hasWifiCredentials())
        startCaptivePortal();
    if (hasWifiCredentials())
        beginWifiConnection();

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
    xTaskCreatePinnedToCore(taskI2CJoystickHandler, "I2CJoy", 2048, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(taskWebServerHandler, "Web", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(taskWifiManager, "WiFi", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(taskUhr, "Uhr", 4096, NULL, 1, &handleUhr, 1);
    xTaskCreatePinnedToCore(taskWetter, "Wetter", 4096, NULL, 1, &handleWetter, 1);
    xTaskCreatePinnedToCore(taskSnakeHandler, "Snake", 4096, NULL, 1, &handleSnake, 1);
    xTaskCreatePinnedToCore(taskMusik, "Musik", 2560, NULL, 1, &handleMusik, 1);
    xTaskCreatePinnedToCore(taskAnim, "Anim", 4096, NULL, 1, &handleAnim, 1);
    xTaskCreatePinnedToCore(taskDHTHandler, "DHT", 4096, NULL, 1, &handleDHT, 1); // <--- NEUER DHT TASK START

    xTaskCreatePinnedToCore(taskWetterFetch, "WetterNet", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(
        taskRaumschiffHandler,
        "Raumschiff",
        4096,
        NULL,
        1,
        &handleRaumschiff,
        1);
    joystick2.setInverted(true, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    joystick1.kalibrieren();
    joystick2.kalibrieren();
    joystick3.kalibrieren();
    printMenu();
}

// ==========================================
// 7. LOOP
// ==========================================
void loop()
{
    joystick1.klickenErkennen();
    joystick2.klickenErkennen();

    if (eventSperreAktiv())
    {
        vTaskDelay(5 / portTICK_PERIOD_MS);
        return;
    }

    struct ClickEvent clicks = readAndClearClicks();

    if (aktiverTask == -1)
    {
        // IM MENÜ
        if (clicks.einfacherKlick > 0 || clicks.langKlick > 0)
        {
            if (fokusModus > 0)
            {
                playSound(SND_SELECT);
                wechsleZuTask(fokusModus - 1);
                return;
            }
        }

        int xPerc = joystick2.readXPercent();
        if (!navigationsSperre)
        {
            if (xPerc <= -80)
            {
                fokusModus = (fokusModus >= 7) ? 1 : fokusModus + 1; // Erweitert auf 6
                navigationsSperre = true;
                playSound(SND_SWIPE);
            }
            else if (xPerc >= 80)
            {
                fokusModus = (fokusModus <= 1) ? 7 : fokusModus - 1; // Erweitert auf 6
                navigationsSperre = true;
                playSound(SND_SWIPE);
            }
        }
        else if (abs(xPerc) < 60)
        {
            navigationsSperre = false;
        }
        printMenu();
    }
    else
    {
        // IN EINER APP
        if (clicks.doppelklick > 0)
        {
            playSound(SND_DIE);
            zurueckZumMenue();
        }
        else if (clicks.langKlick > 0)
        {
            playSound(SND_SELECT);
            int next = (aktiverTask + 1) % 7; // Erweitert auf 6
            fokusModus = next + 1;
            wechsleZuTask(next);
        }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
