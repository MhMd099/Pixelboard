#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>
#include <DHT.h>
#include <ESP_Google_Sheet_Client.h>
#include "Joystick.h"

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
// 2. GOOGLE SHEETS & WLAN KONFIGURATION
// ==========================================
const char *ssid = "Nothin";
const char *password = "nothin099";

#define PROJECT_ID "dataloggingpb"
#define CLIENT_EMAIL "datalogging-pb@dataloggingpb.iam.gserviceaccount.com"
const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCj1HTpxeHGlSXK\nKU0rH93KLh8mDshrQAtDdMSMS7YEd7T5vU0ihlOI5T39H/QXai1VN+rdfJQ8VrMI\nO5VJUusU1vaiqdPkn2HSXIMjBwKZuUVnUCjVEiWdX+Z/zjyIV6685Z4h2D3GBzQ7\n9GFUB5E1wS+ZKd1K+4k758kHARGT1u+cVZ2+IEu7NLqb1UV4QR4y6zH3skgLP1GE\nnfxWMmx5elGvFBcZmMm4Ncx3m8JZNqzpbUeXI+i+fvebZoM4Wu3JjuXLiiyX5u68\nwi8I2Y/HDQF6w475ieRngKbJdHXRo1HGwcocysCvm47/4Vol62onr9PqlHwYm7E9\nWYgxVGRjAgMBAAECggEAM079kp1buWLKpAbNWT0wq/pH3RZyJEy5elXenIW1qq6G\n6lQkDTT+gngxMs5IFvE042SQ1O8ISeFpTqHCfmVOpIcyVP1VFFvqOgSpOVYftV81\n4kZTk2+Mgj4fpVVE1fqICjbrkHP13MgyzrgZp0R7cNdg/doDqVEfyLgt2Fi4VZFR\nogNYEEcg+Tu9LxQSfEWZF8dtdKAU7uzYLuc6BkVDuHnPvGDzQK76Dn65jUrVIhxA\nLwpjccSPiTow0LHAdRguI5FfgpskBlyGZrSGkpxs+y1phd4UhsziVDtOsxQd/L7s\nPrCyucJR5KzEDPIvO/XxmA6xg1A4Yf1Y7ag+aJIZYQKBgQDYATzGttJ8RJblBFIw\nMkoXti5jXXks3pbuJlJU92t5jpFbGi8fpLfhN4EYt+gFOiAqtOBPZsRkoe1QTcEL\nUmIdJO3sFeDnq2BQLAfXslB9+7Xd3lbmZhm6z66+3ksCGjxxSZYmZM43JfSY1X4N\n58thzbNOC0fevYJIn41xhR4p8QKBgQDCKhh6L+/wQwIvApk6NnxD+PITFrrzjIZf\nwSK+nOl5ID+oVrKOl2/vSC7LuJAfyvfMI6F0tqDIBwtvdwX6uax+BxskvYX2w68I\nl40jE37I4mwYyAVny3W8caVXmxpFpPS9hUdC2c/D1a1J4p4dx8ufqBLhEmC8nAwj\niwlKb7A/kwKBgCcR4jpXKy9LALgf1fXdwsUTMMTMTXSuNkKRL+cqcYglH2mJDOj+\nVDwqW/Fqok7/un2/BauW/QLuvwv9ZGN13UVEPryrIGkG+H7H2AtNt31yH+0noDRA\nV3sQwZzIfGy+7hvXoY8EQMB83wcd5pUBTio8mKgPJkrFoGEeaukTmOchAoGBAJyZ\nyygxpboIsags1l0HOO6xyLzwplRs0KxGX7mRYRValz00v8sWBSfe9i9FaqjZ0UaK\nrlwuODtcwzJhsybnvmHfZVsaqQPADFpHsYPK44UuabULDqEKjqkwmASyilwFkYeS\nCUm310TCAIQJDTJDxM2+h4uUgQVebsP0DchFkMeVAoGAKTpShbfT8GzqIQei501D\nF8fS+US/3WjqutNL136N/YMYnJzOF8w2vz9Ab7h+lADCrUjeKCpfGH51AKAFlUXD\nAiyqMiqgWvjxKo9LVyFcJqGp0P5RJSbr0OWq1+LkV3wofGAQWXsH7OFym2BULpJt\nnL39C2joy478eDdRLAyFd7A=\n-----END PRIVATE KEY-----\n";
const char spreadsheetId[] = "1UVF6XSF4KJbVI_b5s-QQKZMaB36BXMaAlNBZLHzkiVc";

// ==========================================
// 3. INSTANZEN & GLOBALE VARIABLEN
// ==========================================
Joystick joystick1(J1_PIN_X, J1_PIN_Y, J1_PIN_TASTER, INPUT_PULLDOWN); // Snake
Joystick joystick2(J2_PIN_X, J2_PIN_Y, J2_PIN_TASTER, INPUT_PULLUP);   // Menü
DHT dht(DHTPIN, DHTTYPE);

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;
cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;
cLEDText AnzeigeOben;
cLEDText AnzeigeUnten;

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

// ==========================================
// 4. LOW-LEVEL GRAFIK & HILFSFUNKTIONEN
// ==========================================
void setPixel(int x, int y, CRGB color) {
    if (y < 0 || y >= 16 || x < 0 || x >= 32) return;
    if (y < 8) { 
        int index = (x % 2 == 0) ? (x * 8 + y) : (x * 8 + (7 - y));
        ledsOben[0][index] = color;
    } else { 
        int vX = 31 - x;
        int vY = 7 - (y - 8);
        int index = (vX % 2 == 0) ? (vX * 8 + vY) : (vX * 8 + (7 - vY));
        ledsUnten[0][index] = color;
    }
}

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
    if (aktiverTask >= 0 && aktuellerHandle != NULL && aktuellerHandle != zielHandle) {
        vTaskSuspend(aktuellerHandle);
    }

    aktiverTask = zielTask;
    navigationsSperre = false;
    lastXPerc = 0;  
    lastYPerc = 0;
    
    joystick2.einfacherKlickZaehler = 0;
    joystick2.doppelklickZaehler = 0;
    joystick2.langKlickZaehler = 0;

    vTaskResume(zielHandle);
    setEventSperre(750);
    Serial.printf("Task %d aktiv\n", zielTask);
}

void zurueckZumMenue() {
    if (aktiverTask >= 0) {
        TaskHandle_t aktuellerHandle = getTaskHandle(aktiverTask);
        if (aktuellerHandle != NULL) {
            vTaskSuspend(aktuellerHandle);
        }
    }

    aktiverTask = -1;
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
void taskA(void * pv) {
    struct tm timeinfo;
    for(;;) {
        if (getLocalTime(&timeinfo)) {
            FastLED.clear();
            if (millis() % 6000 < 3000) { 
                sprintf(datumBuffer, "\x02%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                sprintf(zeitBuffer, "\x02   %02d", timeinfo.tm_sec);
            } else {
                sprintf(datumBuffer, "\x02 %04d", timeinfo.tm_year + 1900);
                sprintf(zeitBuffer, "\x02%02d.%02d.", timeinfo.tm_mday, timeinfo.tm_mon + 1);
            }
            AnzeigeOben.SetText((unsigned char *)datumBuffer, strlen(datumBuffer));
            AnzeigeUnten.SetText((unsigned char *)zeitBuffer, strlen(zeitBuffer));
            AnzeigeOben.UpdateText();
            AnzeigeUnten.UpdateText();
            FastLED.show();
        }
        vTaskDelay(500 / portTICK_PERIOD_MS); 
    }
}

void taskB(void * pv) { for(;;) { Serial.println("B..."); vTaskDelay(1000/portTICK_PERIOD_MS); } }
void taskE(void * pv) { for(;;) { Serial.println("E..."); vTaskDelay(1000/portTICK_PERIOD_MS); } }

// --- TASK C: SNAKE (KORRIGIERT) ---
void taskC(void * pvParameters) {
    Point snake[100]; int snakeLength = 3; Point dir = {1, 0}; Point food;
    int moveInterval = 150; unsigned long lastMoveTime = 0;
    auto spawnFood = [&]() { food.x = random(1, WIDTH - 1); food.y = random(1, HEIGHT_TOTAL - 1); };
    auto resetGame = [&]() { snakeLength = 3; snake[0] = {15, 8}; snake[1] = {14, 8}; snake[2] = {13, 8}; dir = {1, 0}; moveInterval = 150; spawnFood(); };
    resetGame();
    
    for(;;) {
        // HIER: Achsen-Mapping direkt beim Auslesen tauschen
        // User-Wunsch: Y(oben)=Rechts(+X), Y(unten)=Links(-X), X(rechts)=Oben(+Y), X(links)=Unten(-Y)
        int rawX = analogRead(PIN_X); 
        int rawY = analogRead(PIN_Y);
        
        // Logik: X-Achse der Schlange reagiert nun auf physikalisches Y
        // Y-Achse der Schlange reagiert nun auf physikalisches X
        
        // Logik: Vorzeichen für Snake-Richtung umgekehrt
        if (rawY > 2800 && dir.x == 0) { dir.x = 1; dir.y = 0; }      // War <400, jetzt >3700
        else if (rawY < 400 && dir.x == 0) { dir.x = -1; dir.y = 0; } // War >3700, jetzt <400
        else if (rawX < 400 && dir.y == 0) { dir.y = 1; dir.x = 0; }  // War >3700, jetzt <400
        else if (rawX > 2800 && dir.y == 0) { dir.y = -1; dir.x = 0; } // War <400, jetzt >3700

        if (millis() - lastMoveTime > moveInterval) {
            lastMoveTime = millis();
            int nextX = snake[0].x + dir.x; int nextY = snake[0].y + dir.y;
            bool dead = (nextX < 0 || nextX >= WIDTH || nextY < 0 || nextY >= HEIGHT_TOTAL);
            for (int i = 0; i < snakeLength; i++) if (nextX == snake[i].x && nextY == snake[i].y) dead = true;
            if (dead) { resetGame(); vTaskDelay(1000 / portTICK_PERIOD_MS); }
            else {
                if (nextX == food.x && nextY == food.y) { if (snakeLength < 100) snakeLength++; spawnFood(); if (moveInterval > 70) moveInterval -= 2; }
                for (int i = snakeLength - 1; i > 0; i--) snake[i] = snake[i - 1];
                snake[0] = {nextX, nextY};
            }
            FastLED.clear();
            for(int x=0; x<WIDTH; x++) { setPixel(x, 0, CRGB(2, 2, 10)); setPixel(x, 15, CRGB(2, 2, 10)); }
            setPixel(food.x, food.y, CRGB::Red);
            for (int i = 0; i < snakeLength; i++) setPixel(snake[i].x, snake[i].y, (i == 0) ? CRGB::White : CRGB::Green);
            FastLED.show();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// --- VERSCHMOLZENER TASK (DHT + Sheets) --- (Code 100% unverändert)
void taskDataHandler(void *pvParameters) {
    dht.begin();
    GSheet.setTokenCallback(tokenStatusCallback);
    GSheet.setPrerefreshSeconds(10 * 60);
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    unsigned long lastLogTime = 0;

    for (;;) {
        float h = dht.readHumidity();
        float t = dht.readTemperature();

        // 1. DISPLAY AKTUALISIEREN (Immer wenn Task aktiv ist)
        if (!isnan(h) && !isnan(t)) {
            snprintf(bufferOben, 64, "\x02 %.1f C", t); 
            snprintf(bufferUnten, 64, "\x02 %.0f %%", h);
        } else {
            snprintf(bufferOben, 64, "\x02 FEHLER");
            snprintf(bufferUnten, 64, "\x02 SENSOR");
        }

        FastLED.clear();
        AnzeigeOben.SetText((unsigned char *)bufferOben, strlen(bufferOben));
        AnzeigeUnten.SetText((unsigned char *)bufferUnten, strlen(bufferUnten));
        AnzeigeOben.UpdateText();
        AnzeigeUnten.UpdateText();
        FastLED.show();

        // 2. GOOGLE SHEETS LOGGING (Alle 60 Sek im Hintergrund)
        if (GSheet.ready() && (millis() - lastLogTime > 60000)) {
            lastLogTime = millis();
            FirebaseJson valueRange;
            valueRange.add("majorDimension", "COLUMNS");
            time_t now;
            time(&now);
            valueRange.set("values/[0]/[0]", (uint32_t)now);
            valueRange.set("values/[1]/[0]", t);
            valueRange.set("values/[2]/[0]", h);

            FirebaseJson response;
            GSheet.values.append(&response, spreadsheetId, "Sheet1!A1", &valueRange);
            Serial.println("Cloud-Log gesendet.");
        }

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    
    // WiFi & NTP
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
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
    xTaskCreate(taskA, "TaskA", 4096, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskB", 2048, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskC", 4096, NULL, 1, &handleC);          // Snake Task (Index 2)
    xTaskCreate(taskDataHandler, "TaskData", 8192, NULL, 1, &handleData); // DHT Task (Index 3)
    xTaskCreate(taskE, "TaskE", 2048, NULL, 1, &handleE);

    // Alle beim Boot suspendieren
    vTaskSuspend(handleA); vTaskSuspend(handleB); vTaskSuspend(handleC);
    vTaskSuspend(handleData); vTaskSuspend(handleE);
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