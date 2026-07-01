#include "Config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "HardwareUtils.h"

const char* weatherApiKey = "343df2364dc5541a3efd274bf2f845df";

String currentCity = "Innsbruck";
String currentUser = "";
bool forceWeatherUpdate = false;

String wifiSsid = "";
String wifiPassword = "";
int currentDhtPin = 21;
int currentDhtType = 22;

WebServer server(80);
DNSServer dnsServer;
bool captivePortalActive = false;
static const byte DNS_PORT = 53;
static const char* captiveSsid = "PixelBoard";
static unsigned long lastWifiBeginMs = 0;
static unsigned long firstWifiAttemptMs = 0;

void initLittleFS() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount fehlgeschlagen!");
        return;
    }
    Serial.println("LittleFS erfolgreich gemountet.");
}

static String htmlEscape(const String& value) {
    String out;
    out.reserve(value.length());
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c == '&') out += F("&amp;");
        else if (c == '<') out += F("&lt;");
        else if (c == '>') out += F("&gt;");
        else if (c == '"') out += F("&quot;");
        else if (c == '\'') out += F("&#39;");
        else out += c;
    }
    return out;
}

static bool validDhtPin(int pin) {
    if (pin < 0 || pin > 33) return false;
    if (pin >= 6 && pin <= 11) return false; // ESP32 flash pins
    return true;
}

static int normalizeDhtPin(int pin) {
    return validDhtPin(pin) ? pin : 21;
}

static int normalizeDhtType(int type) {
    return (type == 11) ? 11 : 22;
}

void loadWifiCredentials() {
    wifiSsid = "";
    wifiPassword = "";

    if (!LittleFS.exists("/wifi.json")) return;

    File file = LittleFS.open("/wifi.json", "r");
    if (!file) return;

    JsonDocument doc;
    if (!deserializeJson(doc, file)) {
        wifiSsid = doc["s"].is<const char*>() ? doc["s"].as<String>() : "";
        wifiPassword = doc["p"].is<const char*>() ? doc["p"].as<String>() : "";
        wifiSsid.trim();
    }
    file.close();

    if (wifiSsid != "") {
        Serial.println("WLAN-Daten geladen fuer SSID: " + wifiSsid);
    }
}

bool hasWifiCredentials() {
    return wifiSsid.length() > 0;
}

String configuredWifiSsid() {
    return wifiSsid;
}

void beginWifiConnection() {
    if (!hasWifiCredentials()) return;

    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false, false);
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    lastWifiBeginMs = millis();
    if (firstWifiAttemptMs == 0) firstWifiAttemptMs = lastWifiBeginMs;
    Serial.println("Verbinde mit WLAN: " + wifiSsid);
}

void maintainWifiConnection() {
    if (!hasWifiCredentials()) {
        startCaptivePortal();
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        stopCaptivePortal();
        return;
    }

    unsigned long now = millis();
    if (firstWifiAttemptMs != 0 && now - firstWifiAttemptMs > 20000UL) {
        startCaptivePortal();
    }
    if (lastWifiBeginMs == 0 || now - lastWifiBeginMs > 30000UL) {
        beginWifiConnection();
    }
}

void saveWifiCredentials(String newSsid, String newPassword) {
    newSsid.trim();

    if (newSsid == "") return;
    if (newPassword == "" && newSsid == wifiSsid) {
        newPassword = wifiPassword;
    }

    wifiSsid = newSsid;
    wifiPassword = newPassword;

    JsonDocument doc;
    doc["s"] = wifiSsid;
    doc["p"] = wifiPassword;

    File file = LittleFS.open("/wifi.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("WLAN-Daten gespeichert fuer SSID: " + wifiSsid);
    }

    beginWifiConnection();
}

void loadDeviceSettings() {
    currentDhtPin = 21;
    currentDhtType = 22;

    

    Serial.print("DHT geladen: GPIO ");
    Serial.print(currentDhtPin);
    Serial.print(", DHT");
    Serial.println(currentDhtType);
}



static JsonObject userObject(JsonDocument& doc, const String& user) {
    if (!doc[user].is<JsonObject>()) {
        String oldCity = doc[user].is<const char*>() ? doc[user].as<String>() : "";
        JsonObject obj = doc[user].to<JsonObject>();
        if (oldCity != "") obj["c"] = oldCity;
    }
    return doc[user].as<JsonObject>();
}

static int clockIndexFromLegacy(int idx);

static void removeLegacyUserKeys(JsonObject obj) {
    if (!obj["t"].is<const char*>() && obj["theme"].is<int>()) {
        int oldTheme = obj["theme"] | g_themeIndex;
        obj["t"] = String(themeCode(oldTheme));
    }
    if (!obj["u"].is<const char*>() && obj["clock"].is<int>()) {
        int oldClock = clockIndexFromLegacy(obj["clock"] | g_clockStyle);
        obj["u"] = String(clockCode(oldClock));
    }
    if (!obj["h"].is<int>() && obj["highscore"].is<int>()) {
        obj["h"] = obj["highscore"].as<int>();
    }
    obj.remove("city");
    obj.remove("theme");
    obj.remove("clock");
    obj.remove("highscore");
}

static int readHighScore(JsonVariant data) {
    if (!data.is<JsonObject>()) return 0;
    JsonObject obj = data.as<JsonObject>();
    return obj["h"] | (obj["highscore"] | 0);
}

static int clockIndexFromLegacy(int idx) {
    if (idx == 3) return 2;
    if (idx == 2) return 0;
    return idx;
}

void loadConfigForUser(String user) {
    user.trim();
    currentUser = user;

    String alteStadt = currentCity;
    int themeIdx = 0;
    int clockIdx = 0;

    if (user == "") {
        currentCity = "Innsbruck";
    } else {
        currentCity = "Innsbruck";
        if (LittleFS.exists("/config.json")) {
            File file = LittleFS.open("/config.json", "r");
            JsonDocument doc;
            if (!deserializeJson(doc, file)) {
                if (doc[user].is<JsonObject>()) {
                    JsonObject obj = doc[user].as<JsonObject>();
                    currentCity = obj["c"].is<const char*>() ? obj["c"].as<String>() :
                                  (obj["city"].is<const char*>() ? obj["city"].as<String>() : "Innsbruck");

                    if (obj["t"].is<const char*>()) {
                        const char* tCode = obj["t"];
                        themeIdx = themeFromCode(tCode[0]);
                    } else {
                        themeIdx = obj["theme"] | 0;
                    }

                    if (obj["u"].is<const char*>()) {
                        const char* uCode = obj["u"];
                        clockIdx = clockFromCode(uCode[0]);
                    } else {
                        clockIdx = clockIndexFromLegacy(obj["clock"] | 0);
                    }
                } else if (doc[user].is<const char*>()) {
                    currentCity = doc[user].as<String>();
                }
            }
            file.close();
        }
    }

    applyTheme(themeIdx);
    applyClockStyle(clockIdx);
    if (currentCity != alteStadt) forceWeatherUpdate = true;
    Serial.println("Config geladen: User=" + currentUser + ", Stadt=" + currentCity +
                   ", Design=" + String(themeName(g_themeIndex)) + ", Uhr=" + String(clockStyleName(g_clockStyle)));
}

void ensureUserExists(String user) {
    user.trim();
    if (user == "") return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    if (!doc[user].is<JsonObject>()) {
        String oldCity = doc[user].is<const char*>() ? doc[user].as<String>() : "";
        JsonObject obj = doc[user].to<JsonObject>();
        if (oldCity != "") obj["c"] = oldCity;

        File file = LittleFS.open("/config.json", "w");
        if (file) {
            serializeJson(doc, file);
            file.close();
        }
    }
}

void saveConfig(String user, String city) {
    user.trim();
    city.trim();

    if (currentCity != city) {
        forceWeatherUpdate = true;
    }
    currentCity = city;
    currentUser = user;

    if (user == "") return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    JsonObject obj = userObject(doc, user);
    obj["c"] = city;
    removeLegacyUserKeys(obj);

    File file = LittleFS.open("/config.json", "w");
    serializeJson(doc, file);
    file.close();

    Serial.println("Speichere fuer " + user + ": " + city);
}

void saveTheme(String user, int themeIdx) {
    user.trim();
    applyTheme(themeIdx);

    if (user == "") return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }
    JsonObject obj = userObject(doc, user);
    obj["t"] = String(themeCode(g_themeIndex));
    removeLegacyUserKeys(obj);

    File file = LittleFS.open("/config.json", "w");
    serializeJson(doc, file);
    file.close();

    Serial.println("Design gespeichert fuer " + user + ": " + String(themeName(g_themeIndex)));
}

void saveClockStyle(String user, int clockIdx) {
    user.trim();
    applyClockStyle(clockIdx);

    if (user == "") return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }
    JsonObject obj = userObject(doc, user);
    obj["u"] = String(clockCode(g_clockStyle));
    removeLegacyUserKeys(obj);

    File file = LittleFS.open("/config.json", "w");
    serializeJson(doc, file);
    file.close();

    Serial.println("Uhr-Stil gespeichert fuer " + user + ": " + String(clockStyleName(g_clockStyle)));
}

static void appendOption(String& html, int value, int current, const String& label) {
    html += "<option value='" + String(value) + "'";
    if (value == current) html += " selected";
    html += ">" + label + "</option>";
}

void handleRoot() {
    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    String savedSsid = configuredWifiSsid();

    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:Arial,sans-serif;background:#111827;color:#f8fafc;text-align:center;margin:0;padding:22px;}";
    html += "h1{margin:8px 0 4px;font-size:30px;}h2{margin:0 0 18px;color:#38bdf8;font-size:16px;font-weight:600;}";
    html += "input[type='text'],input[type='password'],input[type='number'],select{width:90%;max-width:320px;padding:12px;margin:8px 0;border-radius:8px;border:1px solid #334155;background:#0f172a;color:#fff;}";
    html += "input[type='submit']{background:#06b6d4;color:#fff;padding:11px 20px;border:none;border-radius:8px;font-weight:bold;cursor:pointer;}";
    html += ".card{background:#1f2937;padding:20px;border-radius:10px;display:inline-block;margin:10px;min-width:min(320px,90vw);box-shadow:0 10px 30px #0008;vertical-align:top;}";
    html += ".ok{color:#22c55e}.bad{color:#f87171}hr{border:0;border-top:1px solid #374151;margin:18px 0;}a{color:#f87171;text-decoration:none;font-weight:bold;}</style></head><body>";

    html += "<h1>PixelBoard</h1><h2>Captive Portal</h2>";

    html += "<div class='card'><h3>User</h3>";
    if (currentUser == "") html += "<p>Kein User aktiv.</p>";
    else html += "<p>Aktiv: <b>" + htmlEscape(currentUser) + "</b></p>";
    html += "<form action='/doLogin' method='POST'>";
    html += "<input type='text' name='username' placeholder='Username' value='" + htmlEscape(currentUser) + "' required><br>";
    html += "<input type='submit' value='Login / Registrieren'></form>";
    html += "<p><a href='/logout'>Logout</a></p></div>";

    html += "<div class='card'><h3>WLAN</h3>";
    if (wifiConnected) {
        html += "<p class='ok'>Verbunden mit <b>" + htmlEscape(WiFi.SSID()) + "</b></p>";
        html += "<p>IP: <b>" + WiFi.localIP().toString() + "</b></p>";
    } else if (hasWifiCredentials()) {
        html += "<p class='bad'>Nicht verbunden. Gespeichert: <b>" + htmlEscape(savedSsid) + "</b></p>";
    } else {
        html += "<p class='bad'>Noch kein WLAN gespeichert.</p>";
    }
    html += "<form action='/saveWifi' method='POST'>";
    html += "<input type='text' name='ssid' placeholder='WLAN SSID' value='" + htmlEscape(savedSsid) + "' required><br>";
    html += "<input type='password' name='password' placeholder='WLAN Passwort'><br>";
    html += "<input type='submit' value='WLAN speichern'></form></div>";

    html += "<div class='card'><h3>Einstellungen</h3>";
    html += "<p>Wetter-Stadt: <b>" + htmlEscape(currentCity) + "</b></p>";
    html += "<form action='/updateCity' method='POST'>";
    html += "<input type='text' name='city' placeholder='Stadt' value='" + htmlEscape(currentCity) + "' required><br>";
    html += "<input type='submit' value='Stadt speichern'></form>";

    html += "<hr><h4>Design</h4>";
    html += "<form action='/updateTheme' method='POST'><select name='theme'>";
    for (int i = 0; i < themeAnzahl(); i++) {
        appendOption(html, i, g_themeIndex, String(themeName(i)));
    }
    html += "</select><br><input type='submit' value='Design speichern'></form>";

    html += "<h4>Uhr-Stil</h4>";
    html += "<form action='/updateClock' method='POST'><select name='clock'>";
    for (int i = 0; i < clockStyleAnzahl(); i++) {
        appendOption(html, i, g_clockStyle, String(clockStyleName(i)));
    }
    html += "</select><br><input type='submit' value='Uhr speichern'></form>";

   

    html += "</div></body></html>";
    server.send(200, "text/html", html);
}

void handleDoLogin() {
    if (server.hasArg("username")) {
        String u = server.arg("username");
        u.trim();
        ensureUserExists(u);
        loadConfigForUser(u);
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleSaveWifi() {
    if (server.hasArg("ssid")) {
        String pwd = server.hasArg("password") ? server.arg("password") : "";
        saveWifiCredentials(server.arg("ssid"), pwd);
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateCity() {
    if (server.hasArg("city")) {
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
    loadConfigForUser("");
    server.sendHeader("Location", "/");
    server.send(303);
}

void startCaptivePortal() {
    if (captivePortalActive) return;

    WiFi.mode(WIFI_AP_STA);
    bool ok = WiFi.softAP(captiveSsid);
    IPAddress apIp = WiFi.softAPIP();
    if (ok) {
        dnsServer.start(DNS_PORT, "*", apIp);
        captivePortalActive = true;
        Serial.println("Captive Portal aktiv: http://" + apIp.toString() + " / SSID: " + String(captiveSsid));
    } else {
        Serial.println("Captive Portal konnte nicht gestartet werden.");
    }
}

void stopCaptivePortal() {
    if (!captivePortalActive) return;

    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    captivePortalActive = false;
    Serial.println("Captive Portal beendet, Access Point ist aus.");
}

static void redirectToPortal() {
    String url = captivePortalActive ? ("http://" + WiFi.softAPIP().toString() + "/") : "/";
    server.sendHeader("Location", url, true);
    server.send(302, "text/plain", "");
}

static void handleCaptiveProbe() {
    redirectToPortal();
}

static String contentTypeForPath(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static bool serveLittleFsFile(String path) {
    if (path.endsWith("/")) path += "index.html";

    String gzPath = path + ".gz";
    bool gz = LittleFS.exists(gzPath);
    String filePath = gz ? gzPath : path;
    if (!LittleFS.exists(filePath)) return false;

    File file = LittleFS.open(filePath, "r");
    if (!file) return false;

    if (gz) server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.streamFile(file, contentTypeForPath(path));
    file.close();
    return true;
}

static void handleNotFound() {
    if (serveLittleFsFile(server.uri())) return;

    if (captivePortalActive) {
        redirectToPortal();
    } else {
        server.send(404, "text/plain", "Nicht gefunden");
    }
}

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/doLogin", HTTP_POST, handleDoLogin);
    server.on("/saveWifi", HTTP_POST, handleSaveWifi);
    server.on("/updateCity", HTTP_POST, handleUpdateCity);
    server.on("/updateTheme", HTTP_POST, handleUpdateTheme);
    server.on("/updateClock", HTTP_POST, handleUpdateClock);
    server.on("/logout", HTTP_GET, handleLogout);
    server.on("/generate_204", HTTP_GET, handleCaptiveProbe);
    server.on("/gen_204", HTTP_GET, handleCaptiveProbe);
    server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
    server.on("/library/test/success.html", HTTP_GET, handleCaptiveProbe);
    server.on("/connecttest.txt", HTTP_GET, handleCaptiveProbe);
    server.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);
    server.on("/fwlink", HTTP_GET, handleCaptiveProbe);
    server.on("/redirect", HTTP_GET, handleCaptiveProbe);
    server.on("/canonical.html", HTTP_GET, handleCaptiveProbe);
    server.onNotFound(handleNotFound);
    server.begin();
}

void taskWebServerHandler(void * pvParameters) {
    for (;;) {
        if (captivePortalActive) dnsServer.processNextRequest();
        server.handleClient();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void saveHighScore(String user, int score) {
    user.trim();
    if (user == "") return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    JsonObject obj = userObject(doc, user);
    int currentHigh = readHighScore(doc[user]);
    if (score > currentHigh) {
        obj["h"] = score;
        removeLegacyUserKeys(obj);

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

    struct Player { String name; int score; };
    Player scores[10];
    int count = 0;

    for (JsonPair kv : doc.as<JsonObject>()) {
        if (count < 10) {
            scores[count].name = kv.key().c_str();
            scores[count].score = readHighScore(kv.value());
            count++;
        }
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (scores[j].score < scores[j + 1].score) {
                Player temp = scores[j];
                scores[j] = scores[j + 1];
                scores[j + 1] = temp;
            }
        }
    }

    Serial.println("--- TOP 3 HIGHSCORES ---");
    for (int i = 0; i < min(count, 3); i++) {
        Serial.print(i + 1);
        Serial.print(". ");
        Serial.print(scores[i].name);
        Serial.print(": ");
        Serial.print(scores[i].score);
        Serial.println(" Punkte");
    }
    Serial.println("------------------------");
}

int getHighScore(String user) {
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        JsonDocument doc;
        deserializeJson(doc, file);
        file.close();
        return readHighScore(doc[user]);
    }
    return 0;
}

void getTopScores(PlayerData* list, int& count) {
    JsonDocument doc;
    File file = LittleFS.open("/config.json", "r");
    if (!file) {
        count = 0;
        return;
    }
    deserializeJson(doc, file);
    file.close();

    count = 0;
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (count < 10) {
            list[count].name = kv.key().c_str();
            list[count].score = readHighScore(kv.value());
            count++;
        }
    }

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
