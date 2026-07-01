#include "Config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "HardwareUtils.h"
#include "RaumschiffGame.h"

const char* weatherApiKey = "343df2364dc5541a3efd274bf2f845df";

String currentCity = "Innsbruck";
String currentUser = "";
String pongLeftUser = "";
String pongRightUser = "";
String pacmanP1User = "";
String pacmanP2User = "";
bool pacmanGhostManual = false;
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
static const char* deviceConfigPath = "/device.json";
static const char* deviceHostNameValue = "pixelboard";
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

String deviceHostname() {
    return String(deviceHostNameValue);
}

String deviceLocalUrl() {
    return String("http://") + deviceHostname() + ".local";
}

void beginWifiConnection() {
    if (!hasWifiCredentials()) return;

    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(deviceHostNameValue);
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
    } else {
        Serial.println("WLAN-Daten konnten nicht gespeichert werden.");
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

static int readPongHighScore(JsonVariant data) {
    if (!data.is<JsonObject>()) return 0;
    JsonObject obj = data.as<JsonObject>();
    return obj["pong"] | 0;
}

static int readPacmanHighScore(JsonVariant data) {
    if (!data.is<JsonObject>()) return 0;
    JsonObject obj = data.as<JsonObject>();
    return obj["pac"] | 0;
}

static String loadDefaultCity() {
    String city = "Innsbruck";
    if (!LittleFS.exists(deviceConfigPath)) return city;

    File file = LittleFS.open(deviceConfigPath, "r");
    if (!file) return city;

    JsonDocument doc;
    if (!deserializeJson(doc, file) && doc["city"].is<const char*>()) {
        city = doc["city"].as<String>();
        city.trim();
        if (city == "") city = "Innsbruck";
    }
    file.close();
    return city;
}

static void saveDefaultCity(const String& city) {
    JsonDocument doc;
    doc["city"] = city;

    File file = LittleFS.open(deviceConfigPath, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("Default-Stadt gespeichert: " + city);
    } else {
        Serial.println("Default-Stadt konnte nicht gespeichert werden.");
    }
}

static bool isScoreEntry(JsonPair kv) {
    String key = kv.key().c_str();
    if (key == "" || key[0] == '_') return false;
    if (!kv.value().is<JsonObject>()) return false;
    return readHighScore(kv.value()) > 0;
}

static bool isPongScoreEntry(JsonPair kv) {
    String key = kv.key().c_str();
    if (key == "" || key[0] == '_') return false;
    if (!kv.value().is<JsonObject>()) return false;
    return readPongHighScore(kv.value()) > 0;
}

static bool isPacmanScoreEntry(JsonPair kv) {
    String key = kv.key().c_str();
    if (key == "" || key[0] == '_') return false;
    if (!kv.value().is<JsonObject>()) return false;
    return readPacmanHighScore(kv.value()) > 0;
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
    String defaultCity = loadDefaultCity();

    if (user == "") {
        currentCity = defaultCity;
    } else {
        currentCity = defaultCity;
        if (LittleFS.exists("/config.json")) {
            File file = LittleFS.open("/config.json", "r");
            JsonDocument doc;
            if (!deserializeJson(doc, file)) {
                if (doc[user].is<JsonObject>()) {
                    JsonObject obj = doc[user].as<JsonObject>();
                    currentCity = obj["c"].is<const char*>() ? obj["c"].as<String>() :
                                  (obj["city"].is<const char*>() ? obj["city"].as<String>() : defaultCity);

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
        } else {
            Serial.println("User konnte nicht gespeichert werden.");
        }
    }
}

void saveConfig(String user, String city) {
    user.trim();
    city.trim();
    if (city == "") city = "Innsbruck";

    if (currentCity != city) {
        forceWeatherUpdate = true;
    }
    currentCity = city;
    currentUser = user;

    saveDefaultCity(city);
    if (user == "") {
        Serial.println("Speichere Default-Stadt: " + city);
        return;
    }

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
    if (file) {
        serializeJson(doc, file);
        file.close();
    } else {
        Serial.println("Config konnte nicht gespeichert werden.");
    }

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
    if (file) {
        serializeJson(doc, file);
        file.close();
    } else {
        Serial.println("Design konnte nicht gespeichert werden.");
    }

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
    if (file) {
        serializeJson(doc, file);
        file.close();
    } else {
        Serial.println("Uhr-Stil konnte nicht gespeichert werden.");
    }

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

    html += "<p><a style='color:#38bdf8' href='/shooter'>Shooter Dashboard</a> &nbsp; <a style='color:#a7f3d0' href='/pong'>Pong Dashboard</a> &nbsp; <a style='color:#facc15' href='/pacman'>Pacman Dashboard</a></p>";

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
        html += "<p>Hostname: <b>" + htmlEscape(deviceHostname()) + "</b></p>";
        html += "<p>mDNS: <b>" + htmlEscape(deviceLocalUrl()) + "</b></p>";
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

void handleIpInfo() {
    String json = "{";
    json += "\"host\":\"" + deviceHostname() + "\"";
    json += ",\"url\":\"" + deviceLocalUrl() + "\"";
    json += ",\"connected\":" + String(WiFi.status() == WL_CONNECTED ? 1 : 0);
    json += ",\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

void handleShooterPage() {
    String html = R"HTML(<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>
<style>body{margin:0;background:#07111f;color:#edf6ff;font-family:Arial,sans-serif;text-align:center;touch-action:manipulation}h1{font-size:24px;margin:16px 0 6px}.wrap{max-width:960px;margin:0 auto;padding:10px}.role{color:#a7f3d0;font-size:14px;margin-bottom:10px}.score{font-size:20px;color:#facc15;margin:10px 0 14px}.panel{background:#101b2d;border:1px solid #2b3d55;border-radius:8px;padding:14px;margin:12px 0;text-align:left}.stickyEnergy{position:sticky;top:0;z-index:20;box-shadow:0 10px 24px #0008}h2{font-size:17px;margin:0 0 10px;color:#93c5fd}.players,.grid2,.grid3{display:grid;grid-template-columns:1fr 1fr;gap:10px}.grid3{grid-template-columns:1fr 1fr 1fr}.player{border:1px solid #334155;border-radius:8px;padding:10px;background:#0b1424}.p0 b{color:#67e8f9}.p1 b{color:#a3e635}.dead{color:#f87171}.wait{color:#94a3b8}.row{display:grid;grid-template-columns:145px 1fr 78px;align-items:center;gap:8px;margin:10px 0}input,select,button,output{box-sizing:border-box;border-radius:8px;border:1px solid #3b4d65;background:#0b1424;color:#fff;padding:10px;font-size:15px}input[type=range]{width:100%;accent-color:#38bdf8;padding:0}output{display:block;background:#17243a;border-color:#355274;color:#fde68a;font-weight:bold;text-align:center;min-width:78px}button{background:#2563eb;border:0;font-weight:bold;min-height:42px}button.danger{background:#dc2626}button.power{background:#be185d}button.soft{background:#16a34a}button.warn{background:#9333ea}.stat{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-top:8px;font-size:14px}.stat span,.line,.hint{background:#17243a;border-radius:6px;padding:8px}.hint{color:#bfdbfe}.energybar{height:14px;background:#0b1424;border:1px solid #334155;border-radius:8px;overflow:hidden}.energyfill{height:100%;background:#facc15;width:0%}.full{width:100%;margin-top:8px}.top{display:grid;gap:6px}@media(max-width:680px){.players,.grid2,.grid3{grid-template-columns:1fr}.row{grid-template-columns:110px 1fr 64px}}</style>
</head><body><div class='wrap'><h1>Pixel Shooter</h1><div class='role'>Web = Gegner und GameMaster. Spieler werden ueber Namen angezeigt, nicht nur als P1/P2.</div><div class='line' id='networkLine'>Netzwerk wird geladen...</div><div class='score' id='score'>Team Score 0</div>
<div class='panel stickyEnergy'><h2>Web-Gamemaster</h2><div class='line' id='energyText'>Energie 0 / 0</div><div class='energybar'><div class='energyfill' id='energyFill'></div></div><div class='hint'>Spieler-Kills laden Energie wieder auf. Starke Events kosten mehr.</div></div>
<div class='panel'><h2>Spieler</h2><div class='hint'>Schaden steigt mit eigenem Score plus Kills. Boost und Multi Shot kommen zusaetzlich oben drauf.</div><div class='players' id='players'></div><div class='grid2'><input id='p1name' placeholder='Spieler 1 Name'><input id='p2name' placeholder='Spieler 2 Name'></div><div class='grid2'><select id='p2mode'><option value='0'>Spieler 2 Auto</option><option value='1'>Spieler 2 ESP32 Joystick</option><option value='2'>Spieler 2 I2C Arduino</option></select><button class='soft' onclick='setP2Mode()'>Controller speichern</button></div><button class='full soft' onclick='saveNames()'>Namen speichern</button></div>
<div class='panel'><h2>Scoreboard</h2><div id='lastMatch' class='line'>Letztes Match: -</div><div class='top' id='topScores'></div></div>
<div class='panel'><h2>Director</h2><div class='grid3'><button onclick='preset(0)'>Leicht</button><button onclick='preset(1)'>Normal</button><button class='warn' onclick='preset(2)'>Chaos</button></div><label><input type='checkbox' id='autoWeb'> Auto-Spawns auch mit Web-Gamemaster</label><div class='row'><label>Max Gegner</label><input id='maxA' type='range' min='1' max='16' value='8'><output id='maxAo'>8</output></div><div class='row'><label>Spawn ms</label><input id='spawnMs' type='range' min='80' max='8000' step='20' value='1200'><output id='spawnMso'>1200</output></div><div class='row'><label>Move ms</label><input id='moveMs' type='range' min='15' max='1500' step='5' value='220'><output id='moveMso'>220</output></div><div class='row'><label>Klein %</label><input id='smallP' type='range' min='0' max='100' step='5' value='35'><output id='smallPo'>35</output></div><div class='row'><label>PowerUp %</label><input id='powerP' type='range' min='0' max='100' step='5' value='25'><output id='powerPo'>25</output></div><button class='full' onclick='applyDirector()'>Director anwenden</button><div class='stat' id='directorStats'></div></div>
<div class='panel'><h2>Gegner</h2><div class='grid2'><select id='spawnKind'><option value='0'>Small</option><option value='1'>Medium</option><option value='2'>Heavy</option><option value='3'>MiniBoss</option></select><select id='spawnSize'><option value='1'>1x1</option><option value='2'>2x2</option><option value='3'>3x3</option><option value='4'>4x4</option><option value='5'>5x5</option></select></div><div class='row'><label>Gegner HP</label><input id='spawnHp' type='range' min='1' max='8000' step='10' value='24'><output id='spawnHpo'>24</output></div><div class='row'><label>Speed</label><input id='spawnSpeed' type='range' min='1' max='16' value='2'><output id='spawnSpeedo'>2</output></div><button class='full danger' onclick='spawnAsteroid()'>Gegner spawnen</button></div>
<div class='panel'><h2>Boss / Nerfs</h2><div id='bossStats' class='line'>Boss HP 0</div><div class='row'><label>Boss HP</label><input id='bossHp' type='range' min='50' max='20000' step='100' value='900'><output id='bossHpo'>900</output></div><div class='row'><label>Boss Size</label><input id='bossSize' type='range' min='3' max='7' value='4'><output id='bossSizeo'>4</output></div><button class='full warn' onclick='spawnBoss()'>Boss spawnen</button><div class='row'><label>Black Hole HP</label><input id='holeHp' type='range' min='10' max='6000' step='20' value='120'><output id='holeHpo'>120</output></div><div class='row'><label>Radius</label><input id='holeRadius' type='range' min='1' max='7' value='2'><output id='holeRadiuso'>2</output></div><div class='row'><label>Hole Speed</label><input id='holeSpeed' type='range' min='1' max='12' value='2'><output id='holeSpeedo'>2</output></div><button class='full power' onclick='spawnHazard()'>Black Hole spawnen</button><button class='full soft' onclick='spawnPowerUp()'>Random PowerUp droppen</button></div></div>
<script>let synced=false;const outs=['maxA','spawnMs','moveMs','smallP','powerP','spawnHp','spawnSpeed','bossHp','bossSize','holeHp','holeRadius','holeSpeed'];function q(o){return Object.keys(o).map(k=>k+'='+encodeURIComponent(o[k])).join('&')}function ping(){fetch('/shooter/input').catch(()=>{})}function by(id){return document.getElementById(id)}function syncOut(){outs.forEach(id=>by(id+'o').value=by(id).value)}outs.forEach(id=>by(id).oninput=syncOut);syncOut();function preset(m){let p=m==0?[5,1700,260,55,35]:m==1?[8,1150,190,40,25]:[13,520,95,20,45];by('maxA').value=p[0];by('spawnMs').value=p[1];by('moveMs').value=p[2];by('smallP').value=p[3];by('powerP').value=p[4];syncOut();applyDirector()}function applyDirector(){fetch('/shooter/director?'+q({max:by('maxA').value,spawn:by('spawnMs').value,move:by('moveMs').value,small:by('smallP').value,power:by('powerP').value,auto:by('autoWeb').checked?1:0})).then(state).catch(()=>{})}function setP2Mode(){fetch('/shooter/inputmap?'+q({p2:by('p2mode').value})).then(state).catch(()=>{})}function spawnAsteroid(){fetch('/shooter/spawn?'+q({kind:by('spawnKind').value,size:by('spawnSize').value,hp:by('spawnHp').value,speed:by('spawnSpeed').value})).then(state).catch(()=>{})}function spawnBoss(){fetch('/shooter/boss?'+q({hp:by('bossHp').value,size:by('bossSize').value})).then(state).catch(()=>{})}function spawnHazard(){fetch('/shooter/hazard?'+q({type:1,hp:by('holeHp').value,radius:by('holeRadius').value,speed:by('holeSpeed').value})).then(state).catch(()=>{})}function spawnPowerUp(){fetch('/shooter/powerup').then(state).catch(()=>{})}function saveNames(){fetch('/shooter/names?'+q({p1:by('p1name').value,p2:by('p2name').value})).then(state).catch(()=>{})}function short(n,i){n=(n||('Spieler '+(i+1))).trim();return n.length>3?n.slice(0,3):n}function playerHtml(p,i){let input=i==0?'Joystick 1':(by('p2mode').options[by('p2mode').selectedIndex].text);let status=!p.active?'wartet':(p.alive?'lebt':'tot');let cls=!p.active?'wait':(p.alive?'':'dead');return `<div class='player p${i}'><b>${p.name}</b><div>${input} - <span class='${cls}'>${status}</span></div><div class='stat'><span>HP ${p.hp}</span><span>Score ${p.score}</span><span>Kills ${p.kills}</span><span>Dmg Lv ${p.level}</span><span>Basis ${p.baseDamage}</span><span>Shot ${p.damage}</span><span>Charge ${p.chargedDamage}</span><span>+1 bei ${p.nextDamageScore}</span><span>Shield ${p.shield}</span><span>Boost ${p.boost}s</span><span>Multi ${p.multi}s</span><span>Invert ${p.invert}s</span></div></div>`}async function loadNetwork(){try{let r=await fetch('/ip');let j=await r.json();by('networkLine').textContent=j.connected?`Netzwerk: ${j.ip} | ${j.url}`:`Netzwerk: offline | Host ${j.host}`}catch(e){}}async function state(){try{let r=await fetch('/shooter/state');let j=await r.json();by('score').textContent='Team Score '+j.score;by('energyText').textContent='Energie '+j.webEnergy+' / '+j.webEnergyMax;by('energyFill').style.width=Math.max(0,Math.min(100,j.webEnergy*100/j.webEnergyMax))+'%';by('players').innerHTML=j.players.map(playerHtml).join('');by('bossStats').textContent=j.boss.active?`Boss HP ${j.boss.hp} / ${j.boss.maxHp} | Size ${j.boss.size} | Phase ${j.boss.phase||'-'}`:'Kein Boss aktiv';let d=j.director;by('directorStats').innerHTML=`<span>Web ${j.web?'verbunden':'offline'}</span><span>Auto Web ${d.autoWeb?'an':'aus'}</span><span>Gegner ${d.activeAsteroids}/${d.maxAsteroids}</span><span>PowerUps ${d.activePowerUps}</span><span>Hazards ${d.activeHazards}</span><span>Slow ${d.slow}s</span><span>Move ${d.moveMs}ms</span><span>Spawn ${d.spawnMs}ms</span>`;let lm=j.lastMatch;by('lastMatch').textContent=lm.score>0?`Letztes Match: ${lm.name} - ${lm.score} | ${lm.p1Name} ${lm.p1} / ${lm.p1Kills}K, ${lm.p2Name} ${lm.p2} / ${lm.p2Kills}K | Bosse ${lm.bosses} | ${lm.duration}s`:'Letztes Match: -';by('topScores').innerHTML=j.top.length?j.top.map((t,i)=>`<div class='line'>${i+1}. ${t.name}: ${t.score}</div>`).join(''):'<div class=line>Noch keine Highscores</div>';if(!synced){by('maxA').value=d.maxAsteroids;by('spawnMs').value=d.spawnMs;by('moveMs').value=d.moveMs;by('smallP').value=d.smallPercent;by('powerP').value=d.powerChance;by('autoWeb').checked=!!d.autoWeb;by('p1name').value=j.players[0].name;by('p2name').value=j.players[1].name;by('p2mode').value=j.p2InputMode;syncOut();synced=true}}catch(e){}}setInterval(()=>{ping();state();loadNetwork()},700);ping();state();loadNetwork();</script></body></html>)HTML";
    server.send(200, "text/html", html);
}

void handleShooterState() {
    server.send(200, "application/json", raumschiffStateJson());
}

void handleShooterInput() {
    int8_t dx = server.hasArg("dx") ? (int8_t)server.arg("dx").toInt() : 0;
    int8_t dy = server.hasArg("dy") ? (int8_t)server.arg("dy").toInt() : 0;
    bool shoot = server.hasArg("shoot") && server.arg("shoot").toInt() != 0;
    bool dash = server.hasArg("dash") && server.arg("dash").toInt() != 0;
    bool charge = server.hasArg("charge") && server.arg("charge").toInt() != 0;
    raumschiffSetWebInput(dx, dy, shoot, dash, charge);
    server.send(200, "application/json", "{\"ok\":1}");
}

void handleShooterSpawn() {
    uint8_t size = server.hasArg("size") ? (uint8_t)server.arg("size").toInt() : 1;
    uint16_t hp = server.hasArg("hp") ? (uint16_t)server.arg("hp").toInt() : 0;
    uint8_t speed = server.hasArg("speed") ? (uint8_t)server.arg("speed").toInt() : 1;
    uint8_t kind = server.hasArg("kind") ? (uint8_t)server.arg("kind").toInt() : ENEMY_SMALL;
    raumschiffRequestWebSpawn(size, hp, speed, kind);
    server.send(200, "application/json", "{\"ok\":1}");
}

void handleShooterBoss() {
    uint16_t hp = server.hasArg("hp") ? (uint16_t)server.arg("hp").toInt() : 500;
    uint8_t size = server.hasArg("size") ? (uint8_t)server.arg("size").toInt() : 3;
    raumschiffRequestBoss(hp, size);
    server.send(200, "application/json", "{\"ok\":1}");
}

void handleShooterHazard() {
    uint8_t type = server.hasArg("type") ? (uint8_t)server.arg("type").toInt() : HAZARD_BLACK_HOLE;
    uint8_t radius = server.hasArg("radius") ? (uint8_t)server.arg("radius").toInt() : 2;
    uint8_t speed = server.hasArg("speed") ? (uint8_t)server.arg("speed").toInt() : 2;
    uint16_t hp = server.hasArg("hp") ? (uint16_t)server.arg("hp").toInt() : 120;
    raumschiffRequestHazard(type, radius, speed, hp);
    server.send(200, "application/json", "{\"ok\":1}");
}

void handleShooterPowerUp() {
    raumschiffRequestPowerUp();
    server.send(200, "application/json", "{\"ok\":1}");
}

void handleShooterDirector() {
    uint16_t spawnMs = server.hasArg("spawn") ? (uint16_t)server.arg("spawn").toInt() : 1700;
    uint8_t maxAsteroids = server.hasArg("max") ? (uint8_t)server.arg("max").toInt() : 4;
    uint8_t smallPercent = server.hasArg("small") ? (uint8_t)server.arg("small").toInt() : 45;
    uint16_t moveMs = server.hasArg("move") ? (uint16_t)server.arg("move").toInt() : 220;
    uint8_t powerChance = server.hasArg("power") ? (uint8_t)server.arg("power").toInt() : 25;
    bool autoWeb = server.hasArg("auto") && server.arg("auto").toInt() != 0;
    raumschiffSetDirectorSettings(spawnMs, maxAsteroids, smallPercent, moveMs, powerChance, autoWeb);
    server.send(200, "application/json", "{\"ok\":1}");
}

void handleShooterInputMap() {
    uint8_t p2Mode = server.hasArg("p2") ? (uint8_t)server.arg("p2").toInt() : 0;
    raumschiffSetP2InputMode(p2Mode);
    server.send(200, "application/json", "{\"ok\":1}");
}

void handleShooterNames() {
    if (server.hasArg("p1"))
        raumschiffSetPlayerName(0, server.arg("p1"));
    if (server.hasArg("p2"))
        raumschiffSetPlayerName(1, server.arg("p2"));
    server.send(200, "application/json", "{\"ok\":1}");
}

void handlePongPage() {
    PlayerData top[3];
    int topCount = 0;
    getTopPongScores(top, topCount);

    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{margin:0;background:#07111f;color:#edf6ff;font-family:Arial,sans-serif;padding:18px;text-align:center}";
    html += ".wrap{max-width:480px;margin:0 auto}.card{background:#101b2d;border:1px solid #2b3d55;border-radius:8px;padding:14px;margin:12px 0;text-align:left}";
    html += "h1{font-size:24px;margin:8px 0 12px}h2{font-size:17px;color:#93c5fd;margin:0 0 10px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}label{font-size:13px;color:#bfdbfe}input,button{box-sizing:border-box;width:100%;border-radius:8px;border:1px solid #3b4d65;background:#0b1424;color:#fff;padding:11px;margin:6px 0;font-size:15px}";
    html += "button{background:#2563eb;border:0;font-weight:bold}.line{background:#17243a;border-radius:6px;padding:8px;margin:6px 0}.ok{color:#a7f3d0}.muted{color:#94a3b8}@media(max-width:420px){.grid{grid-template-columns:1fr}}</style></head><body><div class='wrap'>";
    html += "<h1>Pong Dashboard</h1>";
    html += "<div class='card'><h2>Spieler</h2>";
    html += "<div class='line'>Links: <b>" + (pongLeftUser == "" ? String("Bot") : htmlEscape(pongLeftUser)) + "</b> | Rechts: <b>" + (pongRightUser == "" ? String("Bot") : htmlEscape(pongRightUser)) + "</b></div>";
    html += "<div class='line muted'>Links = I2C Joystick. Rechts = ESP32 Joystick 1. Feld leer lassen aktiviert den Bot.</div>";
    html += "<form action='/pong/login' method='POST'><div class='grid'><div><label>Links</label><input name='left' placeholder='Name links oder leer fuer Bot' value='" + htmlEscape(pongLeftUser) + "'></div>";
    html += "<div><label>Rechts</label><input name='right' placeholder='Name rechts oder leer fuer Bot' value='" + htmlEscape(pongRightUser) + "'></div></div><button>Spieler speichern</button></form>";
    if (pongLeftUser != "") html += "<div class='line ok'>" + htmlEscape(pongLeftUser) + " Best Rally: " + String(getPongHighScore(pongLeftUser)) + "</div>";
    if (pongRightUser != "" && pongRightUser != pongLeftUser) html += "<div class='line ok'>" + htmlEscape(pongRightUser) + " Best Rally: " + String(getPongHighScore(pongRightUser)) + "</div>";
    html += "</div>";
    html += "<div class='card'><h2>Top 3 Rally</h2>";
    if (topCount == 0) html += "<div class='line'>Noch keine Pong Scores.</div>";
    for (int i = 0; i < topCount; i++)
        html += "<div class='line'>" + String(i + 1) + ". " + htmlEscape(top[i].name) + ": " + String(top[i].score) + "</div>";
    html += "</div><div class='card'><a style='color:#93c5fd' href='/'>Zurueck</a></div></div></body></html>";
    server.send(200, "text/html", html);
}

void handlePongLogin() {
    if (server.hasArg("left")) {
        pongLeftUser = server.arg("left");
        pongLeftUser.trim();
        ensureUserExists(pongLeftUser);
    }
    if (server.hasArg("right")) {
        pongRightUser = server.arg("right");
        pongRightUser.trim();
        ensureUserExists(pongRightUser);
    }
    if (pongLeftUser != "") {
        loadConfigForUser(pongLeftUser);
    } else if (pongRightUser != "") {
        loadConfigForUser(pongRightUser);
    }
    server.sendHeader("Location", "/pong");
    server.send(303);
}

void handlePacmanPage() {
    PlayerData top[3];
    int topCount = 0;
    getTopPacmanScores(top, topCount);

    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{margin:0;background:#07111f;color:#edf6ff;font-family:Arial,sans-serif;padding:18px;text-align:center}";
    html += ".wrap{max-width:520px;margin:0 auto}.card{background:#101b2d;border:1px solid #2b3d55;border-radius:8px;padding:14px;margin:12px 0;text-align:left}";
    html += "h1{font-size:24px;margin:8px 0 12px}h2{font-size:17px;color:#93c5fd;margin:0 0 10px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}label{font-size:13px;color:#bfdbfe}input,button{box-sizing:border-box;width:100%;border-radius:8px;border:1px solid #3b4d65;background:#0b1424;color:#fff;padding:11px;margin:6px 0;font-size:15px}";
    html += "button{background:#2563eb;border:0;font-weight:bold}.line{background:#17243a;border-radius:6px;padding:8px;margin:6px 0}.ok{color:#a7f3d0}.muted{color:#94a3b8}.check{display:flex;gap:8px;align-items:center}.check input{width:auto}@media(max-width:460px){.grid{grid-template-columns:1fr}}</style></head><body><div class='wrap'>";
    html += "<h1>Pacman Dashboard</h1>";
    html += "<div class='card'><h2>Spieler</h2>";
    html += "<div class='line'>P1: <b>" + (pacmanP1User == "" ? String("-") : htmlEscape(pacmanP1User)) + "</b> | P2: <b>" + (pacmanP2User == "" ? String("-") : htmlEscape(pacmanP2User)) + "</b></div>";
    html += "<div class='line muted'>P1 = ESP32 Joystick 1. P2 = I2C Joystick. Joystick 2 bleibt Menu und Ghost: Klick wechselt Geist, Langklick nutzt Ability, Doppelklick zurueck.</div>";
    html += "<form action='/pacman/login' method='POST'><div class='grid'><div><label>Pacman 1</label><input name='p1' placeholder='Name P1' value='" + htmlEscape(pacmanP1User) + "'></div>";
    html += "<div><label>Pacman 2</label><input name='p2' placeholder='Name P2 optional' value='" + htmlEscape(pacmanP2User) + "'></div></div>";
    html += "<label class='check'><input type='checkbox' name='ghost' value='1'";
    if (pacmanGhostManual) html += " checked";
    html += "> Ghost manuell ueber Joystick 2</label><button>Pacman speichern</button></form>";
    if (pacmanP1User != "") html += "<div class='line ok'>" + htmlEscape(pacmanP1User) + " Highscore: " + String(getPacmanHighScore(pacmanP1User)) + "</div>";
    if (pacmanP2User != "" && pacmanP2User != pacmanP1User) html += "<div class='line ok'>" + htmlEscape(pacmanP2User) + " Highscore: " + String(getPacmanHighScore(pacmanP2User)) + "</div>";
    html += "</div><div class='card'><h2>Top 3 Pacman</h2>";
    if (topCount == 0) html += "<div class='line'>Noch keine Pacman Scores.</div>";
    for (int i = 0; i < topCount; i++)
        html += "<div class='line'>" + String(i + 1) + ". " + htmlEscape(top[i].name) + ": " + String(top[i].score) + "</div>";
    html += "</div><div class='card'><a style='color:#93c5fd' href='/'>Zurueck</a></div></div></body></html>";
    server.send(200, "text/html", html);
}

void handlePacmanLogin() {
    if (server.hasArg("p1")) {
        pacmanP1User = server.arg("p1");
        pacmanP1User.trim();
        ensureUserExists(pacmanP1User);
    }
    if (server.hasArg("p2")) {
        pacmanP2User = server.arg("p2");
        pacmanP2User.trim();
        ensureUserExists(pacmanP2User);
    }
    pacmanGhostManual = server.hasArg("ghost");
    if (pacmanP1User != "") loadConfigForUser(pacmanP1User);
    else if (pacmanP2User != "") loadConfigForUser(pacmanP2User);
    server.sendHeader("Location", "/pacman");
    server.send(303);
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
    server.on("/ip", HTTP_GET, handleIpInfo);
    server.on("/pong", HTTP_GET, handlePongPage);
    server.on("/pong/login", HTTP_POST, handlePongLogin);
    server.on("/pacman", HTTP_GET, handlePacmanPage);
    server.on("/pacman/login", HTTP_POST, handlePacmanLogin);
    server.on("/shooter", HTTP_GET, handleShooterPage);
    server.on("/shooter/state", HTTP_GET, handleShooterState);
    server.on("/shooter/input", HTTP_GET, handleShooterInput);
    server.on("/shooter/spawn", HTTP_GET, handleShooterSpawn);
    server.on("/shooter/boss", HTTP_GET, handleShooterBoss);
    server.on("/shooter/hazard", HTTP_GET, handleShooterHazard);
    server.on("/shooter/powerup", HTTP_GET, handleShooterPowerUp);
    server.on("/shooter/director", HTTP_GET, handleShooterDirector);
    server.on("/shooter/inputmap", HTTP_GET, handleShooterInputMap);
    server.on("/shooter/names", HTTP_GET, handleShooterNames);
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
        if (file) {
            serializeJson(doc, file);
            file.close();
            Serial.println("Neuer Highscore gespeichert: " + String(score));
        } else {
            Serial.println("Highscore konnte nicht gespeichert werden.");
        }
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
        if (count < 10 && isScoreEntry(kv)) {
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

void savePongHighScore(String user, int score) {
    user.trim();
    if (user == "" || score <= 0) return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    JsonObject obj = userObject(doc, user);
    int currentHigh = readPongHighScore(doc[user]);
    if (score > currentHigh) {
        obj["pong"] = score;
        removeLegacyUserKeys(obj);

        File file = LittleFS.open("/config.json", "w");
        if (file) {
            serializeJson(doc, file);
            file.close();
            Serial.println("Neuer Pong-Highscore gespeichert: " + String(score));
        } else {
            Serial.println("Pong-Highscore konnte nicht gespeichert werden.");
        }
    }
}

int getPongHighScore(String user) {
    user.trim();
    if (user == "" || !LittleFS.exists("/config.json")) return 0;

    File file = LittleFS.open("/config.json", "r");
    if (!file) return 0;

    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();
    return readPongHighScore(doc[user]);
}

void savePacmanHighScore(String user, int score) {
    user.trim();
    if (user == "" || score <= 0) return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    JsonObject obj = userObject(doc, user);
    int currentHigh = readPacmanHighScore(doc[user]);
    if (score > currentHigh) {
        obj["pac"] = score;
        removeLegacyUserKeys(obj);

        File file = LittleFS.open("/config.json", "w");
        if (file) {
            serializeJson(doc, file);
            file.close();
            Serial.println("Neuer Pacman-Highscore gespeichert: " + String(score));
        } else {
            Serial.println("Pacman-Highscore konnte nicht gespeichert werden.");
        }
    }
}

int getPacmanHighScore(String user) {
    user.trim();
    if (user == "" || !LittleFS.exists("/config.json")) return 0;

    File file = LittleFS.open("/config.json", "r");
    if (!file) return 0;

    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();
    return readPacmanHighScore(doc[user]);
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
        if (!isScoreEntry(kv)) {
            continue;
        }

        int score = readHighScore(kv.value());
        if (count < 3) {
            int pos = count;
            while (pos > 0 && list[pos - 1].score < score) {
                list[pos] = list[pos - 1];
                pos--;
            }
            list[pos].name = kv.key().c_str();
            list[pos].score = score;
            count++;
        } else if (score > list[2].score) {
            int pos = 2;
            while (pos > 0 && list[pos - 1].score < score) {
                list[pos] = list[pos - 1];
                pos--;
            }
            list[pos].name = kv.key().c_str();
            list[pos].score = score;
        }
    }
}

void getTopPongScores(PlayerData* list, int& count) {
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
        if (!isPongScoreEntry(kv)) {
            continue;
        }

        int score = readPongHighScore(kv.value());
        if (count < 3) {
            int pos = count;
            while (pos > 0 && list[pos - 1].score < score) {
                list[pos] = list[pos - 1];
                pos--;
            }
            list[pos].name = kv.key().c_str();
            list[pos].score = score;
            count++;
        } else if (score > list[2].score) {
            int pos = 2;
            while (pos > 0 && list[pos - 1].score < score) {
                list[pos] = list[pos - 1];
                pos--;
            }
            list[pos].name = kv.key().c_str();
            list[pos].score = score;
        }
    }
}

void getTopPacmanScores(PlayerData* list, int& count) {
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
        if (!isPacmanScoreEntry(kv)) {
            continue;
        }

        int score = readPacmanHighScore(kv.value());
        if (count < 3) {
            int pos = count;
            while (pos > 0 && list[pos - 1].score < score) {
                list[pos] = list[pos - 1];
                pos--;
            }
            list[pos].name = kv.key().c_str();
            list[pos].score = score;
            count++;
        } else if (score > list[2].score) {
            int pos = 2;
            while (pos > 0 && list[pos - 1].score < score) {
                list[pos] = list[pos - 1];
                pos--;
            }
            list[pos].name = kv.key().c_str();
            list[pos].score = score;
        }
    }
}
