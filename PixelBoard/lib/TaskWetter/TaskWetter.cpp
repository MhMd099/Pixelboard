#include "TaskWetter.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "HardwareUtils.h"
#include "Config.h"
#include "SoundUtils.h" // für Donner-Geräusch

extern volatile int aktiverTask;

int weatherID = 800; 
float aktuelleTemp = 0.0;

// (Dein bestehendes font3x5 Array bleibt hier genau so, wie es war!)
extern const char* font3x5[26] ;
extern void drawChar3x5(int startX, int startY, char c, CRGB color);
extern void drawDigitW(int x, int y, int n, CRGB c);
// --- Wetterdaten holen: kurzer Timeout, blockiert nur den Hintergrund-Task ---
static String urlEncode(const String& value) {
    const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(value.length() * 3);
    for (size_t i = 0; i < value.length(); i++) {
        uint8_t c = (uint8_t)value.charAt(i);
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else if (c == ' ') {
            out += "%20";
        } else {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

static String weatherCityQuery() {
    String city = currentCity;
    city.trim();
    if (city == "") city = "Innsbruck";
    if (city.indexOf(',') < 0) city += ",AT";
    return urlEncode(city);
}

static bool fetchJson(const String& path, JsonDocument& doc, int& httpCode) {
    WiFiClient client;
    client.setTimeout(7000);
    httpCode = 0;

    if (!client.connect("api.openweathermap.org", 80)) {
        Serial.println("[WETTER] Verbindung fehlgeschlagen");
        return false;
    }

    client.print(F("GET "));
    client.print(path);
    client.print(F(" HTTP/1.1\r\nHost: api.openweathermap.org\r\nConnection: close\r\nUser-Agent: PixelBoard\r\n\r\n"));

    unsigned long deadline = millis() + 7000UL;
    bool headersDone = false;
    while (millis() < deadline) {
        if (!client.available()) {
            if (!client.connected()) break;
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/")) {
            int firstSpace = line.indexOf(' ');
            if (firstSpace > 0) httpCode = line.substring(firstSpace + 1).toInt();
        }
        if (line.length() == 0) {
            headersDone = true;
            break;
        }
    }

    if (!headersDone) {
        Serial.println("[WETTER] Keine HTTP-Header empfangen");
        client.stop();
        return false;
    }

    DeserializationError err = deserializeJson(doc, client);
    client.stop();
    if (err) {
        Serial.print("[WETTER] JSON-Fehler: ");
        Serial.println(err.c_str());
        return false;
    }
    return httpCode >= 200 && httpCode < 300;
}

static bool fetchCoordinates(float& lat, float& lon) {
    JsonDocument geo;
    int httpCode = 0;
    String path = "/geo/1.0/direct?q=" + weatherCityQuery() +
                  "&limit=1&appid=" + String(weatherApiKey);
    if (!fetchJson(path, geo, httpCode)) {
        Serial.print("[WETTER] Geo Fehler HTTP ");
        Serial.println(httpCode);
        if (httpCode == 401) Serial.println("[WETTER] API-Key pruefen.");
        return false;
    }

    if (!geo[0].is<JsonObject>()) {
        Serial.println("[WETTER] Ort nicht gefunden: " + currentCity);
        return false;
    }

    lat = geo[0]["lat"] | 0.0f;
    lon = geo[0]["lon"] | 0.0f;
    Serial.print("[WETTER] Ort gefunden: ");
    Serial.print(geo[0]["name"].as<const char*>());
    Serial.print(" lat=");
    Serial.print(lat, 4);
    Serial.print(" lon=");
    Serial.println(lon, 4);
    return true;
}

static bool fetchWetter() {
    if (WiFi.status() != WL_CONNECTED) return false;

    float lat = 0.0f;
    float lon = 0.0f;
    if (!fetchCoordinates(lat, lon))
        return false;

    int httpCode = 0;
    JsonDocument doc;
    String path = "/data/2.5/weather?lat=" + String(lat, 6) +
                  "&lon=" + String(lon, 6) +
                  "&appid=" + String(weatherApiKey) + "&units=metric";
    if (!fetchJson(path, doc, httpCode)) {
        Serial.print("[WETTER] Fehler! HTTP Code: ");
        Serial.println(httpCode);
        if (httpCode == 401) Serial.println("[WETTER] API-Key pruefen.");
        return false;
    }

    aktuelleTemp = doc["main"]["temp"] | aktuelleTemp;
    weatherID = doc["weather"][0]["id"] | weatherID;
    Serial.print("[WETTER] OK! Stadt: ");
    Serial.print(currentCity);
    Serial.print(" Temp: ");
    Serial.print(aktuelleTemp, 1);
    Serial.print(" ID: ");
    Serial.println(weatherID);
    return true;
}

// Hintergrund-Task: holt die Daten, ohne je die Anzeige zu blockieren.
void taskWetterFetch(void * pvParameters) {
    vTaskDelay(2500 / portTICK_PERIOD_MS); // kurz warten, bis WLAN/NTP stabil sind
    for (;;) {
        bool ok = fetchWetter();
        forceWeatherUpdate = false;
        unsigned long warte = ok ? 900000UL : 30000UL; // Erfolg: 15 min, sonst in 30 s erneut
        unsigned long start = millis();
        while (millis() - start < warte) {
            if (forceWeatherUpdate) break; // Stadt geaendert -> sofort neu laden
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
}

void taskWetter(void * pvParameters) {
    // --- Wetter-Symbol-Helfer (zeichnen im linken Bereich x:1..11) ---
    auto drawWolke = [](int ox, int oy, CRGB col) {
        for (int x = ox; x <= ox + 9; x++) setPixel(x, oy + 3, col);
        for (int x = ox + 1; x <= ox + 3; x++) for (int y = oy + 1; y <= oy + 3; y++) setPixel(x, y, col);
        for (int x = ox + 4; x <= ox + 6; x++) for (int y = oy;     y <= oy + 3; y++) setPixel(x, y, col);
        for (int x = ox + 7; x <= ox + 9; x++) for (int y = oy + 1; y <= oy + 3; y++) setPixel(x, y, col);
    };
    auto drawSonne = [](unsigned long jt) {
        int cx = 6, cy = 7;
        for (int x = cx - 2; x <= cx + 2; x++)
            for (int y = cy - 2; y <= cy + 2; y++)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= 5) setPixel(x, y, CRGB::Yellow);
        int r = 4 + (int)(fabs(sin(jt / 350.0)) * 1.5);
        setPixel(cx, cy - r, CRGB::Orange); setPixel(cx, cy + r, CRGB::Orange);
        setPixel(cx - r, cy, CRGB::Orange); setPixel(cx + r, cy, CRGB::Orange);
        setPixel(cx - r + 1, cy - r + 1, CRGB::Gold); setPixel(cx + r - 1, cy - r + 1, CRGB::Gold);
        setPixel(cx - r + 1, cy + r - 1, CRGB::Gold); setPixel(cx + r - 1, cy + r - 1, CRGB::Gold);
    };
    auto drawNebel = [](unsigned long jt) {
        for (int row = 0; row < 4; row++) {
            int yy = 3 + row * 3;
            int off = (int)(sin(jt / 300.0 + row) * 2.0);
            for (int x = 1; x <= 11; x++) setPixel(x + off, yy, CRGB(70, 70, 70));
        }
    };

    for(;;) {
        if (aktiverTask != 1) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }
        if (!lockDisplay(20)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }
        if (aktiverTask != 1) {
            unlockDisplay();
            continue;
        }

        // Anzeige rendert nur noch aus den (im Hintergrund aktualisierten) Werten -> friert nie ein
        FastLED.clear();
        unsigned long jetzt = millis();
        CRGB textCol = themeCol(jetzt / 40, 255);
        CRGB valueCol = themeCol(jetzt / 40 + 96, 235);
        
        // --- DYNAMISCHER STADTNAME ---
        String abbr = currentCity.substring(0, 3);
        abbr.toUpperCase();
        
        CRGB txtCol = textCol;
        if (abbr.length() > 0) drawChar3x5(16, 1, abbr.charAt(0), txtCol);
        if (abbr.length() > 1) drawChar3x5(20, 1, abbr.charAt(1), txtCol);
        if (abbr.length() > 2) drawChar3x5(24, 1, abbr.charAt(2), txtCol);

        // --- TEMPERATUR ---
        int t = abs((int)aktuelleTemp);
        drawDigitW(16, 10, t / 10, valueCol);
        drawDigitW(20, 10, t % 10, valueCol);
        
        setPixel(24, 10, valueCol); // Grad-Symbol
        for(int y=10; y<15; y++) setPixel(26, y, valueCol); 
        for(int x=27; x<30; x++) { setPixel(x, 10, valueCol); setPixel(x, 14, valueCol); } 
        setPixel(30, 11, valueCol); setPixel(30, 13, valueCol); 

        // --- WETTER-SYMBOL je nach Bedingung (animiert) ---
        int cat = weatherID / 100;

        if (weatherID == 800) {
            // Klar: Sonne mit pulsierenden Strahlen
            drawSonne(jetzt);
        } else if (cat == 2) {
            // Gewitter: dunkle Wolke + Blitz + Donner
            drawWolke(1, 1, CRGB(80, 80, 90));
            bool flash = ((jetzt / 350) % 4 == 0);
            int bolt[6][2] = {{6, 6}, {5, 8}, {6, 9}, {4, 11}, {5, 12}, {4, 14}};
            CRGB boltCol = flash ? CRGB::White : CRGB::Yellow;
            for (int i = 0; i < 6; i++) setPixel(bolt[i][0], bolt[i][1], boltCol);
            if (flash) drawWolke(1, 1, CRGB(150, 150, 160)); // Wolke hellt beim Blitz auf
            static unsigned long lastThunder = 0;
            if (flash && (jetzt - lastThunder > 2500)) { playSound(SND_DIE); lastThunder = jetzt; }
        } else if (cat == 3 || cat == 5) {
            // Niesel/Regen: Wolke + fallende blaue Tropfen
            drawWolke(1, 1, CRGB::Gray);
            for (int d = 0; d < 6; d++) {
                int dx = 2 + d * 2;
                int dy = 6 + ((jetzt / 90 + d * 3) % 9);
                setPixel(dx, dy, CRGB::DeepSkyBlue);
            }
        } else if (cat == 6) {
            // Schnee: Wolke + langsam taumelnde Flocken
            drawWolke(1, 1, CRGB::Gray);
            for (int d = 0; d < 6; d++) {
                int dx = 2 + d * 2 + (int)(sin(jetzt / 200.0 + d) * 1.0);
                int dy = 6 + ((jetzt / 160 + d * 2) % 9);
                setPixel(dx, dy, CRGB::White);
            }
        } else if (cat == 7) {
            // Nebel/Dunst: treibende graue Schwaden
            drawNebel(jetzt);
        } else {
            // Wolken (80x) und Rest: ruhige weiße Wolke
            drawWolke(1, 5, CRGB::White);
        }

        FastLED.show();
        unlockDisplay();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
