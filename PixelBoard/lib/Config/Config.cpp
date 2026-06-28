#include "Config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "HardwareUtils.h" // für applyTheme / Theme-Status

const char* ssid = "Nothin";
const char* password = "nothin099";
const char* weatherApiKey = "343df2364dc5541a3efd274bf2f845df"; 

const char* spreadsheetId = "1UVF6XSF4KJbVI_b5s-QQKZMaB36BXMaAlNBZLHzkiVc";
const char* PROJECT_ID = "dataloggingpb";
const char* CLIENT_EMAIL = "datalogging-pb@dataloggingpb.iam.gserviceaccount.com";

// TIPP: Nutze R"EOF(...)EOF" für den Key, das verhindert den RSA Parsing Error!
const char* PRIVATE_KEY = R"EOF(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCj1HTpxeHGlSXK
KU0rH93KLh8mDshrQAtDdMSMS7YEd7T5vU0ihlOI5T39H/QXai1VN+rdfJQ8VrMI
O5VJUusU1vaiqdPkn2HSXIMjBwKZuUVnUCjVEiWdX+Z/zjyIV6685Z4h2D3GBzQ7
9GFUB5E1wS+ZKd1K+4k758kHARGT1u+cVZ2+IEu7NLqb1UV4QR4y6zH3skgLP1GE
nfxWMmx5elGvFBcZmMm4Ncx3m8JZNqzpbUeXI+i+fvebZoM4Wu3JjuXLiiyX5u68
wi8I2Y/HDQF6w475ieRngKbJdHXRo1HGwcocysCvm47/4Vol62onr9PqlHwYm7E9
WYgxVGRjAgMBAAECggEAM079kp1buWLKpAbNWT0wq/pH3RZyJEy5elXenIW1qq6G
6lQkDTT+gngxMs5IFvE042SQ1O8ISeFpTqHCfmVOpIcyVP1VFFvqOgSpOVYftV81
4kZTk2+Mgj4fpVVE1fqICjbrkHP13MgyzrgZp0R7cNdg/doDqVEfyLgt2Fi4VZFR
ogNYEEcg+Tu9LxQSfEWZF8dtdKAU7uzYLuc6BkVDuHnPvGDzQK76Dn65jUrVIhxA
LwpjccSPiTow0LHAdRguI5FfgpskBlyGZrSGkpxs+y1phd4UhsziVDtOsxQd/L7s
PrCyucJR5KzEDPIvO/XxmA6xg1A4Yf1Y7ag+aJIZYQKBgQDYATzGttJ8RJblBFIw
MkoXti5jXXks3pbuJlJU92t5jpFbGi8fpLfhN4EYt+gFOiAqtOBPZsRkoe1QTcEL
UmIdJO3sFeDnq2BQLAfXslB9+7Xd3lbmZhm6z66+3ksCGjxxSZYmZM43JfSY1X4N
58thzbNOC0fevYJIn41xhR4p8QKBgQDCKhh6L+/wQwIvApk6NnxD+PITFrrzjIZf
wSK+nOl5ID+oVrKOl2/vSC7LuJAfyvfMI6F0tqDIBwtvdwX6uax+BxskvYX2w68I
l40jE37I4mwYyAVny3W8caVXmxpFpPS9hUdC2c/D1a1J4p4dx8ufqBLhEmC8nAwj
iwlKb7A/kwKBgCcR4jpXKy9LALgf1fXdwsUTMMTMTXSuNkKRL+cqcYglH2mJDOj+
VDwqW/Fqok7/un2/BauW/QLuvwv9ZGN13UVEPryrIGkG+H7H2AtNt31yH+0noDRA
V3sQwZzIfGy+7hvXoY8EQMB83wcd5pUBTio8mKgPJkrFoGEeaukTmOchAoGBAJyZ
yygxpboIsags1l0HOO6xyLzwplRs0KxGX7mRYRValz00v8sWBSfe9i9FaqjZ0UaK
rlwuODtcwzJhsybnvmHfZVsaqQPADFpHsYPK44UuabULDqEKjqkwmASyilwFkYeS
CUm310TCAIQJDTJDxM2+h4uUgQVebsP0DchFkMeVAoGAKTpShbfT8GzqIQei501D
F8fS+US/3WjqutNL136N/YMYnJzOF8w2vz9Ab7h+lADCrUjeKCpfGH51AKAFlUXD
AiyqMiqgWvjxKo9LVyFcJqGp0P5RJSbr0OWq1+LkV3wofGAQWXsH7OFym2BULpJt
nL39C2joy478eDdRLAyFd7A=
-----END PRIVATE KEY-----)EOF";
String currentCity = "Innsbruck"; 
String currentUser = "";
bool forceWeatherUpdate = false; // NEU: Startet auf false

WebServer server(80);

void initLittleFS() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount fehlgeschlagen!");
        return;
    }
    Serial.println("LittleFS erfolgreich gemountet.");
}

void loadConfigForUser(String user) {
    user.trim();
    currentUser = user;

    String alteStadt = currentCity;
    int themeIdx = 0; // Default-Design (Regenbogen)
    int clockIdx = 0; // Default-Uhr (Digital)

    if (user == "") {
        // Nicht eingeloggt -> immer Default-Stadt & Default-Design/-Uhr
        currentCity = "Innsbruck";
    } else {
        currentCity = "Innsbruck"; // Fallback, falls der User noch keine Stadt hat
        if (LittleFS.exists("/config.json")) {
            File file = LittleFS.open("/config.json", "r");
            JsonDocument doc;
            if (!deserializeJson(doc, file)) {
                if (doc[user].is<JsonObject>()) {
                    currentCity = doc[user]["city"] | "Innsbruck";
                    themeIdx = doc[user]["theme"] | 0;
                    clockIdx = doc[user]["clock"] | 0;
                } else if (doc[user].is<const char*>()) {
                    currentCity = doc[user].as<String>(); // Altes Format (nur Stadt als String)
                }
            }
            file.close();
        }
    }

    applyTheme(themeIdx);      // Design des Users (oder Default) aktivieren
    applyClockStyle(clockIdx); // Uhr-Stil des Users (oder Default) aktivieren
    if (currentCity != alteStadt) forceWeatherUpdate = true;
    Serial.println("Config geladen: User=" + currentUser + ", Stadt=" + currentCity +
                   ", Design=" + String(themeName(g_themeIndex)) + ", Uhr=" + String(clockStyleName(g_clockStyle)));
}

void saveConfig(String user, String city) {
    user.trim();
    city.trim();

    // Stadt im Speicher aktualisieren
    if (currentCity != city) {
        forceWeatherUpdate = true;
    }
    currentCity = city;
    currentUser = user;

    // Für "nicht eingeloggt" nichts dauerhaft speichern
    if (user == "") return;

    // JSON einlesen, aktualisieren und speichern
    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    // Eintrag in ein Objekt umwandeln, falls noch altes Format (nur String)
    if (!doc[user].is<JsonObject>()) {
        doc[user].to<JsonObject>();
    }
    // Stadt in eigenem Feld -> Highscore bleibt erhalten
    doc[user]["city"] = city;

    File file = LittleFS.open("/config.json", "w");
    serializeJson(doc, file);
    file.close();

    Serial.println("Speichere für " + user + ": " + city);
}

void saveTheme(String user, int themeIdx) {
    user.trim();
    applyTheme(themeIdx); // sofort live anwenden

    if (user == "") return; // nicht eingeloggt -> nichts dauerhaft speichern

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }
    if (!doc[user].is<JsonObject>()) {
        doc[user].to<JsonObject>();
    }
    doc[user]["theme"] = g_themeIndex;

    File file = LittleFS.open("/config.json", "w");
    serializeJson(doc, file);
    file.close();

    Serial.println("Design gespeichert für " + user + ": " + String(themeName(g_themeIndex)));
}

void saveClockStyle(String user, int clockIdx) {
    user.trim();
    applyClockStyle(clockIdx); // sofort live anwenden

    if (user == "") return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }
    if (!doc[user].is<JsonObject>()) {
        doc[user].to<JsonObject>();
    }
    doc[user]["clock"] = g_clockStyle;

    File file = LittleFS.open("/config.json", "w");
    serializeJson(doc, file);
    file.close();

    Serial.println("Uhr-Stil gespeichert für " + user + ": " + String(clockStyleName(g_clockStyle)));
}
// --- HTML SEITEN ---
void handleRoot() {
    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:Arial,sans-serif; background:#222; color:#fff; text-align:center; padding:20px;}";
    html += "input[type='text'], input[type='password']{width:90%; max-width:300px; padding:10px; margin:10px 0; border-radius:5px; border:none;}";
    html += "input[type='submit']{background:#00bcd4; color:#fff; padding:10px 20px; border:none; border-radius:5px; font-weight:bold; cursor:pointer;}";
    html += ".card{background:#333; padding:20px; border-radius:10px; display:inline-block; margin-top:20px;}</style></head><body>";
    
    html += "<h2>Led-Matrix System</h2>";

    if (currentUser == "") {
        html += "<div class='card'><h3>Bitte einloggen</h3>";
        html += "<form action='/doLogin' method='POST'>";
        html += "<input type='text' name='username' placeholder='Benutzername' required><br>";
        html += "<input type='password' name='password' placeholder='Passwort' required><br>";
        html += "<input type='submit' value='Einloggen'></form></div>";
    } else {
        html += "<div class='card'><h3 style='color:#00ff00;'>Hallo, " + currentUser + "!</h3>";
        html += "<hr style='border-color:#555;'>";
        html += "<p>Aktuelle Wetter-Stadt: <b>" + currentCity + "</b></p>";
        html += "<h4>Stadt ändern:</h4>";
        html += "<form action='/updateCity' method='POST'>";
        html += "<input type='text' name='city' placeholder='Neue Stadt (z.B. Prag)' required><br>";
        html += "<input type='submit' value='Stadt speichern'></form>";

        html += "<hr style='border-color:#555;'>";
        html += "<h4>Design wählen:</h4>";
        html += "<form action='/updateTheme' method='POST'>";
        html += "<select name='theme' style='padding:10px;border-radius:5px;'>";
        for (int i = 0; i < themeAnzahl(); i++) {
            html += "<option value='" + String(i) + "'";
            if (i == g_themeIndex) html += " selected";
            html += ">" + String(themeName(i)) + "</option>";
        }
        html += "</select><br><input type='submit' value='Design speichern'></form>";

        html += "<h4>Uhr-Stil:</h4>";
        html += "<form action='/updateClock' method='POST'>";
        html += "<select name='clock' style='padding:10px;border-radius:5px;'>";
        for (int i = 0; i < clockStyleAnzahl(); i++) {
            html += "<option value='" + String(i) + "'";
            if (i == g_clockStyle) html += " selected";
            html += ">" + String(clockStyleName(i)) + "</option>";
        }
        html += "</select><br><input type='submit' value='Uhr speichern'></form>";

        html += "<br><a href='/logout' style='color:#ff5555; text-decoration:none; font-weight:bold;'>Ausloggen</a></div>";
    }
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleDoLogin() {
    if (server.hasArg("username")) {
        String u = server.arg("username");
        // Lade die Daten für genau diesen User
        loadConfigForUser(u); 
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateCity() {
    if (server.hasArg("city")) {
        // Speichere die neue Stadt explizit für den aktuellen User
        saveConfig(currentUser, server.arg("city")); 
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateTheme() {
    if (server.hasArg("theme")) {
        saveTheme(currentUser, server.arg("theme").toInt());
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateClock() {
    if (server.hasArg("clock")) {
        saveClockStyle(currentUser, server.arg("clock").toInt());
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleLogout() {
    loadConfigForUser(""); // User abmelden -> Default-Stadt Innsbruck
    server.sendHeader("Location", "/");
    server.send(303);
}

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/doLogin", HTTP_POST, handleDoLogin);
    server.on("/updateCity", HTTP_POST, handleUpdateCity);
    server.on("/updateTheme", HTTP_POST, handleUpdateTheme);
    server.on("/updateClock", HTTP_POST, handleUpdateClock);
    server.on("/logout", HTTP_GET, handleLogout);
    server.begin();
}

void taskWebServerHandler(void * pvParameters) {
    for(;;) {
        server.handleClient();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
void saveHighScore(String user, int score) {
    JsonDocument doc;
    // 1. Bestehende Datei einlesen
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    // 2. Eintrag in ein Objekt umwandeln, falls nötig (alte Stadt als String erhalten)
    if (!doc[user].is<JsonObject>()) {
        String alteStadt = doc[user].is<const char*>() ? doc[user].as<String>() : "";
        doc[user].to<JsonObject>();
        if (alteStadt != "") doc[user]["city"] = alteStadt;
    }

    // 3. Nur speichern, wenn der neue Score höher ist!
    int currentHigh = doc[user]["highscore"] | 0; // | 0 ist der Standardwert, falls noch kein Score existiert
    if (score > currentHigh) {
        doc[user]["highscore"] = score;
        
        // Datei speichern
        File file = LittleFS.open("/config.json", "w");
        serializeJson(doc, file);
        file.close();
        Serial.println("Neuer Highscore gespeichert: " + String(score));
    } else {
        Serial.println("Score war nicht hoch genug. Aktueller Highscore: " + String(currentHigh));
    }
}

void printTopThree() {
    JsonDocument doc;
    File file = LittleFS.open("/config.json", "r");
    if (!file) return;
    deserializeJson(doc, file);
    file.close();

    // Wir sammeln alle User und ihre Scores
    struct Player { String name; int score; };
    Player scores[10]; // Platz für 10 User
    int count = 0;

    for (JsonPair kv : doc.as<JsonObject>()) {
        if (count < 10) {
            scores[count].name = kv.key().c_str();
            scores[count].score = kv.value()["highscore"] | 0;
            count++;
        }
    }

    // Ganz einfacher Sortier-Algorithmus (Bubble Sort)
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (scores[j].score < scores[j + 1].score) {
                Player temp = scores[j];
                scores[j] = scores[j + 1];
                scores[j + 1] = temp;
            }
        }
    }

    // Ausgabe
    Serial.println("--- TOP 3 HIGHSCORES ---");
    for (int i = 0; i < min(count, 3); i++) {
        Serial.printf("%d. %s: %d Punkte\n", i + 1, scores[i].name.c_str(), scores[i].score);
    }
    Serial.println("------------------------");
}

int getHighScore(String user) {
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        JsonDocument doc;
        deserializeJson(doc, file);
        file.close();
        return doc[user]["highscore"] | 0;
    }
    return 0; // Standard, wenn noch keiner existiert
}
void getTopScores(PlayerData* list, int& count) {
    JsonDocument doc;
    File file = LittleFS.open("/config.json", "r");
    if (!file) return;
    deserializeJson(doc, file);
    file.close();

    count = 0;
    // Extrahiere alle User
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (count < 10) {
            list[count].name = kv.key().c_str();
            list[count].score = kv.value()["highscore"] | 0;
            count++;
        }
    }
    // Einfacher Bubble-Sort nach Score
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (list[j].score < list[j + 1].score) {
                PlayerData temp = list[j];
                list[j] = list[j + 1];
                list[j + 1] = temp;
            }
        }
    }
}