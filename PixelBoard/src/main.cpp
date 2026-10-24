#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include "time.h"
#include "HardwareUtils.h"
#include "SoundUtils.h"
#include "RuntimeHealth.h"
#include "AppNavigation.h"

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
#include "TaskDHT.h"
#include "RaumschiffGame.h"
#include "TaskRaumschiff.h"
#include "TaskPong.h"
#include "TaskPacman.h"
#include "TaskTetris.h"
#include "TaskMario.h"
#include "TaskStandby.h"
#include "MenuMusic.h"
#include <LittleFS.h>

// ==========================================
// 1. HARDWARE & KONFIGURATION
// ==========================================
#define LED_PIN_OBEN 26
#define LED_PIN_UNTEN 25
#define COLOR_ORDER GRB
#define CHIPSET WS2812B
static const int APP_COUNT = 12;
static const SoundType APP_ENTRY_SOUNDS[APP_COUNT] = {
    SND_CHECKPOINT, // Uhr
    SND_BUFF,       // Wetter
    SND_EAT,        // Snake
    SND_UI,         // Musik
    SND_POWERUP,    // Animationen
    SND_SELECT,     // DHT
    SND_BOSS,       // Raumschiff
    SND_HIT,        // Pong
    SND_COIN,       // Pacman
    SND_EXPLOSION,  // Tetris
    SND_JUMP,       // Mario
    SND_CHECKPOINT  // Standby
};

// --- Joystick 1 (Für Snake) ---
#define J1_PIN_X 34
#define J1_PIN_Y 35
#define J1_PIN_TASTER 13

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

SemaphoreHandle_t displayMutex = NULL; // MUTEX FÜR DAS DISPLAY

volatile int aktiverTask = -1; // -1 = Menü, 0..APP_COUNT-1 = Apps
int fokusModus = 0; // Navigation im Menü
bool navigationsSperre = false;
unsigned long eventSperreBis = 0;
static portMUX_TYPE navigationRequestMux = portMUX_INITIALIZER_UNLOCKED;
static volatile int requestedAppTask = -1;

static const uint8_t MENU_VOLUME_STEP = 5;
static const uint8_t MENU_VOLUME_DEADZONE = 70;
static const unsigned long MENU_VOLUME_REPEAT_MS = 150;
static const unsigned long MENU_VOLUME_OVERLAY_MS = 2200;
static uint8_t lastAudibleMusicVolume = 100;
static uint8_t lastAudibleEffectsVolume = 100;
static unsigned long volumeOverlayUntilMs = 0;
static unsigned long lastVolumeStepMs[2] = {0, 0};
static bool volumeStepArmed[2] = {true, true};

static void showVolumeOverlay()
{
    volumeOverlayUntilMs = millis() + MENU_VOLUME_OVERLAY_MS;
}

static bool volumeOverlayVisible(unsigned long now)
{
    return (long)(volumeOverlayUntilMs - now) > 0;
}

static void drawMenuVolumeBar(int y, uint8_t volume, CRGB activeColor)
{
    CRGB trackColor = activeColor;
    trackColor.nscale8_video(22);
    for (int x = 0; x < 32; x++)
        setPixel(x, y, trackColor);

    int filled = (volume * 32 + 99) / 100;
    for (int x = 0; x < filled; x++)
        setPixel(x, y, activeColor);

    if (volume == 0)
    {
        setPixel(0, y, CRGB::Red);
        setPixel(1, y, CRGB::Red);
    }
}

static void drawMenuVolumeOverlay(unsigned long now)
{
    if (!volumeOverlayVisible(now))
        return;

    // Zwei getrennte, horizontale Balken: oben Musik, darunter Spieleffekte.
    drawMenuVolumeBar(0, getMusicVolume(), CRGB(30, 190, 255));
    drawMenuVolumeBar(2, getEffectsVolume(), CRGB(255, 150, 20));
}

static void toggleMenuChannelMute(bool music)
{
    uint8_t current = music ? getMusicVolume() : getEffectsVolume();
    uint8_t &lastAudible = music ? lastAudibleMusicVolume : lastAudibleEffectsVolume;
    if (current > 0)
    {
        lastAudible = current;
        if (music)
            setMusicVolume(0);
        else
            setEffectsVolume(0);
    }
    else
    {
        uint8_t restored = lastAudible > 0 ? lastAudible : 100;
        if (music)
            setMusicVolume(restored);
        else
            setEffectsVolume(restored);
    }
    showVolumeOverlay();
}

static void adjustMenuChannelVolume(Joystick &stick, bool music, uint8_t slot)
{
    // Die Joysticks sind am Board um 90 Grad montiert: X entspricht hoch/runter.
    int vertical = stick.readXPercent();
    int direction = vertical >= MENU_VOLUME_DEADZONE ? 1 :
                    vertical <= -MENU_VOLUME_DEADZONE ? -1 : 0;
    unsigned long now = millis();
    if (direction == 0)
    {
        volumeStepArmed[slot] = true;
        return;
    }
    if (!volumeStepArmed[slot] && now - lastVolumeStepMs[slot] < MENU_VOLUME_REPEAT_MS)
        return;

    volumeStepArmed[slot] = false;
    lastVolumeStepMs[slot] = now;
    uint8_t current = music ? getMusicVolume() : getEffectsVolume();
    uint8_t next = (uint8_t)constrain((int)current + direction * MENU_VOLUME_STEP, 0, 100);
    if (next == current)
    {
        showVolumeOverlay();
        return;
    }

    if (music)
    {
        setMusicVolume(next);
        if (next > 0)
            lastAudibleMusicVolume = next;
    }
    else
    {
        setEffectsVolume(next);
        if (next > 0)
            lastAudibleEffectsVolume = next;
    }
    showVolumeOverlay();
}

static void handleMenuVolumeControls()
{
    // Controller 1 regelt Musik, der I2C-Controller nur Spieleffekte.
    adjustMenuChannelVolume(joystick1, true, 0);
    adjustMenuChannelVolume(joystick3, false, 1);

    if (joystick1.einfacherKlickZaehler > 0)
        toggleMenuChannelMute(true);
    if (joystick3.einfacherKlickZaehler > 0)
        toggleMenuChannelMute(false);

    joystick1.einfacherKlickZaehler = 0;
    joystick1.doppelklickZaehler = 0;
    joystick1.langKlickZaehler = 0;
    joystick3.einfacherKlickZaehler = 0;
    joystick3.doppelklickZaehler = 0;
    joystick3.langKlickZaehler = 0;
}

// ==========================================
// 3. MENÜ-ICONS & DISPLAY-FUNKTIONEN
// ==========================================
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
        int center = 16 + (int)(sin(jetzt / 520.0) * 2.0);
        for (int i = 0; i < APP_COUNT; i++)
        {
            int x = center + (i - 4) * 4;
            int y = 8 + (int)(sin(jetzt / 280.0 + i * 0.8) * 3.0);
            if (x < 1 || x > 30 || y < 1 || y > 14)
                continue;
            CRGB col = themeCol(jetzt / 18 + i * 24, i == fokusModus ? 255 : 150);
            setPixel(x, y, col);
            setPixel(x - 1, y, col);
            setPixel(x + 1, y, col);
            if (i == 6 || i == 9)
                setPixel(x, y - 1, col);
            if (i == 7 || i == 8)
                setPixel(x, y + 1, col);
        }
        uint8_t pulse = 90 + sin8(jetzt / 6) / 2;
        for (int x = 2; x < 30; x++)
            if (((x + jetzt / 90) % 5) == 0)
                setPixel(x, 14, themeCol(jetzt / 25 + x * 6, pulse));

        drawMenuVolumeOverlay(jetzt);
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
        int fill = 4 + (int)(sin(jetzt / 420.0) * 3.0);
        for (int y = 3; y <= 11; y++) setPixel(13, y, CRGB(45, 70, 110));
        for (int y = 11; y >= 11 - fill; y--) setPixel(13, y, CRGB(255, 80, 50));
        for (int dx = -2; dx <= 2; dx++)
            for (int dy = -2; dy <= 2; dy++)
                if (dx * dx + dy * dy <= 4) setPixel(13 + dx, 12 + dy, CRGB(255, 80, 50));
        int dropY = 5 + (jetzt / 180) % 5;
        setPixel(23, dropY, CRGB(60, 170, 255));
        setPixel(22, dropY + 1, CRGB(60, 170, 255));
        setPixel(23, dropY + 1, CRGB(120, 210, 255));
        break;
    }

    case 7:
    {
        raumschiffRenderMenuPreview();
        break;
    }

    case 8:
    {
        renderPongMenuPreview();
        break;
    }

    case 9:
    {
        renderPacmanMenuPreview();
        break;
    }

    case 10:
    {
        renderTetrisMenuPreview();
        break;
    }

    case 11:
    {
        renderMarioMenuPreview();
        break;
    }

    case 12:
    {
        renderStandbyMenuPreview();
        break;
    }
    }

    drawMenuVolumeOverlay(jetzt);
    FastLED.show();
    unlockDisplay();
}

// ==========================================
// 4. TASK MANAGEMENT
// ==========================================
void setEventSperre(unsigned long dauerMs)
{
    eventSperreBis = millis() + dauerMs;
}
bool eventSperreAktiv()
{
    return (long)(millis() - eventSperreBis) < 0;
}

bool requestAppTaskSwitch(int taskNumber)
{
    if (taskNumber < 0 || taskNumber >= APP_COUNT)
        return false;
    portENTER_CRITICAL(&navigationRequestMux);
    requestedAppTask = taskNumber;
    portEXIT_CRITICAL(&navigationRequestMux);
    return true;
}

static int takeRequestedAppTask()
{
    portENTER_CRITICAL(&navigationRequestMux);
    int taskNumber = requestedAppTask;
    requestedAppTask = -1;
    portEXIT_CRITICAL(&navigationRequestMux);
    return taskNumber;
}

void wechsleZuTask(int zielTask)
{
    if (zielTask < 0 || zielTask >= APP_COUNT)
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
    joystick3.reset();

    playSound(APP_ENTRY_SOUNDS[zielTask]);

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
    joystick3.reset();
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
    result.einfacherKlick = joystick2.einfacherKlickZaehler;
    result.doppelklick = joystick2.doppelklickZaehler;
    result.langKlick = joystick2.langKlickZaehler;
    joystick2.einfacherKlickZaehler = 0;
    joystick2.doppelklickZaehler = 0;
    joystick2.langKlickZaehler = 0;
    return result;
}

struct ClickEvent readAppClicks()
{
    return readAndClearClicks();
}

// ==========================================
// 5. MUSIK TASK (Ehemals taskDataHandler)
// ==========================================
void taskMusik(void *pv)
{
    registerCurrentTaskWithWatchdog();
    for (;;)
    {
        feedRuntimeWatchdog();
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
    registerCurrentTaskWithWatchdog();
    bool timeConfigured = false;
    bool mdnsStarted = false;

    for (;;)
    {
        feedRuntimeWatchdog();
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
    registerCurrentTaskWithWatchdog();
    bool connected = false;
    bool offlineLogged = false;
    uint8_t validPackets = 0;

    for (;;)
    {
        uint8_t received = Wire.requestFrom((uint8_t)J3_I2C_ADDRESS, (uint8_t)5);

        if (received >= 5 && Wire.available() >= 5)
        {
            int rawX = (Wire.read() << 8) | Wire.read();
            int rawY = (Wire.read() << 8) | Wire.read();
            bool pressed = (Wire.read() == 1);

            if (validPackets < 2)
                validPackets++;
            if (!connected && validPackets >= 2)
            {
                connected = true;
                offlineLogged = false;
                joystick3.reset();
                Serial.println("I2C-Joystick verbunden.");
            }
            joystick3.setI2CData(rawX, rawY, pressed);
            if (connected)
                joystick3.update();
        }
        else
        {
            while (Wire.available() > 0)
                Wire.read();
            validPackets = 0;
            if (connected || !offlineLogged)
            {
                connected = false;
                offlineLogged = true;
                joystick3.setI2CData(512, 512, false);
                joystick3.reset();
                Serial.println("I2C-Joystick nicht verbunden; langsame Wiederverbindung aktiv.");
            }
        }

        feedRuntimeWatchdog();
        vTaskDelay(connected ? pdMS_TO_TICKS(15) : pdMS_TO_TICKS(1500));
    }
}

static bool createTaskChecked(TaskFunction_t function, const char *name,
                              uint32_t stackSize, UBaseType_t priority,
                              BaseType_t core)
{
    BaseType_t result = xTaskCreatePinnedToCore(
        function, name, stackSize, NULL, priority, NULL, core);
    if (result == pdPASS)
        return true;
    Serial.print("Task konnte nicht erstellt werden: ");
    Serial.println(name);
    return false;
}

// ==========================================
// 6. SETUP
// ==========================================
void setup()
{
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(500));

    displayMutex = xSemaphoreCreateMutex();
    if (displayMutex == NULL)
    {
        Serial.println("FATAL: Display-Mutex konnte nicht erstellt werden.");
        for (;;)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }

    initRuntimeWatchdog();
    registerCurrentTaskWithWatchdog();

    initLittleFS();
    loadWifiCredentials();
    loadDeviceSettings();
    loadDeviceProfile();
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("I2C-Bus aktiv auf GPIO 32 (SDA) und GPIO 33 (SCL).");
    if (!hasWifiCredentials())
        startCaptivePortal();
    if (hasWifiCredentials())
        beginWifiConnection();

    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    initLedColorCorrection();
    FastLED.setCorrection(TypicalLEDStrip);
    FastLED.setDither(true);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 2400);
    FastLED.setBrightness(255);

    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 1, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 1, 0);

    initAudio();
    setupWebServer();

    createTaskChecked(taskI2CJoystickHandler, "I2CJoy", 2048, 4, 1);
    createTaskChecked(taskWebServerHandler, "Web", 4096, 1, 0);
    createTaskChecked(taskMenuMusic, "MenuMP3", 10240, 2, 0);
    createTaskChecked(taskWifiManager, "WiFi", 4096, 1, 0);
    createTaskChecked(taskUhr, "Uhr", 4096, 1, 1);
    createTaskChecked(taskWetter, "Wetter", 4096, 1, 1);
    createTaskChecked(taskSnakeHandler, "Snake", 6144, 1, 1);
    createTaskChecked(taskMusik, "Musik", 2560, 1, 1);
    createTaskChecked(taskAnim, "Anim", 4096, 1, 1);
    createTaskChecked(taskDHTHandler, "DHT", 4096, 1, 1);
    createTaskChecked(taskWetterFetch, "WetterNet", 8192, 1, 0);
    createTaskChecked(taskRaumschiffHandler, "Raumschiff", 4096, 1, 1);
    createTaskChecked(taskPongHandler, "Pong", 4096, 1, 1);
    createTaskChecked(taskPacmanHandler, "Pacman", 4096, 1, 1);
    createTaskChecked(taskTetrisHandler, "Tetris", 4096, 1, 1);
    createTaskChecked(taskMarioHandler, "Mario", 6144, 1, 1);
    createTaskChecked(taskStandbyHandler, "Standby", 4096, 1, 1);
    joystick2.setInverted(true, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    joystick1.kalibrieren();
    joystick2.kalibrieren();
    joystick3.kalibrieren();
    if (getMusicVolume() > 0)
        lastAudibleMusicVolume = getMusicVolume();
    if (getEffectsVolume() > 0)
        lastAudibleEffectsVolume = getEffectsVolume();
    printMenu();
}

// ==========================================
// 7. LOOP
// ==========================================
void loop()
{
    feedRuntimeWatchdog();
    joystick1.klickenErkennen();
    joystick2.klickenErkennen();

    int requestedTask = takeRequestedAppTask();
    if (requestedTask >= 0)
    {
        fokusModus = requestedTask + 1;
        wechsleZuTask(requestedTask);
        return;
    }

    if (eventSperreAktiv())
    {
        vTaskDelay(5 / portTICK_PERIOD_MS);
        return;
    }

    struct ClickEvent clicks = (aktiverTask == -1) ? readAndClearClicks() : readAppClicks();

    if (aktiverTask == -1)
    {
        // IM MENÜ
        handleMenuVolumeControls();
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
                fokusModus = (fokusModus >= APP_COUNT) ? 1 : fokusModus + 1;
                navigationsSperre = true;
                playSound(SND_SWIPE);
            }
            else if (xPerc >= 80)
            {
                fokusModus = (fokusModus <= 1) ? APP_COUNT : fokusModus - 1;
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
            int next = (aktiverTask + 1) % APP_COUNT;
            fokusModus = next + 1;
            wechsleZuTask(next);
        }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
