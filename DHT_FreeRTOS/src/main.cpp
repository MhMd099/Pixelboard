#include <Arduino.h>
#include <WiFi.h>
#include <ESP_Google_Sheet_Client.h>
#include "time.h"
#include <FastLED.h>
#include <LEDMatrix.h>
#include <LEDText.h>
#include <FontMatrise.h>
#include <DHT.h>
#include "Joystick.h"

// --- Hardware & Pins ---
#define LED_PIN_OBEN   26
#define LED_PIN_UNTEN  25
#define DHTPIN         21
#define DHTTYPE        DHT22
#define PIN_X          34
#define PIN_Y          35
#define PIN_BTN        32
// --- Diese Definitionen fehlen in deinem Code ---
#define CHIPSET        WS2812B
#define COLOR_ORDER    GRB
#define MATRIX_WIDTH   32
#define MATRIX_HEIGHT  8
#define MATRIX_TYPE    VERTICAL_ZIGZAG_MATRIX

DHT dht(DHTPIN, DHTTYPE);
Joystick meinJoystick(PIN_X, PIN_Y, PIN_BTN);

// --- Google Daten ---
#define PROJECT_ID "dataloggingpb"
#define CLIENT_EMAIL "datalogging-pb@dataloggingpb.iam.gserviceaccount.com"
const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCj1HTpxeHGlSXK\nKU0rH93KLh8mDshrQAtDdMSMS7YEd7T5vU0ihlOI5T39H/QXai1VN+rdfJQ8VrMI\nO5VJUusU1vaiqdPkn2HSXIMjBwKZuUVnUCjVEiWdX+Z/zjyIV6685Z4h2D3GBzQ7\n9GFUB5E1wS+ZKd1K+4k758kHARGT1u+cVZ2+IEu7NLqb1UV4QR4y6zH3skgLP1GE\nnfxWMmx5elGvFBcZmMm4Ncx3m8JZNqzpbUeXI+i+fvebZoM4Wu3JjuXLiiyX5u68\nwi8I2Y/HDQF6w475ieRngKbJdHXRo1HGwcocysCvm47/4Vol62onr9PqlHwYm7E9\nWYgxVGRjAgMBAAECggEAM079kp1buWLKpAbNWT0wq/pH3RZyJEy5elXenIW1qq6G\n6lQkDTT+gngxMs5IFvE042SQ1O8ISeFpTqHCfmVOpIcyVP1VFFvqOgSpOVYftV81\n4kZTk2+Mgj4fpVVE1fqICjbrkHP13MgyzrgZp0R7cNdg/doDqVEfyLgt2Fi4VZFR\nogNYEEcg+Tu9LxQSfEWZF8dtdKAU7uzYLuc6BkVDuHnPvGDzQK76Dn65jUrVIhxA\nLwpjccSPiTow0LHAdRguI5FfgpskBlyGZrSGkpxs+y1phd4UhsziVDtOsxQd/L7s\nPrCyucJR5KzEDPIvO/XxmA6xg1A4Yf1Y7ag+aJIZYQKBgQDYATzGttJ8RJblBFIw\nMkoXti5jXXks3pbuJlJU92t5jpFbGi8fpLfhN4EYt+gFOiAqtOBPZsRkoe1QTcEL\nUmIdJO3sFeDnq2BQLAfXslB9+7Xd3lbmZhm6z66+3ksCGjxxSZYmZM43JfSY1X4N\n58thzbNOC0fevYJIn41xhR4p8QKBgQDCKhh6L+/wQwIvApk6NnxD+PITFrrzjIZf\nwSK+nOl5ID+oVrKOl2/vSC7LuJAfyvfMI6F0tqDIBwtvdwX6uax+BxskvYX2w68I\nl40jE37I4mwYyAVny3W8caVXmxpFpPS9hUdC2c/D1a1J4p4dx8ufqBLhEmC8nAwj\niwlKb7A/kwKBgCcR4jpXKy9LALgf1fXdwsUTMMTMTXSuNkKRL+cqcYglH2mJDOj+\nVDwqW/Fqok7/un2/BauW/QLuvwv9ZGN13UVEPryrIGkG+H7H2AtNt31yH+0noDRA\nV3sQwZzIfGy+7hvXoY8EQMB83wcd5pUBTio8mKgPJkrFoGEeaukTmOchAoGBAJyZ\nyygxpboIsags1l0HOO6xyLzwplRs0KxGX7mRYRValz00v8sWBSfe9i9FaqjZ0UaK\nrlwuODtcwzJhsybnvmHfZVsaqQPADFpHsYPK44UuabULDqEKjqkwmASyilwFkYeS\nCUm310TCAIQJDTJDxM2+h4uUgQVebsP0DchFkMeVAoGAKTpShbfT8GzqIQei501D\nF8fS+US/3WjqutNL136N/YMYnJzOF8w2vz9Ab7h+lADCrUjeKCpfGH51AKAFlUXD\nAiyqMiqgWvjxKo9LVyFcJqGp0P5RJSbr0OWq1+LkV3wofGAQWXsH7OFym2BULpJt\nnL39C2joy478eDdRLAyFd7A=\n-----END PRIVATE KEY-----\n";
const char spreadsheetId[] = "1UVF6XSF4KJbVI_b5s-QQKZMaB36BXMaAlNBZLHzkiVc";

// --- WLAN ---
const char *ssid = "Nothin";
const char *password = "nothin099";

// --- Instanzen & Variablen ---
TaskHandle_t handleA, handleB, handleC, handleData;
cLEDMatrix<32, -8, VERTICAL_ZIGZAG_MATRIX> ledsOben;
cLEDMatrix<-32, 8, VERTICAL_ZIGZAG_MATRIX> ledsUnten;
cLEDText AnzeigeOben, AnzeigeUnten;
char bufferOben[64], bufferUnten[64];
int aktuellerModus = 0;

void tokenStatusCallback(TokenInfo info) {
    if (info.status == token_status_error) Serial.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
}

// --- VERSCHMOLZENER TASK (DHT + Sheets) ---
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

// Platzhalter
void taskA(void *p) { for(;;) { Serial.println("A"); vTaskDelay(1000/portTICK_PERIOD_MS); } }
void taskB(void *p) { for(;;) { Serial.println("B"); vTaskDelay(1000/portTICK_PERIOD_MS); } }
void taskC(void *p) { for(;;) { Serial.println("C"); vTaskDelay(1000/portTICK_PERIOD_MS); } }

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }

    // ZEIT-FIX: Warten bis NTP synchron ist
    configTime(3600, 3600, "pool.ntp.org");
    Serial.print("Warte auf Zeit-Sync...");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) { delay(500); Serial.print("."); }
    Serial.println(" Zeit synchronisiert!");
    
    // WICHTIG für Google Library
    time_t now;
    time(&now);
    GSheet.setSystemTime(now); 

    FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());
    FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());
    FastLED.setBrightness(15);

    AnzeigeOben.SetFont(MatriseFontData);
    AnzeigeOben.Init(&ledsOben, 32, 9, 0, 0);
    AnzeigeUnten.SetFont(MatriseFontData);
    AnzeigeUnten.Init(&ledsUnten, 32, 9, 0, 0);

    // Tasks erstellen (Task Data bekommt Modus 3)
    xTaskCreate(taskA, "TaskA", 4096, NULL, 1, &handleA);
    xTaskCreate(taskB, "TaskB", 4096, NULL, 1, &handleB);
    xTaskCreate(taskC, "TaskC", 4096, NULL, 1, &handleC);
    xTaskCreate(taskDataHandler, "TaskData", 8192, NULL, 1, &handleData);

    // Start-Zustand
    vTaskSuspend(handleB);
    vTaskSuspend(handleC);
    vTaskSuspend(handleData);
}

void loop() {
    meinJoystick.klickenErkennen();
    static unsigned long drueckStartZeit = 0;
    static bool wurdeUmschaltungAusgeloest = false;

    if (meinJoystick.isPressed()) {
        if (drueckStartZeit == 0) drueckStartZeit = millis();
        if (!wurdeUmschaltungAusgeloest && (millis() - drueckStartZeit >= 1000)) {
            aktuellerModus = (aktuellerModus + 1) % 4; // 4 Modi: A, B, C, Data
            wurdeUmschaltungAusgeloest = true;

            // Erst alles stoppen
            vTaskSuspend(handleA); vTaskSuspend(handleB); vTaskSuspend(handleC); vTaskSuspend(handleData);

            // Dann den richtigen starten
            if (aktuellerModus == 0) vTaskResume(handleA);
            else if (aktuellerModus == 1) vTaskResume(handleB);
            else if (aktuellerModus == 2) vTaskResume(handleC);
            else if (aktuellerModus == 3) vTaskResume(handleData);
            
            Serial.printf("Modus gewechselt zu: %d\n", aktuellerModus);
            FastLED.clear();
            FastLED.show();
        }
    } else {
        drueckStartZeit = 0;
        wurdeUmschaltungAusgeloest = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}