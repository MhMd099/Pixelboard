#include "Config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "HardwareUtils.h"
#include "RaumschiffGame.h"
#include "SoundUtils.h"
#include "Joystick.h"
#include "RuntimeHealth.h"
#include "AppNavigation.h"
#include "ConfigStorage.h"
#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#define WEATHER_API_KEY "343df2364dc5541a3efd274bf2f845df"
#endif

extern Joystick joystick1;
extern Joystick joystick2;
extern Joystick joystick3;

const char* weatherApiKey = WEATHER_API_KEY;

String currentCity = "Innsbruck";
uint8_t snakeObstacleTarget = 0;
uint8_t snakeFoodTarget = 3;
uint8_t snakePowerupTarget = 1;
uint8_t snakeBotCount = 2;
uint8_t joystick1DeadzonePercent = 1;
uint8_t joystick2DeadzonePercent = 1;
uint8_t joystick3DeadzonePercent = 4;
volatile uint32_t tetrisCurrentScore = 0;
volatile uint32_t tetrisBoardScore = 0;
volatile bool tetrisPlaying = false;
volatile uint16_t tetrisNormalFallMs = 500;
volatile uint16_t tetrisSoftFallMs = 65;
volatile uint16_t tetrisMinFallMs = 220;
volatile uint16_t tetrisSpeedScoreStep = 250;
volatile uint16_t tetrisSpeedStepMs = 35;
volatile bool tetrisFastBotMode = false;
volatile uint8_t pongBotMode = 1;
static String standbyText = "NothinBL";
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
static const char* captivePassword = "NothinBL";
static const char* deviceHostNameValue = "pixelboard";
static unsigned long lastWifiBeginMs = 0;
static unsigned long firstWifiAttemptMs = 0;

extern volatile int aktiverTask;
extern int fokusModus;

using ConfigGuard = ConfigStorageGuard;

void initLittleFS() {
    if (!beginConfigStorage()) {
        Serial.println("LittleFS Mount fehlgeschlagen.");
        return;
    }
    Serial.println("LittleFS erfolgreich gemountet.");
}

String htmlEscape(const String& value) {
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

String jsonEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c == '\\' || c == '"') {
            escaped += '\\';
            escaped += c;
        } else if (c == '\n') escaped += F("\\n");
        else if (c == '\r') escaped += F("\\r");
        else if (c == '\t') escaped += F("\\t");
        else if ((uint8_t)c >= 0x20) escaped += c;
    }
    return escaped;
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
    ConfigGuard guard;
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
    ConfigGuard guard;
    return wifiSsid.length() > 0;
}

String configuredWifiSsid() {
    ConfigGuard guard;
    return wifiSsid;
}

String currentCitySnapshot() { ConfigGuard guard; return currentCity; }

String standbyTextSnapshot() {
    ConfigGuard guard;
    return standbyText;
}

void setStandbyText(String text) {
    ConfigGuard guard;
    text.trim();
    String clean;
    clean.reserve(32);
    for (size_t i = 0; i < text.length() && clean.length() < 32; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c >= 32 && c <= 126) clean += (char)c;
    }
    if (clean.length() == 0) clean = "NothinBL";
    standbyText = clean;
}

String deviceHostname() {
    return String(deviceHostNameValue);
}

String deviceLocalUrl() {
    return String("http://") + deviceHostname() + ".local";
}

void beginWifiConnection() {
    if (!hasWifiCredentials()) return;

    if (captivePortalActive) {
        // Keep the already secured portal online while trying the configured WLAN.
        WiFi.mode(WIFI_AP_STA);
    } else {
        // Never let the ESP32 restore an old open AP configuration from NVS.
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }
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
    ConfigGuard guard;
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

    if (writeJsonAtomically("/wifi.json", doc)) {
        Serial.println("WLAN-Daten gespeichert fuer SSID: " + wifiSsid);
    } else {
        Serial.println("WLAN-Daten konnten nicht gespeichert werden.");
    }

    beginWifiConnection();
}

void loadDeviceSettings() {
    // Alle Werte sind reine Laufzeitwerte und starten nach jedem Boot mit
    // stabilen Standardwerten. Nur die WLAN-Zugangsdaten bleiben persistent.
    currentDhtPin = 21;
    currentDhtType = 22;
    joystick1DeadzonePercent = 1;
    joystick2DeadzonePercent = 1;
    joystick3DeadzonePercent = 4;
    snakeObstacleTarget = 0;
    snakeFoodTarget = 3;
    snakePowerupTarget = 1;
    snakeBotCount = 2;
    tetrisNormalFallMs = 650;
    tetrisSoftFallMs = 90;
    tetrisMinFallMs = 300;
    tetrisSpeedScoreStep = 400;
    tetrisSpeedStepMs = 25;
    tetrisFastBotMode = false;
    pongBotMode = 1;
    standbyText = "NothinBL";
    setMasterVolume(100);
    setEffectsVolume(100);
    setMusicVolume(100);
    joystick1.setDeadZonePercent(joystick1DeadzonePercent);
    joystick2.setDeadZonePercent(joystick2DeadzonePercent);
    joystick3.setDeadZonePercent(joystick3DeadzonePercent);

    Serial.print("DHT Standard: GPIO ");
    Serial.print(currentDhtPin);
    Serial.print(", DHT");
    Serial.println(currentDhtType);
    Serial.println("Sound-Lautstaerke: " + String(getMasterVolume()) + "%");
    Serial.println("Sound-Mix: Effekte " + String(getEffectsVolume()) +
                   "%, Musik " + String(getMusicVolume()) + "%");
    Serial.println("Joystick-Deadzones: " + String(joystick1DeadzonePercent) + "/" +
                   String(joystick2DeadzonePercent) + "/" + String(joystick3DeadzonePercent) + "%");
}

void loadDeviceProfile() {
    currentCity = "Innsbruck";
    applyTheme(0);
    applyClockStyle(0);
    forceWeatherUpdate = true;
    Serial.println("Fluechtiges Geraeteprofil: Stadt=" + currentCity +
                   ", Design=" + String(themeName(g_themeIndex)) + ", Uhr=" + String(clockStyleName(g_clockStyle)));
}

void saveCity(String city) {
    ConfigGuard guard;
    city.trim();
    if (city == "") city = "Innsbruck";

    if (currentCity != city) {
        forceWeatherUpdate = true;
    }
    currentCity = city;
    Serial.println("Stadt bis zum Neustart gesetzt: " + city);
}

void saveTheme(int themeIdx) {
    ConfigGuard guard;
    applyTheme(themeIdx);
    Serial.println("Design bis zum Neustart gesetzt: " + String(themeName(g_themeIndex)));
}

void saveClockStyle(int clockIdx) {
    ConfigGuard guard;
    applyClockStyle(clockIdx);
    Serial.println("Uhr-Stil bis zum Neustart gesetzt: " + String(clockStyleName(g_clockStyle)));
}

static void appendOption(String& html, int value, int current, const String& label) {
    html += "<option value='" + String(value) + "'";
    if (value == current) html += " selected";
    html += ">" + label + "</option>";
}

static const char globalSoundControl[] PROGMEM = R"HTML(
<style>
body{padding-top:0!important}
#globalSoundControl{position:sticky;top:0;z-index:1000;box-sizing:border-box;width:100vw;max-width:100vw;margin-left:calc(50% - 50vw);padding:7px 12px;background:#0b1220f5;border-bottom:1px solid #334155;box-shadow:0 5px 16px #0008;color:#e2e8f0;font:600 14px Arial,sans-serif;text-align:left}
#globalSoundMain{display:grid;grid-template-columns:auto minmax(90px,320px) 48px auto auto auto;align-items:center;justify-content:center;gap:8px}
#globalSoundControl input[type=range]{box-sizing:border-box;width:100%!important;height:28px;margin:0!important;padding:0!important;border:0!important;background:transparent!important;accent-color:#38bdf8}
#globalSoundControl output{box-sizing:border-box;width:48px!important;min-width:48px!important;margin:0!important;padding:0!important;border:0!important;background:transparent!important;color:#f8fafc!important;font:700 13px Arial,sans-serif!important;text-align:right!important}
#globalSoundControl button{box-sizing:border-box;width:auto!important;min-height:34px!important;margin:0!important;padding:7px 10px!important;border:1px solid #475569!important;border-radius:7px!important;background:#1e293b!important;color:#f8fafc!important;font:700 13px Arial,sans-serif!important;cursor:pointer}
#globalSoundMute[aria-pressed='true']{background:#991b1b!important;border-color:#ef4444!important}
#globalSoundMix{display:none;grid-template-columns:auto minmax(80px,220px) 46px auto minmax(80px,220px) 46px;align-items:center;justify-content:center;gap:7px;padding-top:7px}
#globalSoundControl[data-mix='open'] #globalSoundMix{display:grid}
@media(max-width:520px){#globalSoundLabel{display:none}#globalSoundMain{grid-template-columns:minmax(70px,1fr) 42px auto auto auto;gap:4px}#globalSoundMix{grid-template-columns:auto minmax(70px,1fr) 42px;gap:5px}#globalMusicLabel{grid-column:1}#globalSoundControl output{width:42px!important;min-width:42px!important}#globalSoundControl button{padding-left:6px!important;padding-right:6px!important}}
</style>
<div id="globalSoundControl" role="group" aria-label="Globale Lautstaerke" data-mix="closed">
  <div id="globalSoundMain">
    <label id="globalSoundLabel" for="globalSoundSlider">Sound</label>
    <input id="globalSoundSlider" type="range" min="0" max="100" step="5" value="100" aria-label="Globale Lautstaerke">
    <output id="globalSoundValue" for="globalSoundSlider">100%</output>
    <button id="globalSoundMute" type="button" aria-pressed="false">Stumm</button>
    <button id="globalSoundMixButton" type="button" aria-expanded="false">Mix</button>
    <button id="globalSoundTest" type="button">Test</button>
  </div>
  <div id="globalSoundMix">
    <label for="globalEffectsSlider">Effekte</label><input id="globalEffectsSlider" type="range" min="0" max="100" step="5" value="100"><output id="globalEffectsValue">100%</output>
    <label id="globalMusicLabel" for="globalMusicSlider">Musik</label><input id="globalMusicSlider" type="range" min="0" max="100" step="5" value="100"><output id="globalMusicValue">100%</output>
  </div>
</div>
<script>
(()=>{const c=document.getElementById('globalSoundControl'),s=document.getElementById('globalSoundSlider'),e=document.getElementById('globalEffectsSlider'),m=document.getElementById('globalMusicSlider'),so=document.getElementById('globalSoundValue'),eo=document.getElementById('globalEffectsValue'),mo=document.getElementById('globalMusicValue'),mute=document.getElementById('globalSoundMute'),mix=document.getElementById('globalSoundMixButton'),test=document.getElementById('globalSoundTest');let last=70;const clamp=v=>Math.max(0,Math.min(100,Number(v)||0));const draw=j=>{const v=clamp(j.volume),ev=clamp(j.effects),mv=clamp(j.music);s.value=v;e.value=ev;m.value=mv;so.textContent=v+'%';eo.textContent=ev+'%';mo.textContent=mv+'%';mute.textContent=v===0?'Ton an':'Stumm';mute.setAttribute('aria-pressed',v===0?'true':'false');if(v>0)last=v};const local=()=>draw({volume:s.value,effects:e.value,music:m.value});const load=()=>fetch('/sound/state',{cache:'no-store'}).then(r=>r.json()).then(draw).catch(()=>{});const save=data=>fetch('/sound/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)}).then(r=>{if(!r.ok)throw Error();return r.json()}).then(draw).catch(load);[s,e,m].forEach(x=>x.addEventListener('input',local));s.addEventListener('change',()=>save({volume:s.value}));e.addEventListener('change',()=>save({effects:e.value}));m.addEventListener('change',()=>save({music:m.value}));mute.addEventListener('click',()=>save({volume:Number(s.value)===0?last:0}));mix.addEventListener('click',()=>{const open=c.dataset.mix!=='open';c.dataset.mix=open?'open':'closed';mix.setAttribute('aria-expanded',open?'true':'false')});test.addEventListener('click',()=>fetch('/sound/test',{method:'POST'}).catch(()=>{}));load()})();
</script>
<script>
(()=>{const originalFetch=window.fetch.bind(window),postPaths=['/shooter/input','/shooter/spawn','/shooter/boss','/shooter/hazard','/shooter/powerup','/shooter/director','/shooter/inputmap','/shooter/names'];window.fetch=(input,options={})=>{if(typeof input==='string'&&!options.method){const q=input.indexOf('?'),path=q>=0?input.slice(0,q):input;if(postPaths.includes(path)){const body=q>=0?input.slice(q+1):'';return originalFetch(path,{...options,method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded',...(options.headers||{})},body})}}return originalFetch(input,options)}})();
</script>
)HTML";

static void sendDashboardHtml(String& html) {
    const String bodyTag = F("<body>");
    int bodyPos = html.indexOf(bodyTag);
    if (bodyPos < 0) {
        Serial.println("HTML-Seite ohne <body>-Tag; Sound-Regler konnte nicht eingefuegt werden.");
        server.send(500, "text/plain; charset=utf-8", "HTML-Seite ist ungueltig.");
        return;
    }

    // Stream the shared control from flash instead of duplicating every page.
    int insertPos = bodyPos + bodyTag.length();
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html; charset=utf-8", "");
    server.sendContent(html.substring(0, insertPos));
    server.sendContent_P(globalSoundControl);
    server.sendContent(html.substring(insertPos));
    server.sendContent("");
}

static String soundStateJson(bool ok = true) {
    uint8_t volume = getMasterVolume();
    String json = F("{\"ok\":");
    json += ok ? F("true") : F("false");
    json += F(",\"volume\":");
    json += String(volume);
    json += F(",\"muted\":");
    json += volume == 0 ? F("true") : F("false");
    json += F(",\"effects\":");
    json += String(getEffectsVolume());
    json += F(",\"music\":");
    json += String(getMusicVolume());
    json += '}';
    return json;
}

static void sendSoundJson(int status, const String& json) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(status, "application/json; charset=utf-8", json);
}

static bool readRequestedVolume(const char* argument, uint8_t current, uint8_t& volume) {
    if (!server.hasArg(argument)) {
        volume = current;
        return true;
    }

    String value = server.arg(argument);
    value.trim();
    if (value.length() == 0 || value.length() > 3) return false;
    for (size_t i = 0; i < value.length(); i++) {
        if (value[i] < '0' || value[i] > '9') return false;
    }

    int parsed = value.toInt();
    if (parsed < 0 || parsed > 100) return false;
    volume = (uint8_t)parsed;
    return true;
}

static void handleSoundState() {
    sendSoundJson(200, soundStateJson());
}

static void handleSoundTest() {
    playSound(SND_LEVELUP);
    sendSoundJson(200, F("{\"ok\":true}"));
}

static void handleSoundSettings() {
    if (!server.hasArg("volume") && !server.hasArg("effects") && !server.hasArg("music")) {
        sendSoundJson(400, F("{\"ok\":false,\"error\":\"missing sound setting\"}"));
        return;
    }

    uint8_t volume = getMasterVolume();
    uint8_t effects = getEffectsVolume();
    uint8_t music = getMusicVolume();
    if (!readRequestedVolume("volume", volume, volume) ||
        !readRequestedVolume("effects", effects, effects) ||
        !readRequestedVolume("music", music, music)) {
        sendSoundJson(400, F("{\"ok\":false,\"error\":\"sound values must be between 0 and 100\"}"));
        return;
    }

    if (volume == getMasterVolume() && effects == getEffectsVolume() && music == getMusicVolume()) {
        sendSoundJson(200, soundStateJson());
        return;
    }

    setMasterVolume(volume);
    setEffectsVolume(effects);
    setMusicVolume(music);
    sendSoundJson(200, soundStateJson());
}

static String dashboardNav(const String& active) {
    String nav = "<div class='nav'>";
    if (active != "home") nav += "<a href='/'>Home</a>";
    if (active != "shooter") nav += "<a href='/shooter'>Shooter</a>";
    if (active != "snake") nav += "<a href='/snake'>Snake</a>";
    if (active != "tetris") nav += "<a href='/tetris'>Tetris</a>";
    nav += "</div>";
    return nav;
}

void handleRoot() {
    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    String savedSsid = configuredWifiSsid();

    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:Arial,sans-serif;background:radial-gradient(circle at top,#162a46 0,#07111f 52%,#030712 100%);color:#f8fafc;text-align:center;margin:0;min-height:100vh;padding:22px;}";
    html += "h1{margin:10px 0 18px;font-size:32px;letter-spacing:.04em}h3{margin:0 0 14px;color:#7dd3fc}h4{margin:18px 0 4px;color:#cbd5e1}";
    html += "input[type='text'],input[type='password'],input[type='number'],select{box-sizing:border-box;width:100%;padding:12px;margin:7px 0;border-radius:10px;border:1px solid #334155;background:#081120;color:#fff;outline:none}input:focus,select:focus{border-color:#38bdf8;box-shadow:0 0 0 3px #38bdf822}";
    html += "input[type='submit']{width:100%;background:linear-gradient(135deg,#0284c7,#06b6d4);color:#fff;padding:12px 20px;margin-top:7px;border:none;border-radius:10px;font-weight:bold;cursor:pointer}";
    html += ".card{box-sizing:border-box;background:#0f1b2ddd;border:1px solid #29405e;padding:20px;border-radius:16px;display:inline-block;margin:8px;min-width:min(330px,92vw);max-width:420px;box-shadow:0 14px 36px #0007;vertical-align:top;backdrop-filter:blur(8px)}";
    html += ".status{display:inline-flex;align-items:center;gap:8px;padding:7px 12px;border-radius:999px;background:#07111f}.dot{width:9px;height:9px;border-radius:50%;background:#22c55e;box-shadow:0 0 12px #22c55e}.bad .dot{background:#ef4444;box-shadow:0 0 12px #ef4444}.nav{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin:14px auto 20px}.nav a{background:#12223a;border:1px solid #334b6b;border-radius:999px;padding:9px 15px;color:#bae6fd}hr{border:0;border-top:1px solid #2a3b52;margin:18px 0}a{text-decoration:none;font-weight:bold}</style></head><body>";

    html += "<h1>PixelBoard</h1>";

    html += dashboardNav("home");

    html += "<div class='card'><h3>WLAN</h3>";
    if (wifiConnected) {
        html += "<div class='status'><span class='dot'></span><b>" + htmlEscape(WiFi.SSID()) + "</b></div>";
    } else if (hasWifiCredentials()) {
        html += "<div class='status bad'><span class='dot'></span><b>Offline</b></div>";
    } else {
        html += "<div class='status bad'><span class='dot'></span><b>WLAN</b></div>";
    }
    html += "<form action='/saveWifi' method='POST'>";
    html += "<input type='text' name='ssid' placeholder='WLAN SSID' value='" + htmlEscape(savedSsid) + "' required><br>";
    html += "<input type='password' name='password' placeholder='WLAN Passwort'><br>";
    html += "<input type='submit' value='WLAN speichern'></form></div>";

    html += "<div class='card'><h3>Einstellungen</h3>";
    html += "<form action='/updateCity' method='POST'>";
    html += "<input type='text' name='city' placeholder='Stadt' value='" + htmlEscape(currentCity) + "' required><br>";
    html += "<input type='submit' value='Stadt anwenden'></form>";

    html += "<hr><h4>Design</h4>";
    html += "<form action='/updateTheme' method='POST'><select name='theme'>";
    for (int i = 0; i < themeAnzahl(); i++) {
        appendOption(html, i, g_themeIndex, String(themeName(i)));
    }
    html += "</select><br><input type='submit' value='Design anwenden'></form>";

    html += "<h4>Uhr-Stil</h4>";
    html += "<form action='/updateClock' method='POST'><select name='clock'>";
    for (int i = 0; i < clockStyleAnzahl(); i++) {
        appendOption(html, i, g_clockStyle, String(clockStyleName(i)));
    }
    html += "</select><br><input type='submit' value='Uhr anwenden'></form>";

    html += "<hr><h4>Joystick-Ruhezone</h4><form action='/updateJoysticks' method='POST'>";
    html += "<label>Joystick 1 (%)</label><br><input type='number' name='j1' min='1' max='20' value='" + String(joystick1DeadzonePercent) + "'><br>";
    html += "<label>Menue-Joystick (%)</label><br><input type='number' name='j2' min='1' max='20' value='" + String(joystick2DeadzonePercent) + "'><br>";
    html += "<label>I2C-Joystick (%)</label><br><input type='number' name='j3' min='1' max='20' value='" + String(joystick3DeadzonePercent) + "'><br>";
    html += "<input type='submit' value='Joysticks anwenden'></form>";

    html += "</div><div class='card'><h3>Standby</h3>";
    html += "<form action='/updateStandby' method='POST'><input type='text' name='text' maxlength='32' value='" + htmlEscape(standbyTextSnapshot()) + "' required>";
    html += "<input type='submit' value='Text anzeigen'></form>";

    html += "</div><div class='card'><h3>Bot-Modi</h3>";
    html += "<form action='/updatePongMode' method='POST'><h4>Pong</h4><select name='mode'>";
    appendOption(html, 0, pongBotMode, "Normal");
    appendOption(html, 1, pongBotMode, "Plausibel");
    appendOption(html, 2, pongBotMode, "Super");
    html += "</select><input type='submit' value='Pong anwenden'></form>";
    html += "<form action='/tetris/settings' method='POST'><h4>Tetris</h4><select name='botmode'>";
    appendOption(html, 0, tetrisFastBotMode ? 1 : 0, "Normal");
    appendOption(html, 1, tetrisFastBotMode ? 1 : 0, "Schnell");
    html += "</select><input type='submit' value='Tetris anwenden'></form>";

    html += "</div></body></html>";
    sendDashboardHtml(html);
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
    // This page is intentionally kept in flash. Copying the whole dashboard
    // into a String (and then creating two substring copies) fragmented the
    // ESP32 heap badly enough to reset the board when /shooter was opened.
    static const char shooterHtml[] PROGMEM = R"HTML(<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>
<style>body{margin:0;background:radial-gradient(circle at top,#162a46 0,#07111f 52%,#030712 100%);color:#edf6ff;font-family:Arial,sans-serif;text-align:center;touch-action:manipulation;min-height:100vh}h1{font-size:27px;margin:18px 0 8px;letter-spacing:.04em}.wrap{max-width:960px;margin:0 auto;padding:10px}.nav{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin:10px 0 16px}.nav a{background:#12223a;border:1px solid #334b6b;border-radius:999px;padding:9px 15px;color:#bae6fd;text-decoration:none;font-weight:bold}.score{display:inline-block;font-size:20px;color:#facc15;margin:2px 0 10px;padding:9px 16px;background:#111d30;border:1px solid #334155;border-radius:999px}.panel{background:#0e1a2bdd;border:1px solid #2b4564;border-radius:16px;padding:15px;margin:12px 0;text-align:left;box-shadow:0 12px 28px #0005}.stickyEnergy{position:sticky;top:0;z-index:20;backdrop-filter:blur(10px)}h2{font-size:17px;margin:0 0 10px;color:#7dd3fc}.players,.grid2,.grid3{display:grid;grid-template-columns:1fr 1fr;gap:10px}.grid3{grid-template-columns:1fr 1fr 1fr}.player{border:1px solid #334b67;border-radius:12px;padding:11px;background:#081321}.p0 b{color:#67e8f9}.p1 b{color:#a3e635}.dead{color:#f87171}.wait{color:#94a3b8}.row{display:grid;grid-template-columns:145px 1fr 78px;align-items:center;gap:8px;margin:10px 0}input,select,button,output{box-sizing:border-box;border-radius:10px;border:1px solid #3b4d65;background:#081321;color:#fff;padding:10px;font-size:15px}input[type=range]{width:100%;accent-color:#38bdf8;padding:0}output{display:block;background:#17243a;border-color:#355274;color:#fde68a;font-weight:bold;text-align:center;min-width:78px}button{background:linear-gradient(135deg,#1d4ed8,#2563eb);border:0;font-weight:bold;min-height:42px}button.danger{background:#dc2626}button.power{background:#be185d}button.soft{background:#16a34a}button.warn{background:#9333ea}.stat{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-top:8px;font-size:14px}.stat span,.line{background:#17243a;border-radius:8px;padding:8px}.energybar{height:14px;background:#081321;border:1px solid #334155;border-radius:999px;overflow:hidden}.energyfill{height:100%;background:linear-gradient(90deg,#06b6d4,#facc15);width:0%}.full{width:100%;margin-top:8px}.top{display:grid;gap:6px}@media(max-width:680px){.players,.grid2,.grid3{grid-template-columns:1fr}.row{grid-template-columns:110px 1fr 64px}}</style>
</head><body><div class='wrap'><h1>Pixel Shooter</h1><div class='nav'><a href='/'>Home</a><a href='/snake'>Snake</a><a href='/tetris'>Tetris</a></div><div class='score' id='score'>Team Score 0</div>
<div class='panel stickyEnergy'><h2>Web-Gamemaster</h2><div class='line' id='energyText'>Energie 0 / 0</div><div class='energybar'><div class='energyfill' id='energyFill'></div></div></div>
<div class='panel'><h2>Spieler</h2><div class='players' id='players'></div></div>
<div class='panel'><h2>Letztes Match</h2><div id='lastMatch' class='line'>Letztes Match: -</div></div>
<div class='panel'><h2>Director</h2><div class='grid3'><button onclick='preset(0)'>Leicht</button><button onclick='preset(1)'>Normal</button><button class='warn' onclick='preset(2)'>Chaos</button></div><label><input type='checkbox' id='autoWeb'> Auto-Spawns auch mit Web-Gamemaster</label><div class='row'><label>Max Gegner</label><input id='maxA' type='range' min='1' max='16' value='8'><output id='maxAo'>8</output></div><div class='row'><label>Spawn ms</label><input id='spawnMs' type='range' min='80' max='8000' step='20' value='1200'><output id='spawnMso'>1200</output></div><div class='row'><label>Move ms</label><input id='moveMs' type='range' min='15' max='1500' step='5' value='220'><output id='moveMso'>220</output></div><div class='row'><label>Klein %</label><input id='smallP' type='range' min='0' max='100' step='5' value='35'><output id='smallPo'>35</output></div><div class='row'><label>PowerUp %</label><input id='powerP' type='range' min='0' max='100' step='5' value='25'><output id='powerPo'>25</output></div><button class='full' onclick='applyDirector()'>Director anwenden</button><div class='stat' id='directorStats'></div></div>
<div class='panel'><h2>Gegner</h2><div class='grid2'><select id='spawnKind'><option value='0'>Small</option><option value='1'>Medium</option><option value='2'>Heavy</option><option value='3'>MiniBoss</option></select><select id='spawnSize'><option value='1'>1x1</option><option value='2'>2x2</option><option value='3'>3x3</option><option value='4'>4x4</option><option value='5'>5x5</option></select></div><div class='row'><label>Gegner HP</label><input id='spawnHp' type='range' min='1' max='8000' step='10' value='24'><output id='spawnHpo'>24</output></div><div class='row'><label>Speed</label><input id='spawnSpeed' type='range' min='1' max='16' value='2'><output id='spawnSpeedo'>2</output></div><button class='full danger' onclick='spawnAsteroid()'>Gegner spawnen</button></div>
<div class='panel'><h2>Boss / Nerfs</h2><div id='bossStats' class='line'>Boss HP 0</div><div class='row'><label>Boss HP</label><input id='bossHp' type='range' min='50' max='20000' step='100' value='900'><output id='bossHpo'>900</output></div><div class='row'><label>Boss Size</label><input id='bossSize' type='range' min='3' max='7' value='4'><output id='bossSizeo'>4</output></div><button class='full warn' onclick='spawnBoss()'>Boss spawnen</button><div class='row'><label>Black Hole HP</label><input id='holeHp' type='range' min='10' max='6000' step='20' value='120'><output id='holeHpo'>120</output></div><div class='row'><label>Radius</label><input id='holeRadius' type='range' min='1' max='7' value='2'><output id='holeRadiuso'>2</output></div><div class='row'><label>Hole Speed</label><input id='holeSpeed' type='range' min='1' max='12' value='2'><output id='holeSpeedo'>2</output></div><button class='full power' onclick='spawnHazard()'>Black Hole spawnen</button><button class='full soft' onclick='spawnPowerUp()'>Random PowerUp droppen</button></div></div>
<script>let synced=false,busy=false;const outs=['maxA','spawnMs','moveMs','smallP','powerP','spawnHp','spawnSpeed','bossHp','bossSize','holeHp','holeRadius','holeSpeed'];function q(o){return Object.keys(o).map(k=>k+'='+encodeURIComponent(o[k])).join('&')}function ping(){fetch('/shooter/input').catch(()=>{})}function by(id){return document.getElementById(id)}function syncOut(){outs.forEach(id=>by(id+'o').value=by(id).value)}outs.forEach(id=>by(id).oninput=syncOut);syncOut();function preset(m){let p=m==0?[5,1700,260,55,35]:m==1?[8,1150,190,40,25]:[13,520,95,20,45];by('maxA').value=p[0];by('spawnMs').value=p[1];by('moveMs').value=p[2];by('smallP').value=p[3];by('powerP').value=p[4];syncOut();applyDirector()}function applyDirector(){fetch('/shooter/director?'+q({max:by('maxA').value,spawn:by('spawnMs').value,move:by('moveMs').value,small:by('smallP').value,power:by('powerP').value,auto:by('autoWeb').checked?1:0})).then(state).catch(()=>{})}function spawnAsteroid(){fetch('/shooter/spawn?'+q({kind:by('spawnKind').value,size:by('spawnSize').value,hp:by('spawnHp').value,speed:by('spawnSpeed').value})).then(state).catch(()=>{})}function spawnBoss(){fetch('/shooter/boss?'+q({hp:by('bossHp').value,size:by('bossSize').value})).then(state).catch(()=>{})}function spawnHazard(){fetch('/shooter/hazard?'+q({type:1,hp:by('holeHp').value,radius:by('holeRadius').value,speed:by('holeSpeed').value})).then(state).catch(()=>{})}function spawnPowerUp(){fetch('/shooter/powerup').then(state).catch(()=>{})}function playerHtml(p,i){let input=i==0?'Controller 1':'I2C-Controller';let status=p.alive?'aktiv':'aus';let cls=p.alive?'':'dead';return `<div class='player p${i}'><b>${p.name}</b><div>${input} · <span class='${cls}'>${status}</span></div><div class='stat'><span>HP ${p.hp}</span><span>Score ${p.score}</span><span>Kills ${p.kills}</span><span>Level ${p.level}</span><span>Basis ${p.baseDamage}</span><span>Shot ${p.damage}</span><span>Charge ${p.chargedDamage}</span><span>Next ${p.nextDamageScore}</span><span>Shield ${p.shield}</span><span>Boost ${p.boost}s</span><span>Multi ${p.multi}s</span><span>Invert ${p.invert}s</span></div></div>`}async function state(){if(busy)return;busy=true;try{let r=await fetch('/shooter/state',{cache:'no-store'}),j=await r.json();by('score').textContent='Team Score '+j.score;by('energyText').textContent='Energie '+j.webEnergy+' / '+j.webEnergyMax;by('energyFill').style.width=Math.max(0,Math.min(100,j.webEnergy*100/j.webEnergyMax))+'%';by('players').innerHTML=j.players.map(playerHtml).join('');by('bossStats').textContent=j.boss.active?`Boss HP ${j.boss.hp} / ${j.boss.maxHp} | Size ${j.boss.size} | Phase ${j.boss.phase||'-'}`:'Kein Boss aktiv';let d=j.director;by('directorStats').innerHTML=`<span>Auto ${d.autoWeb?'an':'aus'}</span><span>Gegner ${d.activeAsteroids}/${d.maxAsteroids}</span><span>PowerUps ${d.activePowerUps}</span><span>Hazards ${d.activeHazards}</span><span>Slow ${d.slow}s</span><span>Move ${d.moveMs}ms</span><span>Spawn ${d.spawnMs}ms</span>`;let lm=j.lastMatch;by('lastMatch').textContent=lm.score>0?`${lm.name} · ${lm.score} · ${lm.duration}s`:'-';if(!synced){by('maxA').value=d.maxAsteroids;by('spawnMs').value=d.spawnMs;by('moveMs').value=d.moveMs;by('smallP').value=d.smallPercent;by('powerP').value=d.powerChance;by('autoWeb').checked=!!d.autoWeb;syncOut();synced=true}}catch(e){}finally{busy=false}}ping();state();setInterval(state,1200);setInterval(ping,3000);</script></body></html>)HTML";
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", shooterHtml);
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

void handleSnakePage() {
    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{margin:0;background:#07111f;color:#edf6ff;font-family:Arial,sans-serif;padding:18px;text-align:center}";
    html += ".wrap{max-width:520px;margin:0 auto}.nav{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin:10px 0}.nav a{background:#17243a;border:1px solid #334155;border-radius:8px;padding:9px 12px;color:#93c5fd;text-decoration:none;font-weight:bold}.card{background:#101b2d;border:1px solid #2b3d55;border-radius:8px;padding:14px;margin:12px 0;text-align:left}";
    html += "h1{font-size:24px;margin:8px 0 12px}h2{font-size:17px;color:#93c5fd;margin:0 0 10px}.row{display:grid;grid-template-columns:140px 1fr;gap:8px;align-items:center;margin:8px 0}label{font-size:13px;color:#bfdbfe}input,select,button{box-sizing:border-box;width:100%;border-radius:8px;border:1px solid #3b4d65;background:#0b1424;color:#fff;padding:11px;font-size:15px}";
    html += "button{background:#2563eb;border:0;font-weight:bold;margin-top:8px}.line{background:#17243a;border-radius:6px;padding:8px;margin:6px 0}.muted{color:#94a3b8}@media(max-width:460px){.row{grid-template-columns:1fr}}</style></head><body><div class='wrap'>";
    html += "<h1>Snake Dashboard</h1>";
    html += dashboardNav("snake");
    html += "<div class='card'><h2>Spielparameter</h2>";
    html += "<form action='/snake/settings' method='POST'>";
    html += "<div class='row'><label>Bots</label><select name='bots'>";
    appendOption(html, 1, snakeBotCount, "1 Bot");
    appendOption(html, 2, snakeBotCount, "2 Bots");
    html += "</select></div>";
    html += "<div class='row'><label>Aepfel</label><input type='number' name='food' min='1' max='8' value='" + String(snakeFoodTarget) + "'></div>";
    html += "<div class='row'><label>Obstacles</label><input type='number' name='obs' min='0' max='48' value='" + String(snakeObstacleTarget) + "'></div>";
    html += "<div class='row'><label>Powerups</label><input type='number' name='pow' min='0' max='6' value='" + String(snakePowerupTarget) + "'></div>";
    html += "<button>Snake anwenden</button></form></div></div></body></html>";
    sendDashboardHtml(html);
}

void handleSnakeSettings() {
    if (server.hasArg("bots")) snakeBotCount = constrain(server.arg("bots").toInt(), 1, 2);
    if (server.hasArg("food")) snakeFoodTarget = constrain(server.arg("food").toInt(), 1, 8);
    if (server.hasArg("obs")) snakeObstacleTarget = constrain(server.arg("obs").toInt(), 0, 48);
    if (server.hasArg("pow")) snakePowerupTarget = constrain(server.arg("pow").toInt(), 0, 6);
    server.sendHeader("Location", "/snake");
    server.send(303);
}

void handleTetrisPage() {
    String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{margin:0;background:#07111f;color:#edf6ff;font-family:Arial,sans-serif;padding:18px;text-align:center}";
    html += ".wrap{max-width:520px;margin:0 auto}.nav{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin:10px 0}.nav a{background:#17243a;border:1px solid #334155;border-radius:8px;padding:9px 12px;color:#93c5fd;text-decoration:none;font-weight:bold}.card{background:#101b2d;border:1px solid #2b3d55;border-radius:8px;padding:14px;margin:12px 0;text-align:left}";
    html += "h1{font-size:24px;margin:8px 0 12px}h2{font-size:17px;color:#93c5fd;margin:0 0 10px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.row{display:grid;grid-template-columns:145px 1fr;gap:8px;align-items:center;margin:8px 0}.line{background:#17243a;border-radius:6px;padding:8px;margin:6px 0}.big{font-size:26px;color:#facc15;font-weight:bold}.muted{color:#94a3b8}input,button{box-sizing:border-box;width:100%;border-radius:8px;border:1px solid #3b4d65;background:#0b1424;color:#fff;padding:10px;font-size:15px}button{background:#2563eb;border:0;font-weight:bold;margin-top:8px}@media(max-width:460px){.grid,.row{grid-template-columns:1fr}}</style>";
    html += "</head><body><div class='wrap'><h1>Tetris Dashboard</h1>";
    html += dashboardNav("tetris");
    html += "<div class='card'><h2>Live</h2><div class='grid'><div class='line'>Total<div class='big' id='total'>0</div></div><div class='line'>Matrix<div class='big' id='board'>0</div></div></div>";
    html += "<div class='line'>Status: <b id='state'>-</b></div></div>";
    html += "<div class='card'><h2>Bot</h2><form action='/tetris/settings' method='POST'><select name='botmode'>";
    appendOption(html, 0, tetrisFastBotMode ? 1 : 0, "Normal");
    appendOption(html, 1, tetrisFastBotMode ? 1 : 0, "Schnell");
    html += "</select><button>Aktivieren</button></form></div>";
    html += "</div><script>async function load(){try{let r=await fetch('/tetris/state');let j=await r.json();document.getElementById('total').textContent=j.score;document.getElementById('board').textContent=j.board;document.getElementById('state').textContent=j.playing?'laeuft':'bereit';}catch(e){}}setInterval(load,700);load();</script></body></html>";
    sendDashboardHtml(html);
}

void handleTetrisSettings() {
    if (server.hasArg("botmode")) tetrisFastBotMode = server.arg("botmode").toInt() == 1;
    server.sendHeader("Location", "/tetris");
    server.send(303);
}

void handleTetrisState() {
    String json = "{";
    json += "\"score\":" + String((uint32_t)tetrisCurrentScore);
    json += ",\"board\":" + String((uint32_t)tetrisBoardScore);
    json += ",\"playing\":" + String(tetrisPlaying ? 1 : 0);
    json += ",\"fastBot\":" + String(tetrisFastBotMode ? 1 : 0);
    json += "}";
    server.send(200, "application/json", json);
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
    ConfigGuard guard;
    if (server.hasArg("city")) {
        saveCity(server.arg("city"));
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateTheme() {
    ConfigGuard guard;
    if (server.hasArg("theme")) {
        saveTheme(server.arg("theme").toInt());
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateClock() {
    ConfigGuard guard;
    if (server.hasArg("clock")) {
        saveClockStyle(server.arg("clock").toInt());
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdatePongMode() {
    if (server.hasArg("mode")) pongBotMode = constrain(server.arg("mode").toInt(), 0, 2);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateStandby() {
    if (server.hasArg("text")) setStandbyText(server.arg("text"));
    requestAppTaskSwitch(11);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateJoysticks() {
    if (server.hasArg("j1")) joystick1DeadzonePercent = constrain(server.arg("j1").toInt(), 1, 20);
    if (server.hasArg("j2")) joystick2DeadzonePercent = constrain(server.arg("j2").toInt(), 1, 20);
    if (server.hasArg("j3")) joystick3DeadzonePercent = constrain(server.arg("j3").toInt(), 1, 20);
    joystick1.setDeadZonePercent(joystick1DeadzonePercent);
    joystick2.setDeadZonePercent(joystick2DeadzonePercent);
    joystick3.setDeadZonePercent(joystick3DeadzonePercent);
    server.sendHeader("Location", "/");
    server.send(303);
}


void startCaptivePortal() {
    if (captivePortalActive) return;

    // Clear a possibly restored legacy/open AP before creating the WPA2 AP.
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    vTaskDelay(pdMS_TO_TICKS(40));
    WiFi.mode(WIFI_AP_STA);
    bool ok = WiFi.softAP(captiveSsid, captivePassword, 1, false, 4);
    IPAddress apIp = WiFi.softAPIP();
    if (ok) {
        dnsServer.start(DNS_PORT, "*", apIp);
        captivePortalActive = true;
        Serial.println("Captive Portal aktiv: http://" + apIp.toString() +
                       " / SSID: " + String(captiveSsid) + " / Passwort gesetzt");
    } else {
        Serial.println("Captive Portal konnte nicht gestartet werden.");
    }
}

void stopCaptivePortal() {
    bool wasActive = captivePortalActive;
    wifi_mode_t mode = WiFi.getMode();
    bool apRadioEnabled = mode == WIFI_AP || mode == WIFI_AP_STA;
    if (wasActive) dnsServer.stop();
    if (apRadioEnabled) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }
    captivePortalActive = false;
    if (wasActive || apRadioEnabled)
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
    ConfigGuard guard;
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
    server.on("/sound/state", HTTP_GET, handleSoundState);
    server.on("/sound/settings", HTTP_POST, handleSoundSettings);
    server.on("/sound/test", HTTP_POST, handleSoundTest);
    server.on("/snake", HTTP_GET, handleSnakePage);
    server.on("/snake/settings", HTTP_POST, handleSnakeSettings);
    server.on("/tetris", HTTP_GET, handleTetrisPage);
    server.on("/tetris/settings", HTTP_POST, handleTetrisSettings);
    server.on("/tetris/state", HTTP_GET, handleTetrisState);
    server.on("/shooter", HTTP_GET, handleShooterPage);
    server.on("/shooter/state", HTTP_GET, handleShooterState);
    server.on("/shooter/input", HTTP_POST, handleShooterInput);
    server.on("/shooter/spawn", HTTP_POST, handleShooterSpawn);
    server.on("/shooter/boss", HTTP_POST, handleShooterBoss);
    server.on("/shooter/hazard", HTTP_POST, handleShooterHazard);
    server.on("/shooter/powerup", HTTP_POST, handleShooterPowerUp);
    server.on("/shooter/director", HTTP_POST, handleShooterDirector);
    server.on("/saveWifi", HTTP_POST, handleSaveWifi);
    server.on("/updateCity", HTTP_POST, handleUpdateCity);
    server.on("/updateTheme", HTTP_POST, handleUpdateTheme);
    server.on("/updateClock", HTTP_POST, handleUpdateClock);
    server.on("/updatePongMode", HTTP_POST, handleUpdatePongMode);
    server.on("/updateStandby", HTTP_POST, handleUpdateStandby);
    server.on("/updateJoysticks", HTTP_POST, handleUpdateJoysticks);
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
    registerCurrentTaskWithWatchdog();
    for (;;) {
        if (captivePortalActive) dnsServer.processNextRequest();
        server.handleClient();
        feedRuntimeWatchdog();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

#if 0 // Legacy user database and rankings: intentionally not part of the firmware anymore.
void saveHighScore(String user, int score) {
    ConfigGuard guard;
    user.trim();
    if (user == "") return;
    String checkName = user;
    checkName.toUpperCase();
    if (checkName == "P1" || checkName == "P2" || checkName == "P1+P2") return;

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

        if (writeJsonAtomically("/config.json", doc)) {
            Serial.println("Neuer Highscore gespeichert: " + String(score));
        } else {
            Serial.println("Highscore konnte nicht gespeichert werden.");
        }
    } else {
        Serial.println("Score war nicht hoch genug. Aktueller Highscore: " + String(currentHigh));
    }
}

void printTopThree() {
    ConfigGuard guard;
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
    ConfigGuard guard;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        JsonDocument doc;
        deserializeJson(doc, file);
        file.close();
        return readHighScore(doc[user]);
    }
    return 0;
}

void saveSnakeHighScore(String user, int score) {
    ConfigGuard guard;
    user.trim();
    if (user == "" || score <= 0) return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    JsonObject obj = userObject(doc, user);
    int currentHigh = readSnakeHighScore(doc[user]);
    if (score > currentHigh) {
        obj["snake"] = score;
        removeLegacyUserKeys(obj);

        if (writeJsonAtomically("/config.json", doc)) {
            Serial.println("Neuer Snake-Highscore gespeichert: " + String(score));
        } else {
            Serial.println("Snake-Highscore konnte nicht gespeichert werden.");
        }
    }
}

int getSnakeHighScore(String user) {
    ConfigGuard guard;
    user.trim();
    if (user == "" || !LittleFS.exists("/config.json")) return 0;

    File file = LittleFS.open("/config.json", "r");
    if (!file) return 0;

    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();
    return readSnakeHighScore(doc[user]);
}

void savePongHighScore(String user, int score) {
    ConfigGuard guard;
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

        if (writeJsonAtomically("/config.json", doc)) {
            Serial.println("Neuer Pong-Highscore gespeichert: " + String(score));
        } else {
            Serial.println("Pong-Highscore konnte nicht gespeichert werden.");
        }
    }
}

int getPongHighScore(String user) {
    ConfigGuard guard;
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
    ConfigGuard guard;
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

        if (writeJsonAtomically("/config.json", doc)) {
            Serial.println("Neuer Pacman-Highscore gespeichert: " + String(score));
        } else {
            Serial.println("Pacman-Highscore konnte nicht gespeichert werden.");
        }
    }
}

int getPacmanHighScore(String user) {
    ConfigGuard guard;
    user.trim();
    if (user == "" || !LittleFS.exists("/config.json")) return 0;

    File file = LittleFS.open("/config.json", "r");
    if (!file) return 0;

    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();
    return readPacmanHighScore(doc[user]);
}

void saveTetrisHighScore(String user, int score) {
    ConfigGuard guard;
    user.trim();
    if (user == "" || score <= 0) return;

    JsonDocument doc;
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        deserializeJson(doc, file);
        file.close();
    }

    JsonObject obj = userObject(doc, user);
    int currentHigh = readTetrisHighScore(doc[user]);
    if (score > currentHigh) {
        obj["tetris"] = score;
        removeLegacyUserKeys(obj);

        if (writeJsonAtomically("/config.json", doc)) {
            Serial.println("Neuer Tetris-Highscore gespeichert: " + String(score));
        } else {
            Serial.println("Tetris-Highscore konnte nicht gespeichert werden.");
        }
    }
}

int getTetrisHighScore(String user) {
    ConfigGuard guard;
    user.trim();
    if (user == "" || !LittleFS.exists("/config.json")) return 0;

    File file = LittleFS.open("/config.json", "r");
    if (!file) return 0;

    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();
    return readTetrisHighScore(doc[user]);
}

void getTopScores(PlayerData* list, int& count) {
    ConfigGuard guard;
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

void getTopSnakeScores(PlayerData* list, int& count) {
    ConfigGuard guard;
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
        if (!isSnakeScoreEntry(kv)) {
            continue;
        }

        int score = readSnakeHighScore(kv.value());
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
    ConfigGuard guard;
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
    ConfigGuard guard;
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

void getTopTetrisScores(PlayerData* list, int& count) {
    ConfigGuard guard;
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
        if (!isTetrisScoreEntry(kv)) {
            continue;
        }

        int score = readTetrisHighScore(kv.value());
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
#endif
